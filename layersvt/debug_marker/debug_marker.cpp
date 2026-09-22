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

#include "debug_marker.h"
#include "debug_marker_perfetto.h"
#include "perfetto/perfetto.h"

DebugMarker& DebugMarker::Get() {
    static DebugMarker instance;
    return instance;
}

void DebugMarker::SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance) {
    std::lock_guard<std::mutex> lock(mutex_);
    vk_instance_map_[phys_dev] = instance;
}

VkInstance DebugMarker::GetVkInstance(VkPhysicalDevice phys_dev) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = vk_instance_map_.find(phys_dev);
    if (it != vk_instance_map_.end()) return it->second;
    return VK_NULL_HANDLE;
}

void DebugMarker::Emit(const DebugObjectName& marker) {
    const uint64_t device = marker.vk_device;
    const int32_t type = marker.object_type;
    const uint64_t handle = marker.handle;
    const std::string name_str = marker.name;

    // Representation 1: a VulkanApiEvent.VkDebugUtilsObjectName packet. trace_processor folds these
    // into a private lookup that it consults only while parsing GpuRenderStageEvent, which is what
    // puts render pass and render target names on GPU queue slices. The names are not reachable
    // from SQL in this form.
    //
    // This is written as a raw trace packet rather than a track event, so it reaches every session
    // in which this layer's data source is enabled, independently of category filtering.
    //
    // The clock id must be set explicitly. Once this sequence carries a track event, the SDK
    // publishes TracePacketDefaults with timestamp_clock_id = the incremental clock, and a packet
    // that sets a timestamp without naming a clock inherits it. An absolute boot-time value read
    // as a delta advances the sequence clock by hours, and every later event on the sequence -
    // including the VulkanObjectName instants below - inherits the corrupted base.
    perfetto::TrackEvent::Trace([device, type, handle, name_str](perfetto::TrackEvent::TraceContext ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(perfetto::base::GetBootTimeNs().count());
        packet->set_timestamp_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
        auto event = packet->set_vulkan_api_event()->set_vk_debug_utils_object_name();
        event->set_vk_device(device);
        event->set_object_type(type);
        event->set_object(handle);
        event->set_object_name(name_str.c_str());
    });

    // Representation 2: a "VulkanObjectName" instant event. This lands in trace_processor's slice
    // table with its arguments intact, so a consumer can join names to any object by handle, for
    // any object type. Sherlock's Vulkan memory snapshot uses it to label buffers, images and
    // device memory blocks, which representation 1 cannot do.
    //
    // Both are written because neither subsumes the other: the packet is the only form the GPU
    // render stage parser reads, and the slice is the only form SQL can see. A name is small, so
    // publishing it twice costs far less than losing either consumer.
    TRACE_EVENT_INSTANT("VulkanDebugMarker", "VulkanObjectName",
                        "object_type", type,
                        "object_handle", handle,
                        "object_name", name_str.c_str());
}

void DebugMarker::SetDebugObjectName(uint64_t device, int32_t type, uint64_t handle, const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);

    DebugObjectName& marker = debug_object_names_[std::make_pair(type, handle)];
    marker = DebugObjectName(device, type, handle, name ? name : "NULL");

    Emit(marker);
}

void DebugMarker::EmitAllDebugMarkers() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : debug_object_names_) {
        Emit(entry.second);
    }
}

void DebugMarker::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    vk_instance_map_.clear();
    debug_object_names_.clear();
}

bool DebugMarker::HasDebugObjectName(int32_t type, uint64_t handle, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = debug_object_names_.find(std::make_pair(type, handle));
    if (it == debug_object_names_.end()) return false;
    return it->second.name == name;
}
