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

#include "vulkan_object_names.h"

namespace layersvt {

VkObjectType VkObjectTypeFromDebugReportObjectType(VkDebugReportObjectTypeEXT debug_report_object_type) {
    switch (debug_report_object_type) {
        case VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT: return VK_OBJECT_TYPE_UNKNOWN;
        case VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT: return VK_OBJECT_TYPE_INSTANCE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PHYSICAL_DEVICE_EXT: return VK_OBJECT_TYPE_PHYSICAL_DEVICE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT: return VK_OBJECT_TYPE_DEVICE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_QUEUE_EXT: return VK_OBJECT_TYPE_QUEUE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SEMAPHORE_EXT: return VK_OBJECT_TYPE_SEMAPHORE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT: return VK_OBJECT_TYPE_COMMAND_BUFFER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_FENCE_EXT: return VK_OBJECT_TYPE_FENCE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT: return VK_OBJECT_TYPE_DEVICE_MEMORY;
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT: return VK_OBJECT_TYPE_BUFFER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT: return VK_OBJECT_TYPE_IMAGE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_EVENT_EXT: return VK_OBJECT_TYPE_EVENT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_QUERY_POOL_EXT: return VK_OBJECT_TYPE_QUERY_POOL;
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_VIEW_EXT: return VK_OBJECT_TYPE_BUFFER_VIEW;
        case VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_VIEW_EXT: return VK_OBJECT_TYPE_IMAGE_VIEW;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SHADER_MODULE_EXT: return VK_OBJECT_TYPE_SHADER_MODULE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_CACHE_EXT: return VK_OBJECT_TYPE_PIPELINE_CACHE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_LAYOUT_EXT: return VK_OBJECT_TYPE_PIPELINE_LAYOUT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_RENDER_PASS_EXT: return VK_OBJECT_TYPE_RENDER_PASS;
        case VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT: return VK_OBJECT_TYPE_PIPELINE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_EXT: return VK_OBJECT_TYPE_SAMPLER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_POOL_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_POOL;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_SET_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_SET;
        case VK_DEBUG_REPORT_OBJECT_TYPE_FRAMEBUFFER_EXT: return VK_OBJECT_TYPE_FRAMEBUFFER;
        case VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_POOL_EXT: return VK_OBJECT_TYPE_COMMAND_POOL;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SURFACE_KHR_EXT: return VK_OBJECT_TYPE_SURFACE_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT: return VK_OBJECT_TYPE_SWAPCHAIN_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT_EXT: return VK_OBJECT_TYPE_DEBUG_REPORT_CALLBACK_EXT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_KHR_EXT: return VK_OBJECT_TYPE_DISPLAY_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DISPLAY_MODE_KHR_EXT: return VK_OBJECT_TYPE_DISPLAY_MODE_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_VALIDATION_CACHE_EXT_EXT: return VK_OBJECT_TYPE_VALIDATION_CACHE_EXT;
        case VK_DEBUG_REPORT_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION_EXT: return VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION;
        case VK_DEBUG_REPORT_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_EXT: return VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE;
        case VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV_EXT: return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_NV;
        case VK_DEBUG_REPORT_OBJECT_TYPE_CU_MODULE_NVX_EXT: return VK_OBJECT_TYPE_CU_MODULE_NVX;
        case VK_DEBUG_REPORT_OBJECT_TYPE_CU_FUNCTION_NVX_EXT: return VK_OBJECT_TYPE_CU_FUNCTION_NVX;
        case VK_DEBUG_REPORT_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR_EXT: return VK_OBJECT_TYPE_ACCELERATION_STRUCTURE_KHR;
        case VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA_EXT: return VK_OBJECT_TYPE_BUFFER_COLLECTION_FUCHSIA;
        default:
            return VK_OBJECT_TYPE_UNKNOWN;
    }
}

VulkanObjectNames& VulkanObjectNames::Get() {
    static VulkanObjectNames instance;
    return instance;
}

void VulkanObjectNames::SetObjectName(uint64_t vk_device, int32_t object_type, uint64_t handle, const char* name) {
    std::lock_guard<std::mutex> lock(mutex_);

    VulkanObjectName& object_name = object_names_[std::make_pair(object_type, handle)];
    object_name.vk_device = vk_device;
    object_name.object_type = object_type;
    object_name.handle = handle;
    object_name.name = name ? name : "NULL";

    EmitVulkanObjectName(object_name);
}

void VulkanObjectNames::SetObjectName(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* name_info) {
    if (name_info == nullptr) {
        return;
    }
    SetObjectName((uint64_t)device, (int32_t)name_info->objectType, name_info->objectHandle, name_info->pObjectName);
}

void VulkanObjectNames::SetObjectName(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* name_info) {
    if (name_info == nullptr) {
        return;
    }
    SetObjectName((uint64_t)device, (int32_t)VkObjectTypeFromDebugReportObjectType(name_info->objectType), name_info->object,
                  name_info->pObjectName);
}

void VulkanObjectNames::EmitAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : object_names_) {
        EmitVulkanObjectName(entry.second);
    }
}

void VulkanObjectNames::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    object_names_.clear();
}

bool VulkanObjectNames::HasObjectName(int32_t object_type, uint64_t handle, const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = object_names_.find(std::make_pair(object_type, handle));
    if (it == object_names_.end()) return false;
    return it->second.name == name;
}

std::string VulkanObjectNames::GetObjectName(int32_t object_type, uint64_t handle) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = object_names_.find(std::make_pair(object_type, handle));
    if (it == object_names_.end()) return std::string();
    return it->second.name;
}

size_t VulkanObjectNames::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return object_names_.size();
}

}  // namespace layersvt
