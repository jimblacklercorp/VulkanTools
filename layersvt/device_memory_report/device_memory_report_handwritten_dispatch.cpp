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

#include "device_memory_report_handwritten_functions.h"
#include "vk_layer_table.h"
#include <string.h>

extern "C" {

static PFN_vkVoidFunction devmemreport_known_global_functions(const char* pName) {
    if (strcmp(pName, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    if (strcmp(pName, "vkCreateInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceExtensionProperties);
    if (strcmp(pName, "vkEnumerateInstanceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateInstanceLayerProperties);
    return nullptr;
}

static PFN_vkVoidFunction devmemreport_known_instance_functions(const char* pName) {
    if (strcmp(pName, "vkDestroyInstance") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyInstance);
    if (strcmp(pName, "vkEnumeratePhysicalDevices") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDevices);
    if (strcmp(pName, "vkEnumeratePhysicalDeviceGroups") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumeratePhysicalDeviceGroups);
    if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
    if (strcmp(pName, "vkCreateDebugUtilsMessengerEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDebugUtilsMessengerEXT);
    if (strcmp(pName, "vkDestroyDebugUtilsMessengerEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDebugUtilsMessengerEXT);
    if (strcmp(pName, "vkSubmitDebugUtilsMessageEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSubmitDebugUtilsMessageEXT);
    return nullptr;
}

static PFN_vkVoidFunction devmemreport_known_core_device_functions(const char* pName) {
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    if (strcmp(pName, "vkCreateDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    if (strcmp(pName, "vkDestroyDevice") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyDevice);
    if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
    if (strcmp(pName, "vkAllocateMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkAllocateMemory);
    if (strcmp(pName, "vkFreeMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkFreeMemory);
    if (strcmp(pName, "vkBindBufferMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkBindBufferMemory);
    if (strcmp(pName, "vkBindImageMemory") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkBindImageMemory);
    if (strcmp(pName, "vkBindBufferMemory2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkBindBufferMemory2);
    if (strcmp(pName, "vkBindImageMemory2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkBindImageMemory2);
    if (strcmp(pName, "vkCreateImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateImage);
    if (strcmp(pName, "vkDestroyImage") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyImage);
    if (strcmp(pName, "vkCreateBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCreateBuffer);
    if (strcmp(pName, "vkDestroyBuffer") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyBuffer);
    if (strcmp(pName, "vkGetImageMemoryRequirements") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetImageMemoryRequirements);
    if (strcmp(pName, "vkGetImageMemoryRequirements2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetImageMemoryRequirements2);
    if (strcmp(pName, "vkGetBufferMemoryRequirements") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetBufferMemoryRequirements);
    if (strcmp(pName, "vkGetBufferMemoryRequirements2") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetBufferMemoryRequirements2);
    return nullptr;
}

// Device functions for extensions provided directly by this layer (VK_EXT_debug_utils and
// VK_EXT_debug_marker). These are returned unconditionally so the layer functions standalone
// even when neither the underlying driver nor VK_LAYER_GOOGLE_DebugMarker implements them.
static PFN_vkVoidFunction devmemreport_known_layer_device_extension_functions(const char* pName) {
    // VK_EXT_debug_utils
    if (strcmp(pName, "vkSetDebugUtilsObjectNameEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT);
    if (strcmp(pName, "vkSetDebugUtilsObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectTagEXT);
    if (strcmp(pName, "vkCmdBeginDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginDebugUtilsLabelEXT);
    if (strcmp(pName, "vkCmdEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndDebugUtilsLabelEXT);
    if (strcmp(pName, "vkCmdInsertDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdInsertDebugUtilsLabelEXT);
    if (strcmp(pName, "vkQueueBeginDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueBeginDebugUtilsLabelEXT);
    if (strcmp(pName, "vkQueueEndDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueEndDebugUtilsLabelEXT);
    if (strcmp(pName, "vkQueueInsertDebugUtilsLabelEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkQueueInsertDebugUtilsLabelEXT);

    // VK_EXT_debug_marker
    if (strcmp(pName, "vkDebugMarkerSetObjectNameEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectNameEXT);
    if (strcmp(pName, "vkDebugMarkerSetObjectTagEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectTagEXT);
    if (strcmp(pName, "vkCmdDebugMarkerBeginEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerBeginEXT);
    if (strcmp(pName, "vkCmdDebugMarkerEndEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerEndEXT);
    if (strcmp(pName, "vkCmdDebugMarkerInsertEXT") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDebugMarkerInsertEXT);
    return nullptr;
}

static PFN_vkVoidFunction devmemreport_known_device_extension_functions(const char* pName) {
    if (strcmp(pName, "vkBindBufferMemory2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkBindBufferMemory2KHR);
    if (strcmp(pName, "vkBindImageMemory2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkBindImageMemory2KHR);
    if (strcmp(pName, "vkGetImageMemoryRequirements2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetImageMemoryRequirements2KHR);
    if (strcmp(pName, "vkGetBufferMemoryRequirements2KHR") == 0) return reinterpret_cast<PFN_vkVoidFunction>(vkGetBufferMemoryRequirements2KHR);
    return nullptr;
}

static PFN_vkVoidFunction devmemreport_known_device_functions(const char* pName) {
    PFN_vkVoidFunction func = devmemreport_known_core_device_functions(pName);
    if (func) {
        return func;
    }
    return devmemreport_known_device_extension_functions(pName);
}

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    PFN_vkVoidFunction func = devmemreport_known_global_functions(pName);
    if (func) {
        return func;
    }

    if (instance == nullptr) {
        if (strcmp(pName, "vkEnumerateDeviceExtensionProperties") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceExtensionProperties);
        }
        if (strcmp(pName, "vkEnumerateDeviceLayerProperties") == 0) {
            return reinterpret_cast<PFN_vkVoidFunction>(vkEnumerateDeviceLayerProperties);
        }
        return nullptr;
    }

    func = devmemreport_known_instance_functions(pName);
    if (func) {
        return func;
    }
    
    // Core device functions and layer-provided debug extension functions can be returned directly from GIPA.
    func = devmemreport_known_core_device_functions(pName);
    if (func) {
        return func;
    }
    func = devmemreport_known_layer_device_extension_functions(pName);
    if (func) {
        return func;
    }

    auto table = instance_dispatch_table(instance);
    if (table == NULL || table->GetInstanceProcAddr == NULL) {
        return nullptr;
    }

    // For driver-dependent extension device commands, verify the underlying chain supports them before returning an interceptor.
    PFN_vkVoidFunction down_func = table->GetInstanceProcAddr(instance, pName);
    if (down_func == nullptr) {
        return nullptr;
    }

    func = devmemreport_known_device_extension_functions(pName);
    if (func) {
        return func;
    }

    return down_func;
}

EXPORT_FUNCTION VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (device == nullptr || pName == nullptr) {
        return nullptr;
    }

    // Extensions provided by this layer itself are always available regardless of underlying driver support.
    PFN_vkVoidFunction layer_ext_func = devmemreport_known_layer_device_extension_functions(pName);
    if (layer_ext_func) {
        return layer_ext_func;
    }

    if (device_dispatch_table(device)->GetDeviceProcAddr == NULL) {
        return nullptr;
    }
    
    // We must verify that the underlying device actually supports the function.
    // Returning an interceptor for an unsupported function violates the Vulkan spec and can cause
    // applications to erroneously think an extension is supported, leading to crashes when called.
    PFN_vkVoidFunction down_func = device_dispatch_table(device)->GetDeviceProcAddr(device, pName);
    if (down_func == nullptr) {
        return nullptr;
    }

    // Only return the intercepted function if the device supports the command.
    PFN_vkVoidFunction func = devmemreport_known_device_functions(pName);
    if (func) {
        return func;
    }

    return down_func;
}

} // extern "C"
