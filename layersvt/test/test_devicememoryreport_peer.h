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

#include "device_memory_report.h"

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

class DeviceMemoryReportTestPeer {
   public:
    static std::optional<DeviceMemoryReport::MemoryAllocation> FindAllocation(uint64_t memory_handle) {
        auto& report = DeviceMemoryReport::Get();
        std::lock_guard<std::mutex> lock(report.counter_mutex_);
        auto it = report.memory_allocations_.find(memory_handle);
        if (it == report.memory_allocations_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    static std::optional<DeviceMemoryReport::Resource> FindResource(uint64_t resource_handle) {
        auto& report = DeviceMemoryReport::Get();
        std::lock_guard<std::mutex> lock(report.counter_mutex_);
        auto it = report.resources_.find(resource_handle);
        if (it == report.resources_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    static std::string GetDebugObjectName(VkObjectType object_type, uint64_t object_handle) {
        auto& report = DeviceMemoryReport::Get();
        std::lock_guard<std::mutex> lock(report.counter_mutex_);
        auto it = report.debug_object_names_.find(std::make_pair(object_type, object_handle));
        return it != report.debug_object_names_.end() ? it->second : std::string();
    }
};
