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

extern "C" {

// This file contains handwritten functions for the VK_EXT_debug_utils extension.
// We only actively implement vkSetDebugUtilsObjectNameEXT to track object names for Perfetto traces.
// All other functions are simple passthroughs required to be provided so that the layer
// can claim full support for the extension.

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(commandBuffer)->CmdBeginDebugUtilsLabelEXT) {
        device_dispatch_table(commandBuffer)->CmdBeginDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
    }
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer) {
    if (device_dispatch_table(commandBuffer)->CmdEndDebugUtilsLabelEXT) {
        device_dispatch_table(commandBuffer)->CmdEndDebugUtilsLabelEXT(commandBuffer);
    }
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(commandBuffer)->CmdInsertDebugUtilsLabelEXT) {
        device_dispatch_table(commandBuffer)->CmdInsertDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
    }
}

// Required for VK_EXT_debug_utils. Tracks object name state.
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo) {
    layersvt::VulkanObjectNames::Get().SetObjectName(device, pNameInfo);
    if (device_dispatch_table(device)->SetDebugUtilsObjectNameEXT) {
        VkResult result = device_dispatch_table(device)->SetDebugUtilsObjectNameEXT(device, pNameInfo);
        return result;
    }
    return VK_SUCCESS;
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectTagEXT(VkDevice device, const VkDebugUtilsObjectTagInfoEXT* pTagInfo) {
    if (device_dispatch_table(device)->SetDebugUtilsObjectTagEXT) {
        return device_dispatch_table(device)->SetDebugUtilsObjectTagEXT(device, pTagInfo);
    }
    return VK_SUCCESS;
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkQueueBeginDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(queue)->QueueBeginDebugUtilsLabelEXT) {
        device_dispatch_table(queue)->QueueBeginDebugUtilsLabelEXT(queue, pLabelInfo);
    }
}

// Required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkQueueEndDebugUtilsLabelEXT(VkQueue queue) {
    if (device_dispatch_table(queue)->QueueEndDebugUtilsLabelEXT) {
        device_dispatch_table(queue)->QueueEndDebugUtilsLabelEXT(queue);
    }
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkQueueInsertDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(queue)->QueueInsertDebugUtilsLabelEXT) {
        device_dispatch_table(queue)->QueueInsertDebugUtilsLabelEXT(queue, pLabelInfo);
    }
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
    if (instance_dispatch_table(instance)->CreateDebugUtilsMessengerEXT) {
        return instance_dispatch_table(instance)->CreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
    }
    return VK_SUCCESS;
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger, const VkAllocationCallbacks* pAllocator) {
    if (instance_dispatch_table(instance)->DestroyDebugUtilsMessengerEXT) {
        instance_dispatch_table(instance)->DestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
    }
}

// Passthrough required for VK_EXT_debug_utils
VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    if (instance_dispatch_table(instance)->SubmitDebugUtilsMessageEXT) {
        instance_dispatch_table(instance)->SubmitDebugUtilsMessageEXT(instance, messageSeverity, messageTypes, pCallbackData);
    }
}

} // extern "C"
