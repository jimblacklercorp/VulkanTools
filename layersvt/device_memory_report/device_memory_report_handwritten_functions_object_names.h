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

// Object naming intercepts for the DeviceMemoryReport layer.
//
// Memory counters are only actionable if the buffers and images behind them can be identified, so
// this layer publishes the object names the application sets. The bookkeeping itself (name storage,
// mapping of the legacy VK_EXT_debug_marker object types, and replaying known names when a tracing
// session starts) is shared with the DebugMarker layer through layersvt::VulkanObjectNames, so the
// two layers cannot drift apart on what a name is or when it changes.
//
// What they do not share is how a name reaches the trace. DebugMarker writes
// VulkanApiEvent.VkDebugUtilsObjectName packets, which trace_processor reads back only for render
// passes, render targets and command buffers; a name on a buffer or image in that form is parsed
// and then never surfaced. This layer therefore emits "VulkanObjectName" instant events instead,
// which land in the slice table and can be joined to the memory events by object handle. See
// EmitVulkanObjectName in device_memory_report_perfetto.cpp.
//
// Because the two layers emit different events, loading both does not duplicate either one.
//
// Unlike the DebugMarker layer, this layer does not claim VK_EXT_debug_utils or
// VK_EXT_debug_marker. It only observes these entry points when the underlying driver already
// implements them, which vkGetDeviceProcAddr / vkGetInstanceProcAddr verify before handing out
// these interceptors.

extern "C" {

// Observes VK_EXT_debug_utils object names, then passes the call down the chain.
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo) {
    layersvt::VulkanObjectNames::Get().SetObjectName(device, pNameInfo);
    if (device_dispatch_table(device)->SetDebugUtilsObjectNameEXT) {
        return device_dispatch_table(device)->SetDebugUtilsObjectNameEXT(device, pNameInfo);
    }
    return VK_SUCCESS;
}

// Observes legacy VK_EXT_debug_marker object names, then passes the call down the chain.
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* pNameInfo) {
    layersvt::VulkanObjectNames::Get().SetObjectName(device, pNameInfo);
    if (device_dispatch_table(device)->DebugMarkerSetObjectNameEXT) {
        return device_dispatch_table(device)->DebugMarkerSetObjectNameEXT(device, pNameInfo);
    }
    return VK_SUCCESS;
}

}  // extern "C"
