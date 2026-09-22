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
#include <mutex>
#include <vector>
#include <assert.h>
#include <string.h>
#include "vk_layer_table.h"
#include "device_memory_report.h"
#include "device_memory_report_perfetto.h"

// This file contains handwritten implementations for Vulkan functions intercepted by
// the VK_LAYER_GOOGLE_DeviceMemoryReport layer:
//
// Core infrastructure & lifecycle:
// - vkCreateInstance: Initializes Perfetto tracing and the instance dispatch table.
// - vkEnumeratePhysicalDevices / vkEnumeratePhysicalDeviceGroups: Tracks the mapping
//   between physical devices and instances to support dispatch table lookups.
// - vkCreateDevice / vkDestroyDevice: Initializes/destroys device dispatch tables, strips
//   layer-advertised device extensions (VK_EXT_device_memory_report, VK_EXT_debug_marker) when
//   unsupported by the underlying driver, and injects VK_EXT_device_memory_report callback
//   registration into device creation.
//
// Memory tracking, resource tracking & debug naming intercepts:
// - vkAllocateMemory / vkFreeMemory: Tracks direct allocations/frees as fallbacks.
// - vkBindBufferMemory* / vkBindImageMemory*: Associates buffer/image handles with memory allocations.
// - vkCreateBuffer / vkDestroyBuffer: Tracks buffer creation, usage flags, and requested sizes.
// - vkCreateImage / vkDestroyImage: Tracks image creation and usage flags.
// - vkGetBufferMemoryRequirements* / vkGetImageMemoryRequirements*: Tracks resource memory requirements.
// - vkSetDebugUtilsObjectNameEXT / vkDebugMarkerSetObjectNameEXT (and companion passthroughs):
//   Records debug object names for buffers, images, and device memory so the layer can operate
//   standalone without VK_LAYER_GOOGLE_DebugMarker.
// - vkEnumerate*ExtensionProperties / vkEnumerate*LayerProperties: Advertises the layer and
//   support for VK_EXT_device_memory_report, VK_EXT_debug_utils, and VK_EXT_debug_marker.

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#if defined(__GNUC__) && __GNUC__ >= 4
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#elif defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
#define EXPORT_FUNCTION __attribute__((visibility("default")))
#else
#define EXPORT_FUNCTION
#endif

#define LAYER_NAME "VK_LAYER_GOOGLE_DeviceMemoryReport"
#define LAYER_DESCRIPTION "Vulkan Device Memory Report Layer"

extern "C" {

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                                VkInstance* pInstance) {
    InitializeDeviceMemoryReportPerfetto();

    // Get the function pointer
    VkLayerInstanceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    assert(fpGetInstanceProcAddr != 0);
    PFN_vkCreateInstance fpCreateInstance = (PFN_vkCreateInstance)fpGetInstanceProcAddr(NULL, "vkCreateInstance");
    if (fpCreateInstance == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;
    VkResult result = fpCreateInstance(pCreateInfo, pAllocator, pInstance);
    if (result == VK_SUCCESS) {
        initInstanceTable(*pInstance, fpGetInstanceProcAddr);
    }

    return result;
}

// Intercept physical device enumeration to store physical device to instance mapping.
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice* pPhysicalDevices) {
    
    VkResult result = instance_dispatch_table(instance)->EnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices);
    
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDeviceCount != nullptr && pPhysicalDevices != nullptr) {
        for (uint32_t i = 0; i < *pPhysicalDeviceCount; ++i) {
            DeviceMemoryReport::Get().SetVkInstance(pPhysicalDevices[i], instance);
        }
    }
    return result;
}

// Intercept physical device group enumeration to store physical device to instance mapping.
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDeviceGroups(VkInstance instance, uint32_t* pPhysicalDeviceGroupCount, VkPhysicalDeviceGroupProperties* pPhysicalDeviceGroupProperties) {
    
    VkResult result = instance_dispatch_table(instance)->EnumeratePhysicalDeviceGroups(instance, pPhysicalDeviceGroupCount, pPhysicalDeviceGroupProperties);
    
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && pPhysicalDeviceGroupCount != nullptr && pPhysicalDeviceGroupProperties != nullptr) {
        for (uint32_t i = 0; i < *pPhysicalDeviceGroupCount; ++i) {
            for (uint32_t j = 0; j < pPhysicalDeviceGroupProperties[i].physicalDeviceCount; ++j) {
                DeviceMemoryReport::Get().SetVkInstance(pPhysicalDeviceGroupProperties[i].physicalDevices[j], instance);
            }
        }
    }
    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
    dispatch_key key = get_dispatch_key(instance);
    instance_dispatch_table(instance)->DestroyInstance(instance, pAllocator);
    destroy_instance_dispatch_table(key);
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                              const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
    InitializeDeviceMemoryReportPerfetto();

    // Get the function pointer
    VkLayerDeviceCreateInfo* chain_info = get_chain_info(pCreateInfo, VK_LAYER_LINK_INFO);
    assert(chain_info->u.pLayerInfo != 0);
    PFN_vkGetInstanceProcAddr fpGetInstanceProcAddr = chain_info->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr fpGetDeviceProcAddr = chain_info->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    VkInstance vk_instance = DeviceMemoryReport::Get().GetVkInstance(physicalDevice);
    PFN_vkCreateDevice fpCreateDevice = (PFN_vkCreateDevice)fpGetInstanceProcAddr(vk_instance, "vkCreateDevice");
    if (fpCreateDevice == NULL) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Call the function and create the dispatch table
    chain_info->u.pLayerInfo = chain_info->u.pLayerInfo->pNext;

    // Check if the underlying driver supports VK_EXT_device_memory_report or VK_EXT_debug_marker.
    bool supports_memory_report = false;
    bool supports_debug_marker = false;
    uint32_t ext_count = 0;
    if (instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties) {
        if (instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &ext_count, nullptr) == VK_SUCCESS && ext_count > 0) {
            std::vector<VkExtensionProperties> exts(ext_count);
            if (instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &ext_count, exts.data()) == VK_SUCCESS) {
                for (const auto& ext : exts) {
                    if (strcmp(ext.extensionName, VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME) == 0) {
                        supports_memory_report = true;
                    } else if (strcmp(ext.extensionName, VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0) {
                        supports_debug_marker = true;
                    }
                }
            }
        }
    }

    // Strip layer-advertised device extensions if the underlying driver does not natively support
    // them, and inject VK_EXT_device_memory_report callback registration when supported.
    VkDeviceCreateInfo modified_create_info = *pCreateInfo;
    std::vector<const char*> enabled_extensions;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* name = pCreateInfo->ppEnabledExtensionNames[i];
        if (!supports_debug_marker && strcmp(name, VK_EXT_DEBUG_MARKER_EXTENSION_NAME) == 0) {
            continue;
        }
        if (!supports_memory_report && strcmp(name, VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME) == 0) {
            continue;
        }
        enabled_extensions.push_back(name);
    }

    VkDeviceDeviceMemoryReportCreateInfoEXT memory_report_ci = {};
    if (supports_memory_report) {
        bool already_enabled = false;
        for (const char* name : enabled_extensions) {
            if (strcmp(name, VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME) == 0) {
                already_enabled = true;
                break;
            }
        }
        if (!already_enabled) {
            enabled_extensions.push_back(VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME);
        }

        memory_report_ci.sType = VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT;
        memory_report_ci.pfnUserCallback = DeviceMemoryReport::MemoryReportCallback;
        memory_report_ci.pUserData = nullptr;
        memory_report_ci.pNext = modified_create_info.pNext;
        modified_create_info.pNext = &memory_report_ci;
    }

    modified_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
    modified_create_info.ppEnabledExtensionNames = enabled_extensions.empty() ? nullptr : enabled_extensions.data();

    VkResult result = fpCreateDevice(physicalDevice, &modified_create_info, pAllocator, pDevice);
    if (result == VK_SUCCESS) {
        initDeviceTable(*pDevice, fpGetDeviceProcAddr);
        DeviceMemoryReport::Get().SetHasMemoryReportCallback(*pDevice, supports_memory_report);

        VkPhysicalDeviceMemoryProperties mem_props = {};
        if (instance_dispatch_table(physicalDevice)->GetPhysicalDeviceMemoryProperties) {
            instance_dispatch_table(physicalDevice)->GetPhysicalDeviceMemoryProperties(physicalDevice, &mem_props);
            DeviceMemoryReport::Get().SetDeviceMemoryProperties(*pDevice, mem_props);
        }
    }

    return result;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
    DeviceMemoryReport::Get().OnDestroyDevice(device);
    dispatch_key key = get_dispatch_key(device);
    device_dispatch_table(device)->DestroyDevice(device, pAllocator);
    destroy_device_dispatch_table(key);
}

// Fallback memory allocation tracking used when driver callback is unavailable.
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                                                const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
    PFN_vkAllocateMemory fpAllocateMemory = (PFN_vkAllocateMemory)device_dispatch_table(device)->AllocateMemory;
    VkResult result = fpAllocateMemory(device, pAllocateInfo, pAllocator, pMemory);
    if (result == VK_SUCCESS && pAllocateInfo != nullptr && pMemory != nullptr && *pMemory != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnAllocateMemory(device, *pMemory, pAllocateInfo->allocationSize, pAllocateInfo->memoryTypeIndex);
    }
    return result;
}

// Fallback memory free tracking used when driver callback is unavailable.
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice device, VkDeviceMemory memory, const VkAllocationCallbacks* pAllocator) {
    if (memory != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnFreeMemory(device, memory);
    }
    PFN_vkFreeMemory fpFreeMemory = (PFN_vkFreeMemory)device_dispatch_table(device)->FreeMemory;
    if (fpFreeMemory != NULL) {
        fpFreeMemory(device, memory, pAllocator);
    }
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                                       uint32_t* pPropertyCount,
                                                                                       VkExtensionProperties* pProperties) {
    static const VkExtensionProperties instanceExtensions[] = {
        {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_SPEC_VERSION},
    };

    if (pLayerName != nullptr && strcmp(pLayerName, LAYER_NAME) == 0) {
        return util_GetExtensionProperties(ARRAY_SIZE(instanceExtensions), instanceExtensions, pPropertyCount, pProperties);
    }

    return util_GetExtensionProperties(0, nullptr, pPropertyCount, pProperties);
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                                                  VkLayerProperties* pProperties) {
    static const VkLayerProperties layerProperties[] = {{
        LAYER_NAME,
        VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION),  // specVersion
        VK_MAKE_VERSION(0, 1, 0),                  // implementationVersion
        LAYER_DESCRIPTION,
    }};

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice physicalDevice,
                                                                                uint32_t* pPropertyCount,
                                                                                VkLayerProperties* pProperties) {
    static const VkLayerProperties layerProperties[] = {{
        LAYER_NAME,
        VK_MAKE_VERSION(1, 4, VK_HEADER_VERSION),
        VK_MAKE_VERSION(0, 1, 0),
        LAYER_DESCRIPTION,
    }};

    return util_GetLayerProperties(ARRAY_SIZE(layerProperties), layerProperties, pPropertyCount, pProperties);
}

static VKAPI_ATTR VkResult VKAPI_CALL devmemreport_EnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                      const char* pLayerName,
                                                                                      uint32_t* pPropertyCount,
                                                                                      VkExtensionProperties* pProperties) {
    assert(pPropertyCount != nullptr);

    static const VkExtensionProperties layer_device_extensions[] = {
        {VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME, VK_EXT_DEVICE_MEMORY_REPORT_SPEC_VERSION},
        {VK_EXT_DEBUG_MARKER_EXTENSION_NAME, VK_EXT_DEBUG_MARKER_SPEC_VERSION},
    };

    if (pLayerName != nullptr && strcmp(pLayerName, LAYER_NAME) == 0) {
        return util_GetExtensionProperties(ARRAY_SIZE(layer_device_extensions), layer_device_extensions,
                                           pPropertyCount, pProperties);
    }

    assert(physicalDevice != VK_NULL_HANDLE);

    // Forward queries for other explicit layers downstream unchanged.
    if (pLayerName != nullptr) {
        return instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(
            physicalDevice, pLayerName, pPropertyCount, pProperties);
    }

    // Manually merge device extensions when pLayerName == nullptr because the Android Vulkan
    // loader does not expose device extensions from implicit layers (b/143293104).
    uint32_t downstream_count = 0;
    VkResult result = instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &downstream_count, nullptr);
    if (result != VK_SUCCESS) {
        return result;
    }

    std::vector<VkExtensionProperties> downstream_extensions(downstream_count);
    result = instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &downstream_count, downstream_extensions.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
        return result;
    }

    std::vector<VkExtensionProperties> merged_extensions = std::move(downstream_extensions);
    for (const auto& layer_extension : layer_device_extensions) {
        bool duplicate = false;
        for (const auto& existing : merged_extensions) {
            if (strcmp(layer_extension.extensionName, existing.extensionName) == 0) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            merged_extensions.push_back(layer_extension);
        }
    }

    return util_GetExtensionProperties(static_cast<uint32_t>(merged_extensions.size()),
                                       merged_extensions.data(), pPropertyCount, pProperties);
}

EXPORT_FUNCTION VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice physicalDevice,
                                                                                    const char* pLayerName,
                                                                                    uint32_t* pPropertyCount,
                                                                                    VkExtensionProperties* pProperties) {
    return devmemreport_EnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties);
}


// Intercept memory binding to correlate buffer object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    auto* table = device_dispatch_table(device);
    assert(table->BindBufferMemory != nullptr);
    assert(buffer != VK_NULL_HANDLE);
    assert(memory != VK_NULL_HANDLE);
    if (DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(buffer)) == 0) {
        if (table->GetBufferMemoryRequirements) {
            VkMemoryRequirements mem_reqs;
            table->GetBufferMemoryRequirements(device, buffer, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(buffer), mem_reqs.size);
        }
    }
    VkResult result = table->BindBufferMemory(device, buffer, memory, memoryOffset);
    if (result == VK_SUCCESS) {
        DeviceMemoryReport::Get().OnBindBufferMemory(reinterpret_cast<uint64_t>(buffer), reinterpret_cast<uint64_t>(memory), memoryOffset);
    }
    return result;
}

// Intercept memory binding to correlate image object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    auto* table = device_dispatch_table(device);
    assert(table->BindImageMemory != nullptr);
    assert(image != VK_NULL_HANDLE);
    assert(memory != VK_NULL_HANDLE);
    if (DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(image)) == 0) {
        if (table->GetImageMemoryRequirements) {
            VkMemoryRequirements mem_reqs;
            table->GetImageMemoryRequirements(device, image, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(image), mem_reqs.size);
        }
    }
    VkResult result = table->BindImageMemory(device, image, memory, memoryOffset);
    if (result == VK_SUCCESS) {
        DeviceMemoryReport::Get().OnBindImageMemory(reinterpret_cast<uint64_t>(image), reinterpret_cast<uint64_t>(memory), memoryOffset);
    }
    return result;
}

static void RecordBufferBindings(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    auto* table = device_dispatch_table(device);
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        assert(pBindInfos[i].buffer != VK_NULL_HANDLE);
        assert(pBindInfos[i].memory != VK_NULL_HANDLE);
        if (DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].buffer)) == 0) {
            VkMemoryRequirements mem_reqs;
            table->GetBufferMemoryRequirements(device, pBindInfos[i].buffer, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].buffer), mem_reqs.size);
        }
        DeviceMemoryReport::Get().OnBindBufferMemory(reinterpret_cast<uint64_t>(pBindInfos[i].buffer), reinterpret_cast<uint64_t>(pBindInfos[i].memory), pBindInfos[i].memoryOffset);
    }
}

// Intercept memory binding via vkBindBufferMemory2 to correlate buffer object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    auto* table = device_dispatch_table(device);
    assert(table->BindBufferMemory2 != nullptr);
    VkResult result = table->BindBufferMemory2(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordBufferBindings(device, bindInfoCount, pBindInfos);
    }
    return result;
}

// Intercept memory binding via vkBindBufferMemory2KHR to correlate buffer object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    auto* table = device_dispatch_table(device);
    assert(table->BindBufferMemory2KHR != nullptr);
    VkResult result = table->BindBufferMemory2KHR(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordBufferBindings(device, bindInfoCount, pBindInfos);
    }
    return result;
}

static void RecordImageBinds(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    auto* table = device_dispatch_table(device);
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        assert(pBindInfos[i].image != VK_NULL_HANDLE);
        assert(pBindInfos[i].memory != VK_NULL_HANDLE);
        if (DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].image)) == 0) {
            bool is_plane_bind = false;
            for (const auto* header = reinterpret_cast<const VkBaseInStructure*>(pBindInfos[i].pNext);
                 header != nullptr; header = header->pNext) {
                if (header->sType == VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO) {
                    is_plane_bind = true;
                    break;
                }
            }
            if (!is_plane_bind) {
                VkMemoryRequirements mem_reqs;
                table->GetImageMemoryRequirements(device, pBindInfos[i].image, &mem_reqs);
                DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].image), mem_reqs.size);
            }
        }
        DeviceMemoryReport::Get().OnBindImageMemory(reinterpret_cast<uint64_t>(pBindInfos[i].image), reinterpret_cast<uint64_t>(pBindInfos[i].memory), pBindInfos[i].memoryOffset);
    }
}

// Intercept memory binding via vkBindImageMemory2 to correlate image object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    auto* table = device_dispatch_table(device);
    assert(table->BindImageMemory2 != nullptr);
    VkResult result = table->BindImageMemory2(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordImageBinds(device, bindInfoCount, pBindInfos);
    }
    return result;
}

// Intercept memory binding via vkBindImageMemory2KHR to correlate image object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    auto* table = device_dispatch_table(device);
    assert(table->BindImageMemory2KHR != nullptr);
    VkResult result = table->BindImageMemory2KHR(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordImageBinds(device, bindInfoCount, pBindInfos);
    }
    return result;
}

// Intercept image creation to track image usage category flags.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
    PFN_vkCreateImage fpCreateImage = (PFN_vkCreateImage)device_dispatch_table(device)->CreateImage;
    VkResult result = fpCreateImage(device, pCreateInfo, pAllocator, pImage);
    if (result == VK_SUCCESS && pCreateInfo != nullptr && pImage != nullptr && *pImage != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnCreateImage(reinterpret_cast<uint64_t>(*pImage), pCreateInfo->usage);
        
        // Query and record memory requirements right after creation. 
        // This is needed because Vulkan 1.3 applications might use vkGetDeviceImageMemoryRequirements 
        // *before* creation, and subsequently skip calling vkGetImageMemoryRequirements, which would leave the tracked size as 0.
        if (device_dispatch_table(device)->GetImageMemoryRequirements && (pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT) == 0) {
            VkMemoryRequirements mem_reqs;
            device_dispatch_table(device)->GetImageMemoryRequirements(device, *pImage, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(*pImage), mem_reqs.size);
        }
    }
    return result;
}

// Intercept image destruction to clean up tracked handle state.
VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
    if (image != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnDestroyObject(reinterpret_cast<uint64_t>(image), VK_OBJECT_TYPE_IMAGE);
    }
    PFN_vkDestroyImage fpDestroyImage = (PFN_vkDestroyImage)device_dispatch_table(device)->DestroyImage;
    if (fpDestroyImage != NULL) {
        fpDestroyImage(device, image, pAllocator);
    }
}

// Intercept buffer creation to track buffer usage category flags and requested size.
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
    PFN_vkCreateBuffer fpCreateBuffer = (PFN_vkCreateBuffer)device_dispatch_table(device)->CreateBuffer;
    VkResult result = fpCreateBuffer(device, pCreateInfo, pAllocator, pBuffer);
    if (result == VK_SUCCESS && pCreateInfo != nullptr && pBuffer != nullptr && *pBuffer != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnCreateBuffer(reinterpret_cast<uint64_t>(*pBuffer), pCreateInfo->usage, pCreateInfo->size);
        
        // Query buffer memory requirements to record accurate actual bound size,
        // in case the app relies on vkGetDeviceBufferMemoryRequirements.
        if (device_dispatch_table(device)->GetBufferMemoryRequirements) {
            VkMemoryRequirements mem_reqs;
            device_dispatch_table(device)->GetBufferMemoryRequirements(device, *pBuffer, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(*pBuffer), mem_reqs.size);
        }
    }
    return result;
}

// Intercept buffer destruction to clean up tracked handle state.
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks* pAllocator) {
    if (buffer != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnDestroyObject(reinterpret_cast<uint64_t>(buffer), VK_OBJECT_TYPE_BUFFER);
    }
    PFN_vkDestroyBuffer fpDestroyBuffer = (PFN_vkDestroyBuffer)device_dispatch_table(device)->DestroyBuffer;
    if (fpDestroyBuffer != NULL) {
        fpDestroyBuffer(device, buffer, pAllocator);
    }
}

// Intercept image memory requirements query to record virtual resource size.
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements) {
    if (device_dispatch_table(device)->GetImageMemoryRequirements) {
        device_dispatch_table(device)->GetImageMemoryRequirements(device, image, pMemoryRequirements);
        if (image != VK_NULL_HANDLE && pMemoryRequirements != nullptr) {
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(image), pMemoryRequirements->size);
        }
    }
}

static void RecordImageRequirements(const VkImageMemoryRequirementsInfo2* pInfo, const VkMemoryRequirements2* pMemoryRequirements) {
    if (pInfo != nullptr && pInfo->image != VK_NULL_HANDLE && pMemoryRequirements != nullptr) {
        DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(pInfo->image), pMemoryRequirements->memoryRequirements.size);
    }
}

// Intercept image memory requirements 2 query to record virtual resource size.
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    if (device_dispatch_table(device)->GetImageMemoryRequirements2) {
        device_dispatch_table(device)->GetImageMemoryRequirements2(device, pInfo, pMemoryRequirements);
        RecordImageRequirements(pInfo, pMemoryRequirements);
    }
}

// Intercept image memory requirements 2 KHR query to record virtual resource size.
VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements2KHR(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    if (device_dispatch_table(device)->GetImageMemoryRequirements2KHR) {
        device_dispatch_table(device)->GetImageMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
        RecordImageRequirements(pInfo, pMemoryRequirements);
    }
}

// Intercept buffer memory requirements query to record virtual resource size.
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements) {
    if (device_dispatch_table(device)->GetBufferMemoryRequirements) {
        device_dispatch_table(device)->GetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
        if (buffer != VK_NULL_HANDLE && pMemoryRequirements != nullptr) {
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(buffer), pMemoryRequirements->size);
        }
    }
}

static void RecordBufferRequirements2(const VkBufferMemoryRequirementsInfo2* pInfo, const VkMemoryRequirements2* pMemoryRequirements) {
    if (pInfo != nullptr && pInfo->buffer != VK_NULL_HANDLE && pMemoryRequirements != nullptr) {
        DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(pInfo->buffer), pMemoryRequirements->memoryRequirements.size);
    }
}

// Intercept buffer memory requirements 2 query to record virtual resource size.
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    if (device_dispatch_table(device)->GetBufferMemoryRequirements2) {
        device_dispatch_table(device)->GetBufferMemoryRequirements2(device, pInfo, pMemoryRequirements);
        RecordBufferRequirements2(pInfo, pMemoryRequirements);
    }
}

// Intercept buffer memory requirements 2 KHR query to record virtual resource size.
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements2KHR(VkDevice device, const VkBufferMemoryRequirementsInfo2* pInfo, VkMemoryRequirements2* pMemoryRequirements) {
    if (device_dispatch_table(device)->GetBufferMemoryRequirements2KHR) {
        device_dispatch_table(device)->GetBufferMemoryRequirements2KHR(device, pInfo, pMemoryRequirements);
        RecordBufferRequirements2(pInfo, pMemoryRequirements);
    }
}

// Object naming from VK_EXT_debug_utils.
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* pNameInfo) {
    assert(pNameInfo != nullptr);
    auto* table = device_dispatch_table(device);
    // Naming is informational, so a driver that does not implement it is not an error.
    VkResult result = (table->SetDebugUtilsObjectNameEXT != nullptr)
                          ? table->SetDebugUtilsObjectNameEXT(device, pNameInfo)
                          : VK_SUCCESS;
    if (result == VK_SUCCESS) {
        DeviceMemoryReport::Get().SetDebugObjectName(pNameInfo->objectType, pNameInfo->objectHandle,
                                                     pNameInfo->pObjectName);
    }
    return result;
}

// Object naming from VK_EXT_debug_marker, the predecessor of VK_EXT_debug_utils.
VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectNameEXT(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* pNameInfo) {
    assert(pNameInfo != nullptr);
    auto* table = device_dispatch_table(device);
    VkResult result = (table->DebugMarkerSetObjectNameEXT != nullptr)
                          ? table->DebugMarkerSetObjectNameEXT(device, pNameInfo)
                          : VK_SUCCESS;
    if (result == VK_SUCCESS) {
        DeviceMemoryReport::Get().SetDebugObjectName(pNameInfo->objectType, pNameInfo->object,
                                                     pNameInfo->pObjectName);
    }
    return result;
}

// Companion passthroughs for VK_EXT_debug_utils and VK_EXT_debug_marker so the layer can
// advertise and support both extensions standalone without VK_LAYER_GOOGLE_DebugMarker.
VKAPI_ATTR VkResult VKAPI_CALL vkSetDebugUtilsObjectTagEXT(VkDevice device, const VkDebugUtilsObjectTagInfoEXT* pTagInfo) {
    auto* table = device_dispatch_table(device);
    return (table->SetDebugUtilsObjectTagEXT != nullptr) ? table->SetDebugUtilsObjectTagEXT(device, pTagInfo) : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdBeginDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(commandBuffer)->CmdBeginDebugUtilsLabelEXT) {
        device_dispatch_table(commandBuffer)->CmdBeginDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdEndDebugUtilsLabelEXT(VkCommandBuffer commandBuffer) {
    if (device_dispatch_table(commandBuffer)->CmdEndDebugUtilsLabelEXT) {
        device_dispatch_table(commandBuffer)->CmdEndDebugUtilsLabelEXT(commandBuffer);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdInsertDebugUtilsLabelEXT(VkCommandBuffer commandBuffer, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(commandBuffer)->CmdInsertDebugUtilsLabelEXT) {
        device_dispatch_table(commandBuffer)->CmdInsertDebugUtilsLabelEXT(commandBuffer, pLabelInfo);
    }
}

VKAPI_ATTR void VKAPI_CALL vkQueueBeginDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(queue)->QueueBeginDebugUtilsLabelEXT) {
        device_dispatch_table(queue)->QueueBeginDebugUtilsLabelEXT(queue, pLabelInfo);
    }
}

VKAPI_ATTR void VKAPI_CALL vkQueueEndDebugUtilsLabelEXT(VkQueue queue) {
    if (device_dispatch_table(queue)->QueueEndDebugUtilsLabelEXT) {
        device_dispatch_table(queue)->QueueEndDebugUtilsLabelEXT(queue);
    }
}

VKAPI_ATTR void VKAPI_CALL vkQueueInsertDebugUtilsLabelEXT(VkQueue queue, const VkDebugUtilsLabelEXT* pLabelInfo) {
    if (device_dispatch_table(queue)->QueueInsertDebugUtilsLabelEXT) {
        device_dispatch_table(queue)->QueueInsertDebugUtilsLabelEXT(queue, pLabelInfo);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkCreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                                              const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pMessenger) {
    if (instance_dispatch_table(instance)->CreateDebugUtilsMessengerEXT) {
        return instance_dispatch_table(instance)->CreateDebugUtilsMessengerEXT(instance, pCreateInfo, pAllocator, pMessenger);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                                                           const VkAllocationCallbacks* pAllocator) {
    if (instance_dispatch_table(instance)->DestroyDebugUtilsMessengerEXT) {
        instance_dispatch_table(instance)->DestroyDebugUtilsMessengerEXT(instance, messenger, pAllocator);
    }
}

VKAPI_ATTR void VKAPI_CALL vkSubmitDebugUtilsMessageEXT(VkInstance instance, VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                                        VkDebugUtilsMessageTypeFlagsEXT messageTypes,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData) {
    if (instance_dispatch_table(instance)->SubmitDebugUtilsMessageEXT) {
        instance_dispatch_table(instance)->SubmitDebugUtilsMessageEXT(instance, messageSeverity, messageTypes, pCallbackData);
    }
}

VKAPI_ATTR VkResult VKAPI_CALL vkDebugMarkerSetObjectTagEXT(VkDevice device, const VkDebugMarkerObjectTagInfoEXT* pTagInfo) {
    auto* table = device_dispatch_table(device);
    return (table->DebugMarkerSetObjectTagEXT != nullptr) ? table->DebugMarkerSetObjectTagEXT(device, pTagInfo) : VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerBeginEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    if (device_dispatch_table(commandBuffer)->CmdDebugMarkerBeginEXT) {
        device_dispatch_table(commandBuffer)->CmdDebugMarkerBeginEXT(commandBuffer, pMarkerInfo);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerEndEXT(VkCommandBuffer commandBuffer) {
    if (device_dispatch_table(commandBuffer)->CmdDebugMarkerEndEXT) {
        device_dispatch_table(commandBuffer)->CmdDebugMarkerEndEXT(commandBuffer);
    }
}

VKAPI_ATTR void VKAPI_CALL vkCmdDebugMarkerInsertEXT(VkCommandBuffer commandBuffer, const VkDebugMarkerMarkerInfoEXT* pMarkerInfo) {
    if (device_dispatch_table(commandBuffer)->CmdDebugMarkerInsertEXT) {
        device_dispatch_table(commandBuffer)->CmdDebugMarkerInsertEXT(commandBuffer, pMarkerInfo);
    }
}

} // extern "C"
