/* Copyright (C) 2026 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "layer_test_helper.h"
#include "../debug_marker/debug_marker.h"
#include "../debug_marker/debug_marker_perfetto.h"
#include <vulkan/vulkan_core.h>
#include <gtest/gtest.h>
#include <mutex>
#include <stdlib.h>
#include <string>
#include <unordered_map>
#include <vector>

static const char* kLayerName = "VK_LAYER_GOOGLE_DebugMarker";

namespace {

void EnsureInProcessPerfettoInitialized() {
    static std::once_flag init_once;
    std::call_once(init_once, []() {
        perfetto::TracingInitArgs args;
        args.backends = perfetto::kInProcessBackend;
        perfetto::Tracing::Initialize(args);
        InitializeDebugMarkerPerfetto();
    });
}

std::unique_ptr<perfetto::TracingSession> StartInProcessTrace() {
    EnsureInProcessPerfettoInitialized();

    perfetto::TraceConfig cfg;
    cfg.add_buffers()->set_size_kb(1024);
    auto* ds_cfg = cfg.add_data_sources()->mutable_config();
    ds_cfg->set_name("track_event");

    auto session = perfetto::Tracing::NewTrace(perfetto::kInProcessBackend);
    session->Setup(cfg);
    session->StartBlocking();
    return session;
}

std::vector<char> StopAndReadTrace(std::unique_ptr<perfetto::TracingSession> session) {
    perfetto::TrackEvent::Flush();
    session->StopBlocking();
    return session->ReadTraceBlocking();
}

struct DecodedRawObjectNamePacket {
    uint64_t vk_device = 0;
    int32_t object_type = 0;
    uint64_t object = 0;
    std::string object_name;
    bool has_timestamp_clock_id = false;
    uint32_t timestamp_clock_id = 0;
};

struct DecodedObjectNameInstant {
    std::string event_name;
    int64_t object_type = 0;
    uint64_t object_handle = 0;
    std::string object_name;
};

struct DecodedTrace {
    std::vector<DecodedRawObjectNamePacket> raw_packets;
    std::vector<DecodedObjectNameInstant> instants;
    bool saw_incremental_default_clock = false;
};

DecodedTrace DecodeTrace(const std::vector<char>& raw_trace) {
    DecodedTrace result;
    // Per-sequence interned tables for event_names and debug_annotation_names.
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, std::string>> event_names;
    std::unordered_map<uint32_t, std::unordered_map<uint64_t, std::string>> annotation_names;

    perfetto::protos::pbzero::Trace::Decoder trace(
        reinterpret_cast<const uint8_t*>(raw_trace.data()), raw_trace.size());

    for (auto packet_it = trace.packet(); packet_it; ++packet_it) {
        perfetto::protos::pbzero::TracePacket::Decoder packet(*packet_it);
        const uint32_t seq_id = packet.trusted_packet_sequence_id();

        if (packet.has_incremental_state_cleared() && packet.incremental_state_cleared()) {
            event_names[seq_id].clear();
            annotation_names[seq_id].clear();
        }

        if (packet.has_trace_packet_defaults()) {
            perfetto::protos::pbzero::TracePacketDefaults::Decoder defaults(
                packet.trace_packet_defaults());
            if (defaults.has_timestamp_clock_id() &&
                defaults.timestamp_clock_id() !=
                    perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME) {
                result.saw_incremental_default_clock = true;
            }
        }

        if (packet.has_interned_data()) {
            perfetto::protos::pbzero::InternedData::Decoder interned(packet.interned_data());
            for (auto it = interned.event_names(); it; ++it) {
                perfetto::protos::pbzero::EventName::Decoder entry(*it);
                event_names[seq_id][entry.iid()] = entry.name().ToStdString();
            }
            for (auto it = interned.debug_annotation_names(); it; ++it) {
                perfetto::protos::pbzero::DebugAnnotationName::Decoder entry(*it);
                annotation_names[seq_id][entry.iid()] = entry.name().ToStdString();
            }
        }

        if (packet.has_vulkan_api_event()) {
            perfetto::protos::pbzero::VulkanApiEvent::Decoder api_event(packet.vulkan_api_event());
            if (api_event.has_vk_debug_utils_object_name()) {
                perfetto::protos::pbzero::VulkanApiEvent_VkDebugUtilsObjectName::Decoder marker(
                    api_event.vk_debug_utils_object_name());
                DecodedRawObjectNamePacket decoded;
                decoded.vk_device = marker.vk_device();
                decoded.object_type = marker.object_type();
                decoded.object = marker.object();
                decoded.object_name = marker.object_name().ToStdString();
                decoded.has_timestamp_clock_id = packet.has_timestamp_clock_id();
                decoded.timestamp_clock_id = packet.timestamp_clock_id();
                result.raw_packets.push_back(std::move(decoded));
            }
        }

        if (packet.has_track_event()) {
            perfetto::protos::pbzero::TrackEvent::Decoder track_event(packet.track_event());
            if (track_event.type() != perfetto::protos::pbzero::TrackEvent::TYPE_INSTANT) {
                continue;
            }

            std::string ev_name;
            if (track_event.has_name()) {
                ev_name = track_event.name().ToStdString();
            } else if (track_event.has_name_iid()) {
                ev_name = event_names[seq_id][track_event.name_iid()];
            }
            if (ev_name != "VulkanObjectName") {
                continue;
            }

            DecodedObjectNameInstant instant;
            instant.event_name = std::move(ev_name);
            for (auto ann_it = track_event.debug_annotations(); ann_it; ++ann_it) {
                perfetto::protos::pbzero::DebugAnnotation::Decoder ann(*ann_it);
                std::string key;
                if (ann.has_name()) {
                    key = ann.name().ToStdString();
                } else if (ann.has_name_iid()) {
                    key = annotation_names[seq_id][ann.name_iid()];
                }

                if (key == "object_type") {
                    instant.object_type = ann.has_int_value()
                                              ? ann.int_value()
                                              : static_cast<int64_t>(ann.uint_value());
                } else if (key == "object_handle") {
                    instant.object_handle = ann.uint_value();
                } else if (key == "object_name") {
                    instant.object_name = ann.string_value().ToStdString();
                }
            }
            result.instants.push_back(std::move(instant));
        }
    }
    return result;
}

}  // namespace

class DebugMarkerTests : public VkTestFramework {
   public:
    ~DebugMarkerTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(DebugMarkerTests, CombinedTest) {
    TEST_DESCRIPTION("Combined test for DebugMarker layer");

    DebugMarker::Get().Clear();
    layer_test::VulkanInstanceBuilder inst_builder;
    inst_builder.AddExtension("VK_EXT_debug_utils");
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkInstance instance = inst_builder.GetInstance();
    EXPECT_NE(instance, VK_NULL_HANDLE);

    // Verify that GetInstanceProcAddr returns layer functions
    PFN_vkVoidFunction pfnSetDebugUtilsObjectNameEXT = vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");
    EXPECT_NE(pfnSetDebugUtilsObjectNameEXT, nullptr);

    PFN_vkVoidFunction pfnCmdDebugMarkerBeginEXT = vkGetInstanceProcAddr(instance, "vkCmdDebugMarkerBeginEXT");
    EXPECT_NE(pfnCmdDebugMarkerBeginEXT, nullptr);

    // 1. Set instance name
    DebugMarker::Get().SetDebugObjectName(0, VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance");

    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance"));

    // 2. Override new name
    DebugMarker::Get().SetDebugObjectName(0, VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed");
    EXPECT_TRUE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance"));

    // 3. Clear
    DebugMarker::Get().Clear();
    EXPECT_FALSE(DebugMarker::Get().HasDebugObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
}

TEST_F(DebugMarkerTests, EmitsBothRawPacketAndSqlVisibleInstantEvent) {
    TEST_DESCRIPTION("Verify SetDebugObjectName emits both VkDebugUtilsObjectName and a VulkanObjectName instant event");

    DebugMarker::Get().Clear();
    auto session = StartInProcessTrace();

    DebugMarker::Get().SetDebugObjectName(0xD001, VK_OBJECT_TYPE_BUFFER, 0xB001, "VertexBuffer");
    DebugMarker::Get().SetDebugObjectName(0xD001, VK_OBJECT_TYPE_IMAGE, 0xA002, "AlbedoTexture");

    DecodedTrace trace = DecodeTrace(StopAndReadTrace(std::move(session)));
    DebugMarker::Get().Clear();

    ASSERT_EQ(trace.raw_packets.size(), 2u);
    EXPECT_EQ(trace.raw_packets[0].vk_device, 0xD001u);
    EXPECT_EQ(trace.raw_packets[0].object_type, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(trace.raw_packets[0].object, 0xB001u);
    EXPECT_EQ(trace.raw_packets[0].object_name, "VertexBuffer");

    EXPECT_EQ(trace.raw_packets[1].vk_device, 0xD001u);
    EXPECT_EQ(trace.raw_packets[1].object_type, VK_OBJECT_TYPE_IMAGE);
    EXPECT_EQ(trace.raw_packets[1].object, 0xA002u);
    EXPECT_EQ(trace.raw_packets[1].object_name, "AlbedoTexture");

    ASSERT_EQ(trace.instants.size(), 2u);
    EXPECT_EQ(trace.instants[0].object_type, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(trace.instants[0].object_handle, 0xB001u);
    EXPECT_EQ(trace.instants[0].object_name, "VertexBuffer");

    EXPECT_EQ(trace.instants[1].object_type, VK_OBJECT_TYPE_IMAGE);
    EXPECT_EQ(trace.instants[1].object_handle, 0xA002u);
    EXPECT_EQ(trace.instants[1].object_name, "AlbedoTexture");
}

TEST_F(DebugMarkerTests, RawPacketsExplicitlySpecifyBoottimeClockId) {
    TEST_DESCRIPTION("Verify raw VkDebugUtilsObjectName packets set BUILTIN_CLOCK_BOOTTIME so they do not inherit the TrackEvent incremental clock");

    DebugMarker::Get().Clear();
    auto session = StartInProcessTrace();

    DebugMarker::Get().SetDebugObjectName(0xD001, VK_OBJECT_TYPE_BUFFER, 0xB001, "ClockCheckBuffer");

    DecodedTrace trace = DecodeTrace(StopAndReadTrace(std::move(session)));
    DebugMarker::Get().Clear();

    // Emitting the VulkanObjectName track event causes the SDK to publish TracePacketDefaults
    // with an incremental clock ID on this sequence. Every raw packet on the sequence must
    // therefore carry an explicit BUILTIN_CLOCK_BOOTTIME clock ID.
    EXPECT_TRUE(trace.saw_incremental_default_clock);
    ASSERT_EQ(trace.raw_packets.size(), 1u);
    EXPECT_TRUE(trace.raw_packets[0].has_timestamp_clock_id);
    EXPECT_EQ(trace.raw_packets[0].timestamp_clock_id,
              static_cast<uint32_t>(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME));
}

TEST_F(DebugMarkerTests, SessionStartReplayEmitsBothRepresentations) {
    TEST_DESCRIPTION("Verify sessions that attach after objects were named receive both the raw packet and the instant event via OnStart replay");

    EnsureInProcessPerfettoInitialized();
    DebugMarker::Get().Clear();

    // Name objects before the tracing session starts, including a rename.
    DebugMarker::Get().SetDebugObjectName(0xD002, VK_OBJECT_TYPE_BUFFER, 0xB010, "OldName");
    DebugMarker::Get().SetDebugObjectName(0xD002, VK_OBJECT_TYPE_BUFFER, 0xB010, "FinalBufferName");
    DebugMarker::Get().SetDebugObjectName(0xD002, VK_OBJECT_TYPE_DEVICE_MEMORY, 0xC020, "SceneHeap");

    // Starting the session triggers MarkerSessionObserver::OnStart -> EmitAllDebugMarkers().
    auto session = StartInProcessTrace();
    DecodedTrace trace = DecodeTrace(StopAndReadTrace(std::move(session)));
    DebugMarker::Get().Clear();

    // debug_object_names_ is ordered by (object_type, handle), so VK_OBJECT_TYPE_DEVICE_MEMORY (8)
    // is replayed before VK_OBJECT_TYPE_BUFFER (9).
    ASSERT_EQ(trace.raw_packets.size(), 2u);
    EXPECT_EQ(trace.raw_packets[0].object_type, VK_OBJECT_TYPE_DEVICE_MEMORY);
    EXPECT_EQ(trace.raw_packets[0].object, 0xC020u);
    EXPECT_EQ(trace.raw_packets[0].object_name, "SceneHeap");
    EXPECT_TRUE(trace.raw_packets[0].has_timestamp_clock_id);
    EXPECT_EQ(trace.raw_packets[0].timestamp_clock_id,
              static_cast<uint32_t>(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME));

    EXPECT_EQ(trace.raw_packets[1].object_type, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(trace.raw_packets[1].object, 0xB010u);
    EXPECT_EQ(trace.raw_packets[1].object_name, "FinalBufferName");
    EXPECT_TRUE(trace.raw_packets[1].has_timestamp_clock_id);
    EXPECT_EQ(trace.raw_packets[1].timestamp_clock_id,
              static_cast<uint32_t>(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME));

    ASSERT_EQ(trace.instants.size(), 2u);
    EXPECT_EQ(trace.instants[0].object_type, VK_OBJECT_TYPE_DEVICE_MEMORY);
    EXPECT_EQ(trace.instants[0].object_handle, 0xC020u);
    EXPECT_EQ(trace.instants[0].object_name, "SceneHeap");

    EXPECT_EQ(trace.instants[1].object_type, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(trace.instants[1].object_handle, 0xB010u);
    EXPECT_EQ(trace.instants[1].object_name, "FinalBufferName");
}

