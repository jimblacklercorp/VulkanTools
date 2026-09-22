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
#include <map>
#include <mutex>
#include <unordered_map>
#include <string>
#include <utility>
#include <vector>

#ifndef VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT
#define VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT 0x00000001
#endif

/**
 * @brief Categorizes buffer usage flags and memory property flags into a cluster category name.
 * @param usage The Vulkan buffer usage flags.
 * @param memFlags The physical memory property flags.
 * @return The name of the cluster category for buffer memory tracking.
 */
const char* GetBufferCluster(VkBufferUsageFlags usage, VkMemoryPropertyFlags memFlags = 0);

/**
 * @brief Categorizes image usage flags and memory property flags into a cluster category name.
 * @param usage The Vulkan image usage flags.
 * @param memFlags The physical memory property flags.
 * @return The name of the cluster category for image memory tracking.
 */
const char* GetImageCluster(VkImageUsageFlags usage, VkMemoryPropertyFlags memFlags = 0);

/**
 * The DeviceMemoryReport class is responsible for tracking Vulkan device memory
 * allocations and object associations, sending live memory usage counters to Perfetto traces.
 *
 * It tracks allocations per object handle, categorizing memory by usage
 * types (e.g., textures, surfaces, buffers).
 *
 * How it works:
 * The layer intercepts Vulkan memory allocation and object creation events, using either
 * VK_EXT_device_memory_report callbacks (when supported by the underlying driver) or falling back
 * to direct allocation intercepts (vkAllocateMemory/vkFreeMemory).
 * Object bindings (vkBindBufferMemory, vkBindImageMemory, vkBindBufferMemory2, vkBindImageMemory2) are tracked to attribute memory allocations to usage categories.
 *
 * Track Categories:
 * Memory usage counters are reported to Perfetto under:
 * - Driver vs Application allocations (e.g., vulkan.mem.driver.* vs vulkan.mem.app.*)
 * - Usages (vulkan.mem.*.usage.<category>)
 * - Instant events under the "VulkanDeviceMemoryReport" category ("VulkanMemoryAllocation" and
 *   "VulkanObjectName")
 *
 * This class is a singleton and provides thread-safe access to its state.
 */
class DeviceMemoryReport {
   public:
    /**
     * @brief Returns the singleton instance of the DeviceMemoryReport class.
     * @return Reference to the DeviceMemoryReport singleton.
     */
#if defined(__GNUC__) && __GNUC__ >= 4
    __attribute__((visibility("default")))
#endif
    static DeviceMemoryReport& Get();

    /**
     * @brief Associates a Vulkan physical device with its corresponding instance.
     * @param phys_dev The Vulkan physical device.
     * @param instance The Vulkan instance.
     */
    void SetVkInstance(VkPhysicalDevice phys_dev, VkInstance instance);

    /**
     * @brief Retrieves the Vulkan instance associated with a given physical device.
     * @param phys_dev The Vulkan physical device.
     * @return The associated Vulkan instance.
     */
    VkInstance GetVkInstance(VkPhysicalDevice phys_dev);

    /**
     * @brief Stores physical device memory properties for a logical device.
     * @param device The Vulkan device handle.
     * @param props The memory properties of the physical device.
     */
    void SetDeviceMemoryProperties(VkDevice device, const VkPhysicalDeviceMemoryProperties& props);

    /**
     * @brief Explicitly sets memory property flags for an allocation handle (useful for testing or overrides).
     * @param memory_handle The 64-bit handle of the Vulkan device memory.
     * @param property_flags The memory property flags.
     */
    void SetAllocationMemoryProperties(uint64_t memory_handle, VkMemoryPropertyFlags property_flags);

    /**
     * @brief Cleans up device-specific tracking state upon device destruction.
     * @param device The Vulkan device handle.
     */
    void OnDestroyDevice(VkDevice device);

    /**
     * @brief Static callback invoked by the VK_EXT_device_memory_report extension.
     * @param pCallbackData Pointer to the memory report callback data structure.
     * @param pUserData User data pointer (unused).
     */
    static void VKAPI_PTR MemoryReportCallback(const VkDeviceMemoryReportCallbackDataEXT* pCallbackData, void* pUserData);

    /**
     * @brief Processes a device memory report event received from the Vulkan driver callback.
     * @param pCallbackData Pointer to the memory report callback data structure.
     */
    void OnMemoryReportEvent(const VkDeviceMemoryReportCallbackDataEXT* pCallbackData);

    /**
     * @brief Sets whether a Vulkan device active callback is installed for VK_EXT_device_memory_report.
     * @param device The Vulkan device handle.
     * @param has_callback True if driver callback is active for the device, false otherwise.
     */
    void SetHasMemoryReportCallback(VkDevice device, bool has_callback);

    /**
     * @brief Handles fallback memory allocation tracking when driver callback is unavailable.
     * @param device The Vulkan device handle.
     * @param memory The VkDeviceMemory handle being allocated.
     * @param size The size of the allocation in bytes.
     * @param memory_type_index The memory type index from VkMemoryAllocateInfo.
     * @param property_flags Optional explicit memory property flags (for testing or overrides).
     */
    void OnAllocateMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize size, uint32_t memory_type_index = UINT32_MAX, VkMemoryPropertyFlags property_flags = 0);

    /**
     * @brief Handles fallback memory free tracking when driver callback is unavailable.
     * @param device The Vulkan device handle.
     * @param memory The VkDeviceMemory handle being freed.
     */
    void OnFreeMemory(VkDevice device, VkDeviceMemory memory);

    /**
     * @brief Tracks binding of buffer memory to correlate buffer usage with memory allocations.
     * @param buffer_handle The 64-bit handle of the Vulkan buffer.
     * @param memory_handle The 64-bit handle of the Vulkan device memory.
     * @param memory_offset Offset into device memory where buffer is bound.
     */
    void OnBindBufferMemory(uint64_t buffer_handle, uint64_t memory_handle, VkDeviceSize memory_offset);

    /**
     * @brief Tracks binding of image memory to correlate image usage with memory allocations.
     * @param image_handle The 64-bit handle of the Vulkan image.
     * @param memory_handle The 64-bit handle of the Vulkan device memory.
     * @param memory_offset Offset into device memory where image is bound.
     */
    void OnBindImageMemory(uint64_t image_handle, uint64_t memory_handle, VkDeviceSize memory_offset);

    /**
     * @brief Records the size of a virtual resource (buffer or image) in bytes.
     * @param resource_handle The 64-bit handle of the Vulkan object.
     * @param size Size in bytes from memory requirements or create info.
     */
    void OnRecordResourceSize(uint64_t resource_handle, VkDeviceSize size);

    /**
     * @brief Retrieves the recorded size of a virtual resource (for testing).
     * @param resource_handle The 64-bit handle of the Vulkan object.
     * @return Size in bytes, or 0 if not tracked.
     */
    VkDeviceSize GetRecordedResourceSize(uint64_t resource_handle);

    /**
     * @brief Resets all tracked internal state (for testing).
     */
    void Reset();

    /**
     * @brief Retrieves the current total memory bytes recorded on a usage track (for testing).
     * @param track The full usage track name (e.g. "vulkan.mem.app.usage.unbound_memory").
     * @return Number of bytes currently recorded on the track, or 0 if the track has no counter.
     */
    uint64_t GetUsageCounterBytes(const std::string& track);

    /**
     * @brief Tracks creation of a Vulkan image and its usage flags.
     * @param image_handle The 64-bit handle of the Vulkan image.
     * @param usage Usage flags for the created image.
     */
    void OnCreateImage(uint64_t image_handle, VkImageUsageFlags usage);

    /**
     * @brief Tracks creation of a Vulkan buffer, its usage flags, and requested size.
     * @param buffer_handle The 64-bit handle of the Vulkan buffer.
     * @param usage Usage flags for the created buffer.
     * @param size Size in bytes of the buffer allocation.
     */
    void OnCreateBuffer(uint64_t buffer_handle, VkBufferUsageFlags usage, VkDeviceSize size);

    /**
     * @brief Dumps the current state of counters and allocations to Perfetto.
     * Invoked when a new Perfetto tracing session starts. Acquires counter_mutex_,
     * so it must not be called while that lock is held.
     */
    void DumpCurrentCountersAndAllocations();

    /**
     * @brief Handles destruction of a Vulkan object, cleaning up tracked usage state and any
     * recorded debug name.
     *
     * Handles are only unique within an object type, so @p object_type is required to clear the
     * destroyed object's name without dropping the name of an unrelated live object that shares
     * the handle value.
     *
     * @param object_handle The 64-bit handle of the destroyed Vulkan object.
     * @param object_type The type of the destroyed object, as a VkObjectType.
     */
    void OnDestroyObject(uint64_t object_handle, VkObjectType object_type);

    /**
     * @brief Records the debug name an application gave to a Vulkan object and publishes it.
     *
     * Names arrive through VK_EXT_debug_utils or VK_EXT_debug_marker, typically well after the
     * object was created, so they are published as their own event stream rather than attached to
     * the memory events. Only the object types this layer attributes memory to are kept.
     *
     * @param object_type The type of the object, as a VkObjectType.
     * @param object_handle The 64-bit handle of the object.
     * @param name The name given by the application. A null or empty name clears the stored name.
     */
    void SetDebugObjectName(VkObjectType object_type, uint64_t object_handle, const char* name);

    /**
     * @brief Records the debug name an application gave to a Vulkan object via the legacy
     * VK_EXT_debug_marker extension and publishes it.
     *
     * Maps the legacy VkDebugReportObjectTypeEXT enum to VkObjectType for the object types this
     * layer attributes memory to, and ignores all other types.
     *
     * @param object_type The type of the object, as a VkDebugReportObjectTypeEXT.
     * @param object_handle The 64-bit handle of the object.
     * @param name The name given by the application. A null or empty name clears the stored name.
     */
    void SetDebugObjectName(VkDebugReportObjectTypeEXT object_type, uint64_t object_handle, const char* name);

   private:
    friend class DeviceMemoryReportTestPeer;

    /**
     * @brief Represents a sub-allocation of a Vulkan resource (buffer or image) bound within a physical memory allocation.
     */
    struct SubAllocation {
        uint64_t resource_handle;  /**< Handle of the bound Vulkan resource (buffer or image). */
        VkDeviceSize offset;       /**< Offset in bytes within the physical memory allocation where the resource is bound. */
        VkDeviceSize size;         /**< Size in bytes of the sub-allocated resource. */
        std::string usage_track;   /**< Name of the memory usage trace counter category for this sub-allocation. */
    };

    /**
     * @brief Tracks state and sub-allocations for a physical device memory allocation.
     */
    struct MemoryAllocation {
        VkDeviceSize total_size = 0;
        VkDeviceSize applied_unbound_bytes = 0;
        bool is_driver = false;
        VkObjectType object_type = VK_OBJECT_TYPE_UNKNOWN;
        VkMemoryPropertyFlags mem_flags = 0;
        std::vector<SubAllocation> sub_allocations;
        std::string unbound_usage_track;
        uint64_t object_handle = 0;
        const char* cluster_name = "unbound_memory";
    };

    /**
     * @brief Tracks metadata for a virtual resource (buffer or image).
     */
    struct Resource {
        bool is_image = false;
        VkImageUsageFlags image_usage = 0;
        VkBufferUsageFlags buffer_usage = 0;
        VkDeviceSize size = 0;

        const char* GetCluster(VkMemoryPropertyFlags mem_flags) const {
            if (is_image) {
                return GetImageCluster(image_usage, mem_flags);
            }
            return GetBufferCluster(buffer_usage, mem_flags);
        }
    };

    /**
     * @brief Helper to bind resource memory (buffers and images).
     */
    void BindResourceMemory(uint64_t resource_handle, uint64_t memory_handle, VkDeviceSize memory_offset);

    /**
     * @brief Updates unbound memory category counter for a physical memory slab.
     * Unbound memory is allocated device memory not currently bound to any active resource (e.g. buffer or image).
     */
    void UpdateAllocationUnboundCounter(uint64_t memory_handle);

    /**
     * @brief Removes a sub-allocation of a resource from a physical memory slab if bound.
     */
    void RemoveResourceBinding(uint64_t resource_handle);

    /**
     * @brief Removes all sub-allocations and tracking for a memory slab being freed.
     */
    void RemoveAllocationTracking(uint64_t memory_handle);

    /**
     * @brief Republishes every known object name. Called while counter_mutex_ is held.
     */
    void EmitAllDebugObjectNames();

    /**
     * @brief Increments trace counter for a memory track.
     */
    void AddCounterBytes(const std::string& track, uint64_t size);

    /**
     * @brief Decrements trace counter for a memory track with underflow protection.
     */
    void SubtractCounterBytes(const std::string& track, uint64_t size);

    /**
     * @brief Mutex protecting access to the physical device to instance mapping table.
     */
    std::mutex map_mutex_;

    /**
     * @brief Maps a physical device handle to its corresponding Vulkan instance handle.
     */
    std::unordered_map<VkPhysicalDevice, VkInstance> vk_instance_map_;

    /**
     * @brief Mutex protecting access to memory tracking tables and counter states.
     */
    std::mutex counter_mutex_;

    /**
     * @brief Maps a Vulkan device handle to a boolean indicating if driver memory report callback is active.
     */
    std::unordered_map<VkDevice, bool> has_callback_map_;

    /**
     * @brief Maps a Vulkan device handle to its physical device memory properties.
     */
    std::unordered_map<VkDevice, VkPhysicalDeviceMemoryProperties> device_memory_properties_map_;

    /**
     * @brief Maps a virtual resource handle to its metadata.
     */
    std::unordered_map<uint64_t, Resource> resources_;

    /**
     * @brief Maps a virtual resource handle to the physical memory handle it is bound to.
     */
    std::unordered_map<uint64_t, uint64_t> resource_to_memory_map_;

    /**
     * @brief Maps a physical memory handle to its allocation details and sub-allocations.
     */
    std::unordered_map<uint64_t, MemoryAllocation> memory_allocations_;

    /**
     * @brief Maps a usage track name to its current total memory usage in bytes.
     */
    std::unordered_map<std::string, uint64_t> usage_memory_bytes_;

    /**
     * @brief Maps a pair of (object_type, object_handle) to the name the application gave the object.
     *
     * Handles are only unique within an object type, hence the pair key. Like resources_ and
     * memory_allocations_, keys are not scoped by VkDevice, and entries persist across
     * OnDestroyDevice until the object itself is destroyed or Reset() is called. Objects not
     * destroyed through vkDestroyImage, vkDestroyBuffer, or vkFreeMemory (such as presentable
     * VkImages owned by a VkSwapchainKHR) retain their entries until Reset() or until a recycled
     * handle is renamed. Guarded by counter_mutex_. std::map is used instead of unordered_map for
     * pair key support and deterministic replay order.
     */
    std::map<std::pair<VkObjectType, uint64_t>, std::string> debug_object_names_;
};
