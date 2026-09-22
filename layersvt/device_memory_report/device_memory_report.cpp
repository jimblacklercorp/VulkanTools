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

#include "device_memory_report.h"
#include "device_memory_report_perfetto.h"
#include "perfetto/perfetto.h"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

DeviceMemoryReport& DeviceMemoryReport::Get() {
    static DeviceMemoryReport instance;
    return instance;
}

void DeviceMemoryReport::SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    vk_instance_map_[phys_dev] = instance;
}

VkInstance DeviceMemoryReport::GetVkInstance(VkPhysicalDevice phys_dev) {
    if (phys_dev == VK_NULL_HANDLE) return VK_NULL_HANDLE;
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto it = vk_instance_map_.find(phys_dev);
    if (it != vk_instance_map_.end()) return it->second;

    // Fallback: If only a single VkInstance exists in the process, any valid physical device
    // must belong to that instance.
    if (vk_instance_map_.size() == 1) {
        return vk_instance_map_.begin()->second;
    }
    return VK_NULL_HANDLE;
}

void DeviceMemoryReport::SetDeviceMemoryProperties(VkDevice device, const VkPhysicalDeviceMemoryProperties& props) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    device_memory_properties_map_[device] = props;
}

void DeviceMemoryReport::SetAllocationMemoryProperties(uint64_t memory_handle, VkMemoryPropertyFlags property_flags) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto& allocation = memory_allocations_[memory_handle];
    allocation.mem_flags = property_flags;
    // An allocation with total_size > 0 represents an active physical memory allocation that
    // has already been recorded and contributed to Perfetto counter tracks.
    // Changing its memory property flags (e.g., HOST_VISIBLE or LAZILY_ALLOCATED) can alter
    // how its unbound memory headroom is classified into usage categories (such as staging
    // vs general buffer or transient memoryless). We must update the unbound counter to move
    // the reported bytes from the old usage track to the newly classified track.
    // If total_size == 0, the allocation has not yet been instantiated (or is being configured
    // prior to allocation), so no counter bytes have been emitted to Perfetto yet.
    if (allocation.total_size > 0) {
        UpdateAllocationUnboundCounter(memory_handle);
    }
}

void DeviceMemoryReport::OnDestroyDevice(VkDevice device) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    has_callback_map_.erase(device);
    device_memory_properties_map_.erase(device);
}

void VKAPI_PTR DeviceMemoryReport::MemoryReportCallback(const VkDeviceMemoryReportCallbackDataEXT* pCallbackData, void* pUserData) {
    DeviceMemoryReport::Get().OnMemoryReportEvent(pCallbackData);
}

const char* GetBufferCluster(VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags) {
    const VkBufferUsageFlags kFunctionalBufferUsageFlags =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
        VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT |
        VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT;

    // 1. Host staging: Must be host-visible and not a primary GPU functional buffer
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        (usage & (VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)) &&
        !(usage & kFunctionalBufferUsageFlags)) {
        return "staging_transfer";
    }
    // 2. Ray tracing acceleration structures & shader binding tables
    if (usage & (VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                 VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)) {
        return "ray_tracing";
    }
    // 3. Indirect draw / dispatch commands & conditional rendering
    if (usage & (VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT)) {
        return "indirect_gpu_driven";
    }
    // 4. Compute storage buffers (SSBOs & Storage Texel Buffers)
    if (usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) {
        return "compute_storage";
    }
    // 5. Uniform buffers & uniform texel buffers
    if (usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT)) {
        return "uniform_constants";
    }
    // 6. Geometry & mesh data (Vertex + Index)
    if (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) {
        return "geometry_mesh";
    }
    // 7. Transform feedback stream-out
    if (usage & (VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT | VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT)) {
        return "transform_feedback";
    }
    return "general_buffer";
}

const char* GetImageCluster(VkImageUsageFlags usage, VkMemoryPropertyFlags memFlags) {
    // 1. Mobile TBDR on-chip tile memoryless attachments
    if ((usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) || 
        (memFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)) {
        return "transient_memoryless";
    }
    // 2. Compute storage write targets (UAVs)
    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        return "storage_compute_image";
    }
    // 3. Depth / stencil render targets
    if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
        return "depth_stencil_target";
    }
    // 4. Color render targets
    if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
        return "color_render_target";
    }
    // 5. Sampled textures
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        return "static_texture";
    }
    return "general_image";
}

// Construct a Perfetto track name for visualizing memory usage by category in the UI.
static std::string GetUsageTrackName(bool is_driver, std::string_view usage) {
    std::string track = is_driver ? "vulkan.mem.driver.usage." : "vulkan.mem.app.usage.";
    track += usage;
    return track;
}

void DeviceMemoryReport::AddCounterBytes(const std::string& track, uint64_t size) {
    uint64_t& bytes = usage_memory_bytes_[track];
    bytes += size;
    TRACE_COUNTER("VulkanDeviceMemoryReport", GetCounterTrack(track), bytes);
}

void DeviceMemoryReport::SubtractCounterBytes(const std::string& track, uint64_t size) {
    uint64_t& bytes = usage_memory_bytes_[track];
    bytes = (bytes >= size) ? (bytes - size) : 0;
    TRACE_COUNTER("VulkanDeviceMemoryReport", GetCounterTrack(track), bytes);
}

void DeviceMemoryReport::UpdateAllocationUnboundCounter(uint64_t memory_handle) {
    auto allocation_it = memory_allocations_.find(memory_handle);
    if (allocation_it == memory_allocations_.end()) return;
    auto& allocation = allocation_it->second;

    uint64_t bound_size = 0;
    std::vector<std::pair<VkDeviceSize, VkDeviceSize>> intervals;
    intervals.reserve(allocation.sub_allocations.size());
    for (const auto& suballocation : allocation.sub_allocations) {
        if (suballocation.size > 0) {
            VkDeviceSize end = (suballocation.offset + suballocation.size < suballocation.offset) ? UINT64_MAX : (suballocation.offset + suballocation.size);
            intervals.emplace_back(suballocation.offset, end);
        }
    }
    if (!intervals.empty()) {
        std::sort(intervals.begin(), intervals.end());

        // Overlapping and adjacent intervals are merged:
        // bound_size is calculated as the mathematical union (distinct physical footprint) of all intervals.
        VkDeviceSize current_start = intervals[0].first;
        VkDeviceSize current_end = intervals[0].second;
        for (size_t i = 1; i < intervals.size(); ++i) {
            if (intervals[i].first <= current_end) {
                // Overlapping or adjacent interval: extend current merged range
                current_end = std::max(current_end, intervals[i].second);
            } else {
                // Disjoint interval: add previous merged interval size and begin next range
                bound_size += (current_end - current_start);
                current_start = intervals[i].first;
                current_end = intervals[i].second;
            }
        }
        bound_size += (current_end - current_start);
    }

    uint64_t new_unbound = (allocation.total_size > bound_size) ? (allocation.total_size - bound_size) : 0;
    
    const char* cluster_name = "unbound_memory";
    if (allocation.is_driver &&
        (allocation.object_type == VK_OBJECT_TYPE_IMAGE ||
         allocation.object_type == VK_OBJECT_TYPE_BUFFER)) {
        auto res_it = resources_.find(allocation.object_handle);
        // If the memory object has an associated resource with a specific usage, use it as the track name.
        if (res_it != resources_.end()) {
            cluster_name = res_it->second.GetCluster(allocation.mem_flags);
        }
    }
    allocation.cluster_name = cluster_name;
    std::string new_unbound_track = GetUsageTrackName(allocation.is_driver, cluster_name);

    // If the unbound memory usage track name or the number of unbound bytes has changed,
    // update the global counters by subtracting the old bytes from the old track 
    // and adding the new bytes to the new track.
    if (allocation.unbound_usage_track != new_unbound_track || allocation.applied_unbound_bytes != new_unbound) {
        if (allocation.applied_unbound_bytes > 0) {
            SubtractCounterBytes(allocation.unbound_usage_track, allocation.applied_unbound_bytes);
        }
        if (new_unbound > 0) {
            AddCounterBytes(new_unbound_track, new_unbound);
        }
    }
    allocation.unbound_usage_track = new_unbound_track;
    allocation.applied_unbound_bytes = new_unbound;
}

namespace {

struct AllocationTraceEvent {
    const char* operation;
    const char* source;
    uint64_t memory_object_id;
    VkDeviceSize size;
    VkDeviceSize offset;
    uint64_t object_handle;
    const char* memory_type;
};

void EmitAllocationTraceEvent(const AllocationTraceEvent& event) {
    TRACE_EVENT_INSTANT("VulkanDeviceMemoryReport", "VulkanMemoryAllocation",
                        "operation", event.operation,
                        "source", event.source,
                        "memory_object_id", event.memory_object_id,
                        "size", static_cast<uint64_t>(event.size),
                        "offset", static_cast<uint64_t>(event.offset),
                        "object_handle", event.object_handle,
                        "memory_type", event.memory_type);
}

/**
 * @brief Writes one object name to the trace, for consumers to join onto memory events by handle.
 *
 * Emits a "VulkanObjectName" instant event under the "VulkanDeviceMemoryReport" category with
 * three debug annotations:
 *   - "object_type" (int32_t): the VkObjectType value of the named object.
 *   - "object_handle" (uint64_t): the raw Vulkan handle of the object.
 *   - "object_name" (string): the debug name assigned to the object, or an empty string when a
 *     previously assigned name has been cleared.
 */
void EmitDebugObjectName(VkObjectType object_type, uint64_t object_handle, std::string_view name) {
    TRACE_EVENT_INSTANT("VulkanDeviceMemoryReport", "VulkanObjectName",
                        "object_type", static_cast<int32_t>(object_type),
                        "object_handle", object_handle,
                        "object_name", name);
}

}  // namespace

void DeviceMemoryReport::RemoveResourceBinding(uint64_t resource_handle) {
    auto mem_it = resource_to_memory_map_.find(resource_handle);
    if (mem_it == resource_to_memory_map_.end()) return;

    uint64_t memory_handle = mem_it->second;
    resource_to_memory_map_.erase(mem_it);

    auto allocation_it = memory_allocations_.find(memory_handle);
    if (allocation_it == memory_allocations_.end()) return;

    auto& allocation = allocation_it->second;
    auto resource_iterator = resources_.find(resource_handle);
    bool is_image = (resource_iterator != resources_.end()) ? resource_iterator->second.is_image : false;
    const char* cluster_name = (resource_iterator != resources_.end())
                                   ? resource_iterator->second.GetCluster(allocation.mem_flags)
                                   : "unbound_memory";

    auto& suballocations = allocation.sub_allocations;
    // Search by resource handle to identify which specific suballocation to remove,
    // since a single memory block can have multiple resources bound to it.
    for (auto it = suballocations.begin(); it != suballocations.end(); ++it) {
        if (it->resource_handle == resource_handle) {
            VkDeviceSize suballocation_size = it->size;
            VkDeviceSize suballocation_offset = it->offset;
            SubtractCounterBytes(it->usage_track, suballocation_size);
            suballocations.erase(it);
            UpdateAllocationUnboundCounter(memory_handle);

            EmitAllocationTraceEvent({
                .operation = "DESTROY",
                .source = is_image ? "IMAGE" : "BUFFER",
                .memory_object_id = memory_handle,
                .size = suballocation_size,
                .offset = suballocation_offset,
                .object_handle = resource_handle,
                .memory_type = cluster_name,
            });
            break;
        }
    }
}

void DeviceMemoryReport::BindResourceMemory(uint64_t resource_handle, uint64_t memory_handle, VkDeviceSize memory_offset) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto res_it = resources_.find(resource_handle);
    if (res_it == resources_.end() || res_it->second.size == 0) return;

    // If the same resource handle is passed more than once, remove stale bindings first.
    RemoveResourceBinding(resource_handle);

    auto& allocation = memory_allocations_[memory_handle];
    VkDeviceSize res_size = res_it->second.size;
    std::string new_usage_track = GetUsageTrackName(allocation.is_driver, res_it->second.GetCluster(allocation.mem_flags));

    // Suballocations represent individual resources (like buffers or images) that are bound 
    // to specific offset regions within a single large memory allocation. 
    // We add a record here to track this specific resource's footprint within the larger memory block.
    allocation.sub_allocations.push_back({ resource_handle, memory_offset, res_size, new_usage_track });
    resource_to_memory_map_[resource_handle] = memory_handle;

    // Each distinct virtual resource handle adds its virtual size to its specific category track upon binding.
    AddCounterBytes(new_usage_track, res_size);
    UpdateAllocationUnboundCounter(memory_handle);

    bool is_image = res_it->second.is_image;
    const char* cluster_name = res_it->second.GetCluster(allocation.mem_flags);
    EmitAllocationTraceEvent({
        .operation = "BIND",
        .source = is_image ? "IMAGE" : "BUFFER",
        .memory_object_id = memory_handle,
        .size = res_size,
        .offset = memory_offset,
        .object_handle = resource_handle,
        .memory_type = cluster_name,
    });
}

void DeviceMemoryReport::OnBindBufferMemory(uint64_t buffer_handle, uint64_t memory_handle, VkDeviceSize memory_offset) {
    BindResourceMemory(buffer_handle, memory_handle, memory_offset);
}

void DeviceMemoryReport::OnBindImageMemory(uint64_t image_handle, uint64_t memory_handle, VkDeviceSize memory_offset) {
    BindResourceMemory(image_handle, memory_handle, memory_offset);
}

void DeviceMemoryReport::RemoveAllocationTracking(uint64_t memory_handle) {
    auto allocation_it = memory_allocations_.find(memory_handle);
    if (allocation_it == memory_allocations_.end()) return;

    auto& allocation = allocation_it->second;
    for (const auto& suballocation : allocation.sub_allocations) {
        SubtractCounterBytes(suballocation.usage_track, suballocation.size);
        auto resource_iterator = resources_.find(suballocation.resource_handle);
        bool is_image = (resource_iterator != resources_.end()) ? resource_iterator->second.is_image : false;
        const char* cluster_name = (resource_iterator != resources_.end())
                                       ? resource_iterator->second.GetCluster(allocation.mem_flags)
                                       : "unbound_memory";
        EmitAllocationTraceEvent({
            .operation = "DESTROY",
            .source = is_image ? "IMAGE" : "BUFFER",
            .memory_object_id = memory_handle,
            .size = suballocation.size,
            .offset = suballocation.offset,
            .object_handle = suballocation.resource_handle,
            .memory_type = cluster_name,
        });
        resource_to_memory_map_.erase(suballocation.resource_handle);
    }
    if (allocation.applied_unbound_bytes > 0) {
        SubtractCounterBytes(allocation.unbound_usage_track, allocation.applied_unbound_bytes);
    }
    memory_allocations_.erase(allocation_it);
}

void DeviceMemoryReport::OnRecordResourceSize(uint64_t resource_handle, VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    resources_[resource_handle].size = size;
}

VkDeviceSize DeviceMemoryReport::GetRecordedResourceSize(uint64_t resource_handle) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto it = resources_.find(resource_handle);
    return it != resources_.end() ? it->second.size : 0;
}

uint64_t DeviceMemoryReport::GetUsageCounterBytes(const std::string& track) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto it = usage_memory_bytes_.find(track);
    return it != usage_memory_bytes_.end() ? it->second : 0;
}

void DeviceMemoryReport::Reset() {
    std::scoped_lock lock(map_mutex_, counter_mutex_);
    vk_instance_map_.clear();
    has_callback_map_.clear();
    device_memory_properties_map_.clear();
    resources_.clear();
    resource_to_memory_map_.clear();
    memory_allocations_.clear();
    usage_memory_bytes_.clear();
    debug_object_names_.clear();
}
void DeviceMemoryReport::OnCreateImage(uint64_t image_handle, VkImageUsageFlags usage) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto& res = resources_[image_handle];
    res.is_image = true;
    res.image_usage = usage;
    for (const auto& pair : memory_allocations_) {
        if (pair.second.is_driver && pair.second.object_type == VK_OBJECT_TYPE_IMAGE && pair.second.object_handle == image_handle) {
            UpdateAllocationUnboundCounter(pair.first);
        }
    }
}

void DeviceMemoryReport::OnCreateBuffer(uint64_t buffer_handle, VkBufferUsageFlags usage, VkDeviceSize size) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    auto& res = resources_[buffer_handle];
    res.is_image = false;
    res.buffer_usage = usage;
    res.size = size;
    for (const auto& pair : memory_allocations_) {
        if (pair.second.is_driver && pair.second.object_type == VK_OBJECT_TYPE_BUFFER && pair.second.object_handle == buffer_handle) {
            UpdateAllocationUnboundCounter(pair.first);
        }
    }
}

void DeviceMemoryReport::OnDestroyObject(uint64_t object_handle, VkObjectType object_type) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    RemoveResourceBinding(object_handle);
    resources_.erase(object_handle);
    debug_object_names_.erase(std::make_pair(object_type, object_handle));
}

void DeviceMemoryReport::SetDebugObjectName(VkObjectType object_type, uint64_t object_handle, const char* name) {
    // Other types would be trace volume that nothing reads; VK_LAYER_GOOGLE_DebugMarker names them
    // all for consumers that need it.
    switch (object_type) {
        case VK_OBJECT_TYPE_BUFFER:
        case VK_OBJECT_TYPE_IMAGE:
        case VK_OBJECT_TYPE_DEVICE_MEMORY:
            break;
        default:
            return;
    }

    // A null or empty name clears the name, and the clear still has to be published. A view, not a
    // string: the overwhelmingly common call is an application re-applying a name that has not
    // changed, some do it every frame, and that path must not allocate.
    const std::string_view new_name = name ? std::string_view(name) : std::string_view();
    const auto key = std::make_pair(object_type, object_handle);

    std::lock_guard<std::mutex> lock(counter_mutex_);

    auto existing = debug_object_names_.find(key);
    if (existing == debug_object_names_.end()) {
        // Nothing stored and nothing to store: no state change to publish.
        if (new_name.empty()) return;
        debug_object_names_.emplace(key, new_name);
    } else if (existing->second == new_name) {
        // Applications re-apply the same name routinely, so only a change is worth publishing.
        return;
    } else if (new_name.empty()) {
        debug_object_names_.erase(existing);
    } else {
        // Assigning in place reuses the capacity already allocated for the previous name.
        existing->second.assign(new_name);
    }
    EmitDebugObjectName(object_type, object_handle, new_name);
}

void DeviceMemoryReport::SetDebugObjectName(VkDebugReportObjectTypeEXT object_type, uint64_t object_handle, const char* name) {
    // VK_EXT_debug_marker names objects with the legacy VkDebugReportObjectTypeEXT enum. Only the
    // types this layer attributes memory to are mapped.
    VkObjectType mapped_type = VK_OBJECT_TYPE_UNKNOWN;
    switch (object_type) {
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT:
            mapped_type = VK_OBJECT_TYPE_BUFFER;
            break;
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT:
            mapped_type = VK_OBJECT_TYPE_IMAGE;
            break;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT:
            mapped_type = VK_OBJECT_TYPE_DEVICE_MEMORY;
            break;
        default:
            return;
    }
    SetDebugObjectName(mapped_type, object_handle, name);
}

void DeviceMemoryReport::EmitAllDebugObjectNames() {
    for (const auto& [key, name] : debug_object_names_) {
        const auto& [object_type, object_handle] = key;
        EmitDebugObjectName(object_type, object_handle, name);
    }
}

void DeviceMemoryReport::DumpCurrentCountersAndAllocations() {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    for (const auto& pair : usage_memory_bytes_) {
        if (pair.second > 0) {
            TRACE_COUNTER("VulkanDeviceMemoryReport", GetCounterTrack(pair.first), pair.second);
        }
    }

    for (const auto& pair : memory_allocations_) {
        uint64_t memory_handle = pair.first;
        const auto& allocation = pair.second;
        if (allocation.total_size == 0) continue;

        const char* memory_type = allocation.is_driver ? allocation.cluster_name : "unbound_memory";
        EmitAllocationTraceEvent({
            .operation = "CREATE",
            .source = allocation.is_driver ? "DRIVER" : "DEVICE_MEMORY",
            .memory_object_id = memory_handle,
            .size = allocation.total_size,
            .offset = 0,
            .object_handle = allocation.object_handle,
            .memory_type = memory_type,
        });

        for (const auto& suballocation : allocation.sub_allocations) {
            auto resource_iterator = resources_.find(suballocation.resource_handle);
            bool is_image = (resource_iterator != resources_.end()) ? resource_iterator->second.is_image : false;
            const char* cluster_name = (resource_iterator != resources_.end())
                                           ? resource_iterator->second.GetCluster(allocation.mem_flags)
                                           : "unbound_memory";

            EmitAllocationTraceEvent({
                .operation = "BIND",
                .source = is_image ? "IMAGE" : "BUFFER",
                .memory_object_id = memory_handle,
                .size = suballocation.size,
                .offset = suballocation.offset,
                .object_handle = suballocation.resource_handle,
                .memory_type = cluster_name,
            });
        }
    }

    // Names are replayed after the allocations for the same reason they are replayed at all: a
    // session that attaches mid-run never saw the naming calls, and a name with no allocation to
    // attach to is meaningless.
    EmitAllDebugObjectNames();
}

void DeviceMemoryReport::OnMemoryReportEvent(const VkDeviceMemoryReportCallbackDataEXT* pCallbackData) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    // For internal driver allocations, a single object (e.g. VkImage) might have multiple distinct memory allocations.
    // We must use memoryObjectId as the key so each allocation is tracked separately and can be individually freed.
    // For device memory allocations, objectHandle is the VkDeviceMemory handle, which we use as the key for compatibility.
    bool is_driver = (pCallbackData->flags & VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT) != 0;
    uint64_t key = (!is_driver && pCallbackData->objectType == VK_OBJECT_TYPE_DEVICE_MEMORY)
                       ? pCallbackData->objectHandle
                       : pCallbackData->memoryObjectId;

    const char* operation_name = nullptr;
    const char* memory_type = "unbound_memory";
    VkDeviceSize event_size = pCallbackData->size;
    if (pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT ||
        pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT) {
        auto& allocation = memory_allocations_[key];
        allocation.total_size = pCallbackData->size;
        allocation.is_driver = is_driver;
        allocation.object_type = pCallbackData->objectType;
        allocation.object_handle = pCallbackData->objectHandle;
        UpdateAllocationUnboundCounter(key);
        operation_name = "CREATE";
        memory_type = is_driver ? allocation.cluster_name : "unbound_memory";
    } else if (pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT ||
               pCallbackData->type == VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT) {
        auto allocation_iterator = memory_allocations_.find(key);
        if (allocation_iterator == memory_allocations_.end()) return;

        memory_type = is_driver ? allocation_iterator->second.cluster_name : "unbound_memory";
        event_size = allocation_iterator->second.total_size;
        RemoveAllocationTracking(key);
        operation_name = "DESTROY";
    }

    if (operation_name != nullptr) {
        EmitAllocationTraceEvent({
            .operation = operation_name,
            .source = is_driver ? "DRIVER" : "DEVICE_MEMORY",
            .memory_object_id = key,
            .size = event_size,
            .offset = 0,
            .object_handle = pCallbackData->objectHandle,
            .memory_type = memory_type,
        });
    }
}

void DeviceMemoryReport::SetHasMemoryReportCallback(VkDevice device, bool has_callback) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    has_callback_map_[device] = has_callback;
}

void DeviceMemoryReport::OnAllocateMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, uint32_t memory_type_index, VkMemoryPropertyFlags property_flags) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    uint64_t handle = reinterpret_cast<uint64_t>(memory);
    auto& allocation = memory_allocations_[handle];

    VkMemoryPropertyFlags mem_flags = property_flags;
    if (mem_flags == 0 && memory_type_index != UINT32_MAX) {
        auto prop_it = device_memory_properties_map_.find(device);
        if (prop_it != device_memory_properties_map_.end() && memory_type_index < prop_it->second.memoryTypeCount) {
            mem_flags = prop_it->second.memoryTypes[memory_type_index].propertyFlags;
        }
    }
    bool mem_flags_changed = (allocation.mem_flags != mem_flags);
    allocation.mem_flags = mem_flags;

    if (has_callback_map_[device]) {
        if (mem_flags_changed && allocation.total_size > 0) {
            UpdateAllocationUnboundCounter(handle);
        }
    } else {
        allocation.total_size = size;
        allocation.is_driver = false;
        allocation.object_type = VK_OBJECT_TYPE_DEVICE_MEMORY;
        allocation.object_handle = handle;
        UpdateAllocationUnboundCounter(handle);

        EmitAllocationTraceEvent({
            .operation = "CREATE",
            .source = "DEVICE_MEMORY",
            .memory_object_id = handle,
            .size = size,
            .offset = 0,
            .object_handle = handle,
            .memory_type = "unbound_memory",
        });
    }
}

void DeviceMemoryReport::OnFreeMemory(VkDevice device, VkDeviceMemory memory) {
    std::lock_guard<std::mutex> lock(counter_mutex_);
    uint64_t handle = reinterpret_cast<uint64_t>(memory);
    debug_object_names_.erase(std::make_pair(VK_OBJECT_TYPE_DEVICE_MEMORY, handle));
    if (has_callback_map_[device]) return;
    auto allocation_iterator = memory_allocations_.find(handle);
    if (allocation_iterator == memory_allocations_.end()) return;

    VkDeviceSize freed_size = allocation_iterator->second.total_size;
    RemoveAllocationTracking(handle);

    EmitAllocationTraceEvent({
        .operation = "DESTROY",
        .source = "DEVICE_MEMORY",
        .memory_object_id = handle,
        .size = freed_size,
        .offset = 0,
        .object_handle = handle,
        .memory_type = "unbound_memory",
    });
}
