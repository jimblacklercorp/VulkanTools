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

// Unit tests for the object name bookkeeping shared by the DebugMarker and DeviceMemoryReport
// layers.
//
// The component leaves EmitVulkanObjectName to be defined by whichever layer links it, so
// this test supplies its own recording implementation instead of a Perfetto one. That keeps the
// test free of any tracing backend while still covering what the layers rely on: what is stored,
// and exactly which packets would be emitted and when.

#include "object_names/vulkan_object_names.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<layersvt::VulkanObjectName>& EmittedPackets() {
    static std::vector<layersvt::VulkanObjectName> packets;
    return packets;
}

// Resets both the store and the recorded packets. The store is a singleton, so every test must
// start from a known state.
void ResetState() {
    layersvt::VulkanObjectNames::Get().Clear();
    EmittedPackets().clear();
}

VkDevice FakeDevice(uintptr_t value) { return reinterpret_cast<VkDevice>(value); }

}  // namespace

namespace layersvt {

// The link seam under test: the store calls this for every name it stores or replays.
void EmitVulkanObjectName(const VulkanObjectName& object_name) { EmittedPackets().push_back(object_name); }

}  // namespace layersvt

TEST(VulkanObjectNamesTest, StoresAndEmitsName) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    names.SetObjectName(0x1234, VK_OBJECT_TYPE_BUFFER, 0x42, "MyBuffer");

    EXPECT_EQ(names.Size(), 1u);
    EXPECT_TRUE(names.HasObjectName(VK_OBJECT_TYPE_BUFFER, 0x42, "MyBuffer"));
    EXPECT_EQ(names.GetObjectName(VK_OBJECT_TYPE_BUFFER, 0x42), "MyBuffer");

    ASSERT_EQ(EmittedPackets().size(), 1u);
    EXPECT_EQ(EmittedPackets()[0].vk_device, 0x1234u);
    EXPECT_EQ(EmittedPackets()[0].object_type, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(EmittedPackets()[0].handle, 0x42u);
    EXPECT_EQ(EmittedPackets()[0].name, "MyBuffer");
}

TEST(VulkanObjectNamesTest, RenamingReplacesTheStoredName) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    names.SetObjectName(0, VK_OBJECT_TYPE_IMAGE, 0x7, "First");
    names.SetObjectName(0, VK_OBJECT_TYPE_IMAGE, 0x7, "Second");

    // Only the current name is retained, but both naming events were emitted.
    EXPECT_EQ(names.Size(), 1u);
    EXPECT_TRUE(names.HasObjectName(VK_OBJECT_TYPE_IMAGE, 0x7, "Second"));
    EXPECT_FALSE(names.HasObjectName(VK_OBJECT_TYPE_IMAGE, 0x7, "First"));
    EXPECT_EQ(EmittedPackets().size(), 2u);
}

TEST(VulkanObjectNamesTest, HandlesAreScopedByObjectType) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    // Handles are only unique within an object type, so the same value must not collide.
    names.SetObjectName(0, VK_OBJECT_TYPE_BUFFER, 0x100, "Buffer");
    names.SetObjectName(0, VK_OBJECT_TYPE_IMAGE, 0x100, "Image");

    EXPECT_EQ(names.Size(), 2u);
    EXPECT_EQ(names.GetObjectName(VK_OBJECT_TYPE_BUFFER, 0x100), "Buffer");
    EXPECT_EQ(names.GetObjectName(VK_OBJECT_TYPE_IMAGE, 0x100), "Image");
}

TEST(VulkanObjectNamesTest, NullNameIsStoredAsNullLiteral) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    names.SetObjectName(0, VK_OBJECT_TYPE_BUFFER, 0x1, nullptr);

    EXPECT_TRUE(names.HasObjectName(VK_OBJECT_TYPE_BUFFER, 0x1, "NULL"));
}

TEST(VulkanObjectNamesTest, UnknownObjectHasNoName) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    EXPECT_FALSE(names.HasObjectName(VK_OBJECT_TYPE_BUFFER, 0x999, "Anything"));
    EXPECT_EQ(names.GetObjectName(VK_OBJECT_TYPE_BUFFER, 0x999), "");
}

TEST(VulkanObjectNamesTest, DebugUtilsNameInfoIsStored) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    VkDebugUtilsObjectNameInfoEXT name_info = {};
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    name_info.objectType = VK_OBJECT_TYPE_IMAGE;
    name_info.objectHandle = 0xABC;
    name_info.pObjectName = "GBuffer";

    names.SetObjectName(FakeDevice(0x5000), &name_info);

    EXPECT_TRUE(names.HasObjectName(VK_OBJECT_TYPE_IMAGE, 0xABC, "GBuffer"));
    ASSERT_EQ(EmittedPackets().size(), 1u);
    EXPECT_EQ(EmittedPackets()[0].vk_device, (uint64_t)FakeDevice(0x5000));
}

TEST(VulkanObjectNamesTest, DebugMarkerNameInfoIsMappedToModernObjectType) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    VkDebugMarkerObjectNameInfoEXT name_info = {};
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
    name_info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
    name_info.object = 0xABC;
    name_info.pObjectName = "LegacyImage";

    names.SetObjectName(FakeDevice(0x5000), &name_info);

    // Stored against the modern enum, matching what VK_EXT_debug_utils would have produced.
    EXPECT_TRUE(names.HasObjectName(VK_OBJECT_TYPE_IMAGE, 0xABC, "LegacyImage"));
    ASSERT_EQ(EmittedPackets().size(), 1u);
    EXPECT_EQ(EmittedPackets()[0].object_type, VK_OBJECT_TYPE_IMAGE);
}

TEST(VulkanObjectNamesTest, NullNameInfoIsIgnored) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    names.SetObjectName(FakeDevice(0x1), static_cast<const VkDebugUtilsObjectNameInfoEXT*>(nullptr));
    names.SetObjectName(FakeDevice(0x1), static_cast<const VkDebugMarkerObjectNameInfoEXT*>(nullptr));

    EXPECT_EQ(names.Size(), 0u);
    EXPECT_EQ(EmittedPackets().size(), 0u);
}

TEST(VulkanObjectNamesTest, EmitAllReplaysEveryKnownName) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    names.SetObjectName(0, VK_OBJECT_TYPE_BUFFER, 0x1, "A");
    names.SetObjectName(0, VK_OBJECT_TYPE_BUFFER, 0x2, "B");
    names.SetObjectName(0, VK_OBJECT_TYPE_BUFFER, 0x1, "A2");
    EmittedPackets().clear();

    // A session starting late must still observe the current name of every object, and only the
    // current one.
    names.EmitAll();

    ASSERT_EQ(EmittedPackets().size(), 2u);
    EXPECT_EQ(EmittedPackets()[0].name, "A2");
    EXPECT_EQ(EmittedPackets()[1].name, "B");
}

TEST(VulkanObjectNamesTest, ClearDropsEverything) {
    ResetState();
    auto& names = layersvt::VulkanObjectNames::Get();

    names.SetObjectName(0, VK_OBJECT_TYPE_BUFFER, 0x1, "A");
    names.Clear();
    EmittedPackets().clear();

    EXPECT_EQ(names.Size(), 0u);
    names.EmitAll();
    EXPECT_EQ(EmittedPackets().size(), 0u);
}

TEST(VulkanObjectNamesTest, LegacyObjectTypeMapping) {
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_UNKNOWN_EXT), VK_OBJECT_TYPE_UNKNOWN);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_INSTANCE_EXT), VK_OBJECT_TYPE_INSTANCE);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_EXT), VK_OBJECT_TYPE_DEVICE);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT), VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT), VK_OBJECT_TYPE_IMAGE);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT),
              VK_OBJECT_TYPE_DEVICE_MEMORY);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_COMMAND_BUFFER_EXT),
              VK_OBJECT_TYPE_COMMAND_BUFFER);
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(VK_DEBUG_REPORT_OBJECT_TYPE_SWAPCHAIN_KHR_EXT),
              VK_OBJECT_TYPE_SWAPCHAIN_KHR);

    // Anything without a modern equivalent falls back to UNKNOWN rather than a bogus type.
    EXPECT_EQ(layersvt::VkObjectTypeFromDebugReportObjectType(static_cast<VkDebugReportObjectTypeEXT>(0x7FFFFFFF)),
              VK_OBJECT_TYPE_UNKNOWN);
}
