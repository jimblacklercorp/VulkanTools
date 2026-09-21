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

#pragma once

#include <vulkan/vulkan.h>
#include "vk_layer_table.h"
#include "object_names/vulkan_object_names.h"

// This file contains handwritten functions for the VK_EXT_debug_marker extension.
// We only actively implement vkDebugMarkerSetObjectNameEXT to track object names for Perfetto traces.
// All other functions are simple passthroughs required to be provided so that the layer
// can claim full support for the extension.
//
// VK_EXT_debug_marker identifies objects with the legacy VkDebugReportObjectTypeEXT enum, while we
// store everything using the modern VkObjectType enum to be consistent with VK_EXT_debug_utils and
// standard trace packets. layersvt::VulkanObjectNames performs that remapping for us.

extern "C" {

// Required for VK_EXT_debug_marker
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    if (device_dispatch_table(commandBuffer)->CmdDebugMarkerBeginEXT) {
        device_dispatch_table(commandBuffer)->CmdDebugMarkerBeginEXT(commandBuffer, pMarkerInfo);
    }
}

// Required for VK_EXT_debug_marker
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer) {
    if (device_dispatch_table(commandBuffer)->CmdDebugMarkerEndEXT) {
        device_dispatch_table(commandBuffer)->CmdDebugMarkerEndEXT(commandBuffer);
    }
}

// Required for VK_EXT_debug_marker
VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    if (device_dispatch_table(commandBuffer)->CmdDebugMarkerInsertEXT) {
        device_dispatch_table(commandBuffer)->CmdDebugMarkerInsertEXT(commandBuffer, pMarkerInfo);
    }
}

// Required for VK_EXT_debug_marker. Tracks object name state.
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* pNameInfo) {
    layersvt::VulkanObjectNames::Get().SetObjectName(device, pNameInfo);
    if (device_dispatch_table(device)->DebugMarkerSetObjectNameEXT) {
        VkResult result = device_dispatch_table(device)->DebugMarkerSetObjectNameEXT(device, pNameInfo);
        return result;
    }
    return VK_SUCCESS;
}

// Required for VK_EXT_debug_marker
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(VkDevice device, const VkDebugMarkerObjectTagInfoEXT* pTagInfo) {
    if (device_dispatch_table(device)->DebugMarkerSetObjectTagEXT) {
        return device_dispatch_table(device)->DebugMarkerSetObjectTagEXT(device, pTagInfo);
    }
    return VK_SUCCESS;
}

} // extern "C"
