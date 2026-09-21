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

#include "device_memory_report_perfetto.h"
#include "device_memory_report.h"
#include "object_names/vulkan_object_names.h"
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

namespace layersvt {

// Binds the shared object name store to this layer's track event data source.
//
// Unlike the DebugMarker layer, this emits an ordinary instant event rather than a
// VulkanApiEvent.VkDebugUtilsObjectName trace packet. trace_processor only reads the latter back for
// render passes, render targets and command buffers, so a name on the buffers and images this layer
// reports on would be parsed and then never surfaced. As an instant event the name becomes a slice
// that a consumer can query directly and join to the memory events by object handle.
void EmitVulkanObjectName(const VulkanObjectName& object_name) {
    TRACE_EVENT_INSTANT("VulkanDeviceMemoryReport", "VulkanObjectName",
                        "object_type", object_name.object_type,
                        "object_handle", object_name.handle,
                        "object_name", object_name.name.c_str());
}

}  // namespace layersvt

namespace {

class DeviceMemoryReportSessionObserver : public perfetto::TrackEventSessionObserver {
public:
    DeviceMemoryReportSessionObserver() {
        // Touch the singleton during observer construction so DeviceMemoryReport
        // completes construction first and is destroyed after this observer unregisters.
        (void)DeviceMemoryReport::Get();
        (void)layersvt::VulkanObjectNames::Get();
    }

    ~DeviceMemoryReportSessionObserver() override {
        perfetto::TrackEvent::RemoveSessionObserver(this);
    }

    void OnStart(const perfetto::DataSourceBase::StartArgs&) override {
        DeviceMemoryReport::Get().DumpCurrentCountersAndAllocations();
        // Replay the names of objects that were named before this session started, so that
        // memory attributed to a buffer or image can still be traced back to it.
        layersvt::VulkanObjectNames::Get().EmitAll();
    }
};

}  // namespace

void InitializeDeviceMemoryReportPerfetto() {
    static std::once_flag init_flag;
    std::call_once(init_flag, []() {
        static DeviceMemoryReportSessionObserver session_observer;
        perfetto::TracingInitArgs args;
        args.backends = perfetto::kSystemBackend;
        perfetto::Tracing::Initialize(args);
        perfetto::TrackEvent::Register();
        perfetto::TrackEvent::AddSessionObserver(&session_observer);

        if (TRACE_EVENT_CATEGORY_ENABLED("VulkanDeviceMemoryReport")) {
            DeviceMemoryReport::Get().DumpCurrentCountersAndAllocations();
            layersvt::VulkanObjectNames::Get().EmitAll();
        }
    });
}

perfetto::CounterTrack GetCounterTrack(std::string_view name) {
    static std::mutex tracks_mutex;
    // Declare track_names first so LIFO static destruction destroys tracks before the strings it views.
    static std::unordered_set<std::string> track_names;
    static std::unordered_map<std::string_view, perfetto::CounterTrack> tracks;
    std::lock_guard<std::mutex> lock(tracks_mutex);

    auto it = tracks.find(name);
    if (it != tracks.end()) {
        return it->second;
    }

    // CounterTrack captures ProcessTrack::Current().uuid at construction time; ensure tracing is
    // initialized before constructing the first track so it isn't parented to the null/global track.
    InitializeDeviceMemoryReportPerfetto();

    auto name_it = track_names.emplace(name).first;
    perfetto::CounterTrack track = perfetto::CounterTrack(perfetto::DynamicString(*name_it))
        .set_unit(perfetto::CounterTrack::Unit::UNIT_SIZE_BYTES);
    tracks.emplace(*name_it, track);
    return track;
}
