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
#include <unordered_map>

/**
 * The DebugMarker class holds the state the DebugMarker layer needs beyond object names.
 *
 * Object names themselves are not stored here: the tracking of (VkObjectType, handle) -> name,
 * the mapping of the legacy VK_EXT_debug_marker object types and the replay of known names when a
 * Perfetto session starts all live in layersvt::VulkanObjectNames
 * (see object_names/vulkan_object_names.h), which this layer shares with the DeviceMemoryReport
 * layer.
 *
 * This class is a singleton and provides thread-safe access to its state.
 */
class DebugMarker {
   public:
    /**
     * @brief Returns the singleton instance of the DebugMarker class.
     * @return Reference to the DebugMarker singleton.
     */
    static DebugMarker& Get();

    /**
     * @brief Clears all instance mappings.
     * @note This function is for testing only.
     */
    void Clear();

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

   private:
    std::mutex mutex_;
    /**
     * @brief Maps a physical device handle to its corresponding Vulkan instance handle.
     */
    std::unordered_map<VkPhysicalDevice, VkInstance> vk_instance_map_;
};
