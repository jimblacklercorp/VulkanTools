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
// - vkCreateDevice / vkDestroyDevice: Initializes/destroys device dispatch tables and
//   injects VK_EXT_device_memory_report callback registration into device creation.
//
// Memory tracking & resource tracking intercepts:
// - vkAllocateMemory / vkFreeMemory: Tracks direct allocations/frees as fallbacks.
// - vkBindBufferMemory* / vkBindImageMemory*: Associates buffer/image handles with memory allocations.
// - vkCreateBuffer / vkDestroyBuffer: Tracks buffer creation, usage flags, and requested sizes.
// - vkCreateImage / vkDestroyImage: Tracks image creation and usage flags.
// - vkGetBufferMemoryRequirements* / vkGetImageMemoryRequirements*: Tracks resource memory requirements.
// - vkEnumerate*ExtensionProperties / vkEnumerate*LayerProperties: Advertises the layer
//   and support for the VK_EXT_device_memory_report extension.

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

    // Check if the underlying driver supports VK_EXT_device_memory_report
    bool supports_memory_report = false;
    uint32_t ext_count = 0;
    if (instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties) {
        if (instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &ext_count, nullptr) == VK_SUCCESS && ext_count > 0) {
            std::vector<VkExtensionProperties> exts(ext_count);
            if (instance_dispatch_table(physicalDevice)->EnumerateDeviceExtensionProperties(physicalDevice, nullptr, &ext_count, exts.data()) == VK_SUCCESS) {
                for (const auto& ext : exts) {
                    if (strcmp(ext.extensionName, VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME) == 0) {
                        supports_memory_report = true;
                        break;
                    }
                }
            }
        }
    }

    // If supported, inject VK_EXT_device_memory_report callback into pNext chain
    VkDeviceCreateInfo modified_create_info = *pCreateInfo;
    std::vector<const char*> enabled_extensions;
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        enabled_extensions.push_back(pCreateInfo->ppEnabledExtensionNames[i]);
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
        modified_create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        modified_create_info.ppEnabledExtensionNames = enabled_extensions.data();

        memory_report_ci.sType = VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT;
        memory_report_ci.pfnUserCallback = DeviceMemoryReport::MemoryReportCallback;
        memory_report_ci.pUserData = nullptr;
        memory_report_ci.pNext = modified_create_info.pNext;
        modified_create_info.pNext = &memory_report_ci;
    }

    VkResult result = fpCreateDevice(physicalDevice, supports_memory_report ? &modified_create_info : pCreateInfo, pAllocator, pDevice);
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


// Intercept memory binding to correlate buffer object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    auto* table = device_dispatch_table(device);
    if (!table || !table->BindBufferMemory) return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (buffer != VK_NULL_HANDLE && DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(buffer)) == 0) {
        if (table->GetBufferMemoryRequirements) {
            VkMemoryRequirements mem_reqs;
            table->GetBufferMemoryRequirements(device, buffer, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(buffer), mem_reqs.size);
        }
    }
    VkResult result = table->BindBufferMemory(device, buffer, memory, memoryOffset);
    if (result == VK_SUCCESS && buffer != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnBindBufferMemory(reinterpret_cast<uint64_t>(buffer), reinterpret_cast<uint64_t>(memory), memoryOffset);
    }
    return result;
}

// Intercept memory binding to correlate image object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset) {
    auto* table = device_dispatch_table(device);
    if (!table || !table->BindImageMemory) return VK_ERROR_EXTENSION_NOT_PRESENT;
    if (image != VK_NULL_HANDLE && DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(image)) == 0) {
        if (table->GetImageMemoryRequirements) {
            VkMemoryRequirements mem_reqs;
            table->GetImageMemoryRequirements(device, image, &mem_reqs);
            DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(image), mem_reqs.size);
        }
    }
    VkResult result = table->BindImageMemory(device, image, memory, memoryOffset);
    if (result == VK_SUCCESS && image != VK_NULL_HANDLE && memory != VK_NULL_HANDLE) {
        DeviceMemoryReport::Get().OnBindImageMemory(reinterpret_cast<uint64_t>(image), reinterpret_cast<uint64_t>(memory), memoryOffset);
    }
    return result;
}

static void RecordBufferBindings(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        if (pBindInfos[i].buffer != VK_NULL_HANDLE && pBindInfos[i].memory != VK_NULL_HANDLE) {
            if (DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].buffer)) == 0) {
                if (device_dispatch_table(device)->GetBufferMemoryRequirements) {
                    VkMemoryRequirements mem_reqs;
                    device_dispatch_table(device)->GetBufferMemoryRequirements(device, pBindInfos[i].buffer, &mem_reqs);
                    DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].buffer), mem_reqs.size);
                }
            }
            DeviceMemoryReport::Get().OnBindBufferMemory(reinterpret_cast<uint64_t>(pBindInfos[i].buffer), reinterpret_cast<uint64_t>(pBindInfos[i].memory), pBindInfos[i].memoryOffset);
        }
    }
}

// Intercept memory binding via vkBindBufferMemory2 to correlate buffer object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    if (!device_dispatch_table(device)->BindBufferMemory2) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkResult result = device_dispatch_table(device)->BindBufferMemory2(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordBufferBindings(device, bindInfoCount, pBindInfos);
    }
    return result;
}

// Intercept memory binding via vkBindBufferMemory2KHR to correlate buffer object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindBufferMemoryInfo* pBindInfos) {
    if (!device_dispatch_table(device)->BindBufferMemory2KHR) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkResult result = device_dispatch_table(device)->BindBufferMemory2KHR(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordBufferBindings(device, bindInfoCount, pBindInfos);
    }
    return result;
}

static void RecordImageBinds(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    for (uint32_t i = 0; i < bindInfoCount; ++i) {
        if (pBindInfos[i].image != VK_NULL_HANDLE && pBindInfos[i].memory != VK_NULL_HANDLE) {
            if (DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].image)) == 0) {
                if (device_dispatch_table(device)->GetImageMemoryRequirements) {
                    VkMemoryRequirements mem_reqs;
                    device_dispatch_table(device)->GetImageMemoryRequirements(device, pBindInfos[i].image, &mem_reqs);
                    DeviceMemoryReport::Get().OnRecordResourceSize(reinterpret_cast<uint64_t>(pBindInfos[i].image), mem_reqs.size);
                }
            }
            DeviceMemoryReport::Get().OnBindImageMemory(reinterpret_cast<uint64_t>(pBindInfos[i].image), reinterpret_cast<uint64_t>(pBindInfos[i].memory), pBindInfos[i].memoryOffset);
        }
    }
}

// Intercept memory binding via vkBindImageMemory2 to correlate image object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    if (!device_dispatch_table(device)->BindImageMemory2) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkResult result = device_dispatch_table(device)->BindImageMemory2(device, bindInfoCount, pBindInfos);
    if (result == VK_SUCCESS && pBindInfos != nullptr) {
        RecordImageBinds(device, bindInfoCount, pBindInfos);
    }
    return result;
}

// Intercept memory binding via vkBindImageMemory2KHR to correlate image object handles with device memory allocations.
VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos) {
    if (!device_dispatch_table(device)->BindImageMemory2KHR) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkResult result = device_dispatch_table(device)->BindImageMemory2KHR(device, bindInfoCount, pBindInfos);
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
        DeviceMemoryReport::Get().OnDestroyObject(reinterpret_cast<uint64_t>(image));
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
        DeviceMemoryReport::Get().OnDestroyObject(reinterpret_cast<uint64_t>(buffer));
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

} // extern "C"
