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

#ifndef LAYERSVT_OBJECT_NAMES_VULKAN_OBJECT_NAMES_PERFETTO_H
#define LAYERSVT_OBJECT_NAMES_VULKAN_OBJECT_NAMES_PERFETTO_H

#include "object_names/vulkan_object_names.h"
#include "perfetto/perfetto.h"

namespace layersvt {

/**
 * @brief Writes one VulkanApiEvent.VkDebugUtilsObjectName trace packet.
 *
 * This is the single definition of the object-name packet layout, shared by every layer that
 * publishes object names. It is a template because each layer declares its own track event data
 * source (via PERFETTO_DEFINE_CATEGORIES) with its own category set; a layer instantiates this
 * with its own perfetto::TrackEvent to provide EmitVulkanObjectName, e.g.
 *
 * @code
 * namespace layersvt {
 * void EmitVulkanObjectName(const VulkanObjectName& object_name) {
 *     WriteVulkanObjectNamePacket<perfetto::TrackEvent>(object_name);
 * }
 * }  // namespace layersvt
 * @endcode
 *
 * The packet is written as a raw trace packet rather than a track event, so it is delivered to
 * every session in which the layer's data source is enabled, independently of category filtering.
 *
 * @tparam TrackEventDataSource The layer's track event data source type.
 * @param object_name The object name to write.
 */
template <typename TrackEventDataSource>
void WriteVulkanObjectNamePacket(const VulkanObjectName& object_name) {
    const uint64_t vk_device = object_name.vk_device;
    const int32_t object_type = object_name.object_type;
    const uint64_t handle = object_name.handle;
    const std::string name = object_name.name;

    TrackEventDataSource::Trace([vk_device, object_type, handle, name](auto ctx) {
        auto packet = ctx.NewTracePacket();
        packet->set_timestamp(perfetto::base::GetBootTimeNs().count());
        auto event = packet->set_vulkan_api_event()->set_vk_debug_utils_object_name();
        event->set_vk_device(vk_device);
        event->set_object_type(object_type);
        event->set_object(handle);
        event->set_object_name(name.c_str());
    });
}

}  // namespace layersvt

#endif  // LAYERSVT_OBJECT_NAMES_VULKAN_OBJECT_NAMES_PERFETTO_H
