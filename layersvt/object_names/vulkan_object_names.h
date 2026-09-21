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

#ifndef LAYERSVT_OBJECT_NAMES_VULKAN_OBJECT_NAMES_H
#define LAYERSVT_OBJECT_NAMES_VULKAN_OBJECT_NAMES_H

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace layersvt {

/**
 * @brief Converts a legacy VK_EXT_debug_marker object type to the modern VkObjectType.
 *
 * VK_EXT_debug_marker names objects using the legacy VkDebugReportObjectTypeEXT enum, whereas
 * VK_EXT_debug_utils and the Perfetto trace packets both use VkObjectType. Every layer that
 * observes object names therefore has to normalise to VkObjectType, so the mapping lives here.
 *
 * @param debug_report_object_type The legacy object type.
 * @return The equivalent VkObjectType, or VK_OBJECT_TYPE_UNKNOWN if there is no equivalent.
 */
VkObjectType VkObjectTypeFromDebugReportObjectType(VkDebugReportObjectTypeEXT debug_report_object_type);

/**
 * @brief A single (object type, handle) -> name association, together with its owning device.
 */
struct VulkanObjectName {
    uint64_t vk_device = 0; /**< Handle of the VkDevice the object belongs to. */
    int32_t object_type = 0; /**< VkObjectType of the named object. */
    uint64_t handle = 0;     /**< Handle of the named object. */
    std::string name;        /**< Name currently assigned to the object. */
};

/**
 * @brief Writes one object name to the tracing backend.
 *
 * This is the seam between the shared bookkeeping in this component and the Perfetto tracing
 * machinery of an individual layer. It is *declared* here and *defined by each layer*, because the
 * generated perfetto::TrackEvent data source is layer-private: every layer registers its own with
 * its own category set. Resolving the emitter at link time keeps this component free of any
 * Perfetto dependency.
 *
 * The layers deliberately emit *different representations*, because their names are read back by
 * different consumers:
 *
 * - DebugMarker writes a VulkanApiEvent.VkDebugUtilsObjectName trace packet (see
 *   vulkan_object_names_perfetto.h). Perfetto's trace_processor folds those into an internal map and
 *   uses it to label GPU render stage slices with their render pass, render target and command
 *   buffer names.
 * - DeviceMemoryReport writes a "VulkanObjectName" track event instead, because trace_processor only
 *   resolves that internal map for those three render stage object types. A name on a VkBuffer or
 *   VkImage would be parsed and then never read, so the memory layer publishes names as ordinary
 *   instant events that its consumer can query directly.
 *
 * @param object_name The object name to write. Called with the store's mutex held, so
 *        implementations must not call back into VulkanObjectNames.
 */
void EmitVulkanObjectName(const VulkanObjectName& object_name);

/**
 * VulkanObjectNames is the shared store of Vulkan debug object names, used by every layer that
 * observes vkSetDebugUtilsObjectNameEXT / vkDebugMarkerSetObjectNameEXT.
 *
 * How it works:
 * We do not store a history of naming events. Instead we store only the current name for each
 * object (one name per object). This keeps the memory footprint small for most applications
 * (proportional to the name size multiplied by the number of unique objects).
 *
 * Perfetto session support:
 * This solution supports
 * - starting a Perfetto session before the application starts,
 * - starting a Perfetto session after the application is already running,
 * - running multiple Perfetto sessions during a single application run.
 *
 * To support late attach and multiple sessions, a layer calls EmitAll() when a Perfetto session
 * starts, which replays every currently known object name into the new session. Names are retained
 * in memory because a user might start another session later, requiring us to replay them again.
 *
 * A potential issue exists if an application constantly creates and destroys objects without
 * bound, as we currently do not remove names for destroyed objects. Support for removing names on
 * object destruction can be added later if needed.
 *
 * This class is a singleton (one per loaded layer module) and provides thread-safe access to its
 * state.
 */
class VulkanObjectNames {
   public:
    /**
     * @brief Returns the singleton instance.
     */
    static VulkanObjectNames& Get();

    /**
     * @brief Sets or updates the name associated with a Vulkan object and emits it to the trace.
     * @param vk_device The handle of the Vulkan device that owns the object.
     * @param object_type The VkObjectType of the object, as int32_t.
     * @param handle The handle of the Vulkan object.
     * @param name The name to associate with the object. A null name is stored as "NULL".
     */
    void SetObjectName(uint64_t vk_device, int32_t object_type, uint64_t handle, const char* name);

    /**
     * @brief VK_EXT_debug_utils overload of SetObjectName. Ignores a null @p name_info.
     * @param device The device passed to vkSetDebugUtilsObjectNameEXT.
     * @param name_info The naming information provided by the application.
     */
    void SetObjectName(VkDevice device, const VkDebugUtilsObjectNameInfoEXT* name_info);

    /**
     * @brief VK_EXT_debug_marker overload of SetObjectName, mapping the legacy object type.
     * Ignores a null @p name_info.
     * @param device The device passed to vkDebugMarkerSetObjectNameEXT.
     * @param name_info The naming information provided by the application.
     */
    void SetObjectName(VkDevice device, const VkDebugMarkerObjectNameInfoEXT* name_info);

    /**
     * @brief Replays every stored object name into the trace.
     *
     * Call this when a Perfetto session starts so that sessions which attach after the objects
     * were named still observe their names.
     */
    void EmitAll() const;

    /**
     * @brief Forgets every stored object name.
     * @note This function is for testing only.
     */
    void Clear();

    /**
     * @brief Checks whether @p name is the name currently stored for an object.
     * @note This function is for testing only.
     */
    bool HasObjectName(int32_t object_type, uint64_t handle, const std::string& name) const;

    /**
     * @brief Returns the name currently stored for an object, or an empty string if it has none.
     */
    std::string GetObjectName(int32_t object_type, uint64_t handle) const;

    /**
     * @brief Returns the number of objects that currently have a name.
     */
    size_t Size() const;

   private:
    VulkanObjectNames() = default;
    ~VulkanObjectNames() = default;
    VulkanObjectNames(const VulkanObjectNames&) = delete;
    VulkanObjectNames& operator=(const VulkanObjectNames&) = delete;

    /**
     * @brief Guards object_names_.
     *
     * Names are emitted while this mutex is held. That is deliberate: it makes the order in which
     * packets are written match the order in which names were stored, so a concurrent EmitAll()
     * replay can never overwrite a newer name with an older one in the trace.
     */
    mutable std::mutex mutex_;

    /**
     * @brief Maps a pair of (object_type, object_handle) to its name information.
     * We use a pair as the key because handles are not guaranteed to be unique across different
     * object types.
     */
    std::map<std::pair<int32_t, uint64_t>, VulkanObjectName> object_names_;
};

}  // namespace layersvt

#endif  // LAYERSVT_OBJECT_NAMES_VULKAN_OBJECT_NAMES_H
