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

// Unit tests for the memory binding entry points of the DeviceMemoryReport layer.
//
// The layer's vk* entry points are linked directly into this test binary (the Vulkan loader is
// deliberately not linked), and every fake VkDevice is given a dispatch table built from the stub
// driver below. That makes it possible to exercise the layer's interception logic without a real
// Vulkan implementation.

#include "device_memory_report.h"
#include "object_names/vulkan_object_names.h"
#include "vk_layer_table.h"

#include <vulkan/vulkan.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

// Sizes returned by the stub driver's memory requirement queries.
VkDeviceSize g_buffer_requirements_size = 0;
VkDeviceSize g_image_requirements_size = 0;

// Number of times the stub driver's memory requirement queries were called.
int g_buffer_requirements_queries = 0;
int g_image_requirements_queries = 0;

// Whether the stub driver implements the object naming entry points, and how often it saw them.
// The layer must only intercept naming calls when the driver below actually supports them.
bool g_supports_object_names = false;
int g_set_debug_utils_object_name_calls = 0;
int g_debug_marker_set_object_name_calls = 0;

template <typename HandleType>
HandleType MakeHandle(uintptr_t value) {
    return reinterpret_cast<HandleType>(value);
}

template <typename HandleType>
uint64_t AsObjectHandle(HandleType handle) {
    return reinterpret_cast<uint64_t>(handle);
}

uintptr_t g_next_handle = 0x10000;

VKAPI_ATTR VkResult VKAPI_CALL StubCreateImage(VkDevice, const VkImageCreateInfo*, const VkAllocationCallbacks*, VkImage* pImage) {
    if (pImage != nullptr) {
        *pImage = MakeHandle<VkImage>(++g_next_handle);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL StubDestroyImage(VkDevice, VkImage, const VkAllocationCallbacks*) {}

VKAPI_ATTR VkResult VKAPI_CALL StubAllocateMemory(VkDevice, const VkMemoryAllocateInfo*, const VkAllocationCallbacks*, VkDeviceMemory* pMemory) {
    if (pMemory != nullptr) {
        *pMemory = MakeHandle<VkDeviceMemory>(++g_next_handle);
    }
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL StubFreeMemory(VkDevice, VkDeviceMemory, const VkAllocationCallbacks*) {}

VKAPI_ATTR VkResult VKAPI_CALL StubBindBufferMemory(VkDevice, VkBuffer, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL StubBindImageMemory(VkDevice, VkImage, VkDeviceMemory, VkDeviceSize) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL StubBindBufferMemory2(VkDevice, uint32_t, const VkBindBufferMemoryInfo*) { return VK_SUCCESS; }

VKAPI_ATTR VkResult VKAPI_CALL StubBindImageMemory2(VkDevice, uint32_t, const VkBindImageMemoryInfo*) { return VK_SUCCESS; }

VKAPI_ATTR void VKAPI_CALL StubGetBufferMemoryRequirements(VkDevice, VkBuffer, VkMemoryRequirements* pMemoryRequirements) {
    ++g_buffer_requirements_queries;
    pMemoryRequirements->size = g_buffer_requirements_size;
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = 1;
}

VKAPI_ATTR void VKAPI_CALL StubGetImageMemoryRequirements(VkDevice, VkImage, VkMemoryRequirements* pMemoryRequirements) {
    ++g_image_requirements_queries;
    pMemoryRequirements->size = g_image_requirements_size;
    pMemoryRequirements->alignment = 256;
    pMemoryRequirements->memoryTypeBits = 1;
}

VKAPI_ATTR VkResult VKAPI_CALL StubSetDebugUtilsObjectNameEXT(VkDevice, const VkDebugUtilsObjectNameInfoEXT*) {
    ++g_set_debug_utils_object_name_calls;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL StubDebugMarkerSetObjectNameEXT(VkDevice, const VkDebugMarkerObjectNameInfoEXT*) {
    ++g_debug_marker_set_object_name_calls;
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL StubGetDeviceProcAddr(VkDevice, const char* pName) {
    if (pName == nullptr) return nullptr;
    const std::string name(pName);

    if (g_supports_object_names) {
        if (name == "vkSetDebugUtilsObjectNameEXT") return reinterpret_cast<PFN_vkVoidFunction>(StubSetDebugUtilsObjectNameEXT);
        if (name == "vkDebugMarkerSetObjectNameEXT") return reinterpret_cast<PFN_vkVoidFunction>(StubDebugMarkerSetObjectNameEXT);
    }

    if (name == "vkCreateImage") return reinterpret_cast<PFN_vkVoidFunction>(StubCreateImage);
    if (name == "vkDestroyImage") return reinterpret_cast<PFN_vkVoidFunction>(StubDestroyImage);
    if (name == "vkAllocateMemory") return reinterpret_cast<PFN_vkVoidFunction>(StubAllocateMemory);
    if (name == "vkFreeMemory") return reinterpret_cast<PFN_vkVoidFunction>(StubFreeMemory);
    if (name == "vkBindBufferMemory") return reinterpret_cast<PFN_vkVoidFunction>(StubBindBufferMemory);
    if (name == "vkBindImageMemory") return reinterpret_cast<PFN_vkVoidFunction>(StubBindImageMemory);
    if (name == "vkBindBufferMemory2" || name == "vkBindBufferMemory2KHR") {
        return reinterpret_cast<PFN_vkVoidFunction>(StubBindBufferMemory2);
    }
    if (name == "vkBindImageMemory2" || name == "vkBindImageMemory2KHR") {
        return reinterpret_cast<PFN_vkVoidFunction>(StubBindImageMemory2);
    }
    if (name == "vkGetBufferMemoryRequirements") return reinterpret_cast<PFN_vkVoidFunction>(StubGetBufferMemoryRequirements);
    if (name == "vkGetImageMemoryRequirements") return reinterpret_cast<PFN_vkVoidFunction>(StubGetImageMemoryRequirements);

    // Everything else is not implemented by the stub driver.
    return nullptr;
}

// A dispatchable Vulkan object begins with a pointer to its dispatch table, and the layer uses that
// pointer as the key into its own dispatch table map. This fake device therefore just points at
// itself, which also gives every instance a distinct key.
class FakeDevice {
   public:
    FakeDevice() {
        dispatch_key_ = this;
        initDeviceTable(handle(), StubGetDeviceProcAddr);
    }

    ~FakeDevice() { destroy_device_dispatch_table(get_dispatch_key(handle())); }

    FakeDevice(const FakeDevice&) = delete;
    FakeDevice& operator=(const FakeDevice&) = delete;

    VkDevice handle() { return reinterpret_cast<VkDevice>(this); }

   private:
    void* dispatch_key_ = nullptr;
};

class DeviceMemoryReportDispatchTests : public ::testing::Test {
   protected:
    void SetUp() override {
        DeviceMemoryReport::Get().Reset();
        layersvt::VulkanObjectNames::Get().Clear();
        g_buffer_requirements_size = 0;
        g_image_requirements_size = 0;
        g_buffer_requirements_queries = 0;
        g_image_requirements_queries = 0;
        g_supports_object_names = false;
        g_set_debug_utils_object_name_calls = 0;
        g_debug_marker_set_object_name_calls = 0;
    }

    void TearDown() override {
        DeviceMemoryReport::Get().Reset();
        layersvt::VulkanObjectNames::Get().Clear();
        g_supports_object_names = false;
    }
};

TEST_F(DeviceMemoryReportDispatchTests, BindBufferMemoryQueriesUnknownResourceSize) {
    // A buffer whose size was never recorded (for example when the application created it before
    // the layer was active) must have its size queried from the driver at bind time, otherwise the
    // suballocation cannot be sized and the binding is dropped.
    FakeDevice device;
    VkBuffer buffer = MakeHandle<VkBuffer>(0xB1000);
    VkDeviceMemory memory = MakeHandle<VkDeviceMemory>(0xB1001);

    ASSERT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(buffer)), 0u);

    g_buffer_requirements_size = 4096;
    EXPECT_EQ(vkBindBufferMemory(device.handle(), buffer, memory, 0), VK_SUCCESS);

    EXPECT_EQ(g_buffer_requirements_queries, 1);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(buffer)), 4096u);
}

TEST_F(DeviceMemoryReportDispatchTests, BindImageMemoryQueriesUnknownResourceSize) {
    FakeDevice device;
    VkImage image = MakeHandle<VkImage>(0xB2000);
    VkDeviceMemory memory = MakeHandle<VkDeviceMemory>(0xB2001);

    ASSERT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image)), 0u);

    g_image_requirements_size = 65536;
    EXPECT_EQ(vkBindImageMemory(device.handle(), image, memory, 0), VK_SUCCESS);

    EXPECT_EQ(g_image_requirements_queries, 1);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image)), 65536u);
}

TEST_F(DeviceMemoryReportDispatchTests, BindMemory2QueriesUnknownResourceSizes) {
    FakeDevice device;
    VkDeviceMemory memory = MakeHandle<VkDeviceMemory>(0xB3000);

    VkBuffer buffer = MakeHandle<VkBuffer>(0xB3001);
    VkBuffer buffer_khr = MakeHandle<VkBuffer>(0xB3002);
    VkImage image = MakeHandle<VkImage>(0xB3003);
    VkImage image_khr = MakeHandle<VkImage>(0xB3004);

    g_buffer_requirements_size = 1024;
    g_image_requirements_size = 8192;

    VkBindBufferMemoryInfo buffer_bind = {};
    buffer_bind.sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO;
    buffer_bind.buffer = buffer;
    buffer_bind.memory = memory;
    buffer_bind.memoryOffset = 0;
    EXPECT_EQ(vkBindBufferMemory2(device.handle(), 1, &buffer_bind), VK_SUCCESS);

    buffer_bind.buffer = buffer_khr;
    buffer_bind.memoryOffset = 1024;
    EXPECT_EQ(vkBindBufferMemory2KHR(device.handle(), 1, &buffer_bind), VK_SUCCESS);

    VkBindImageMemoryInfo image_bind = {};
    image_bind.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    image_bind.image = image;
    image_bind.memory = memory;
    image_bind.memoryOffset = 8192;
    EXPECT_EQ(vkBindImageMemory2(device.handle(), 1, &image_bind), VK_SUCCESS);

    image_bind.image = image_khr;
    image_bind.memoryOffset = 16384;
    EXPECT_EQ(vkBindImageMemory2KHR(device.handle(), 1, &image_bind), VK_SUCCESS);

    EXPECT_EQ(g_buffer_requirements_queries, 2);
    EXPECT_EQ(g_image_requirements_queries, 2);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(buffer)), 1024u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(buffer_khr)), 1024u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image)), 8192u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image_khr)), 8192u);
}

TEST_F(DeviceMemoryReportDispatchTests, BindMemoryKeepsAlreadyRecordedSizes) {
    // Sizes that are already known must not be re-queried or overwritten at bind time.
    FakeDevice device;
    VkBuffer buffer = MakeHandle<VkBuffer>(0xB4000);
    VkImage image = MakeHandle<VkImage>(0xB4001);
    VkDeviceMemory memory = MakeHandle<VkDeviceMemory>(0xB4002);

    DeviceMemoryReport::Get().OnRecordResourceSize(AsObjectHandle(buffer), 2048);
    DeviceMemoryReport::Get().OnRecordResourceSize(AsObjectHandle(image), 4096);

    g_buffer_requirements_size = 999;
    g_image_requirements_size = 999;

    EXPECT_EQ(vkBindBufferMemory(device.handle(), buffer, memory, 0), VK_SUCCESS);
    EXPECT_EQ(vkBindImageMemory(device.handle(), image, memory, 2048), VK_SUCCESS);

    EXPECT_EQ(g_buffer_requirements_queries, 0);
    EXPECT_EQ(g_image_requirements_queries, 0);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(buffer)), 2048u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image)), 4096u);
}

TEST_F(DeviceMemoryReportDispatchTests, BindImageMemory2SkipsDisjointImagePlaneBind) {
    // When binding an image plane with VkBindImagePlaneMemoryInfo in the pNext chain (even if not first),
    // vkGetImageMemoryRequirements must not be queried because disjoint images require
    // vkGetImageMemoryRequirements2 with plane aspect specified.
    FakeDevice device;
    VkImage image = MakeHandle<VkImage>(0xB4800);
    VkDeviceMemory memory = MakeHandle<VkDeviceMemory>(0xB4801);

    ASSERT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image)), 0u);

    VkBindImagePlaneMemoryInfo plane_info = {};
    plane_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
    plane_info.pNext = nullptr;
    plane_info.planeAspect = VK_IMAGE_ASPECT_PLANE_0_BIT;

    VkBindImageMemoryDeviceGroupInfo device_group_info = {};
    device_group_info.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_DEVICE_GROUP_INFO;
    device_group_info.pNext = &plane_info;

    VkBindImageMemoryInfo image_bind = {};
    image_bind.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
    image_bind.pNext = &device_group_info;
    image_bind.image = image;
    image_bind.memory = memory;
    image_bind.memoryOffset = 0;

    EXPECT_EQ(vkBindImageMemory2(device.handle(), 1, &image_bind), VK_SUCCESS);

    EXPECT_EQ(g_image_requirements_queries, 0);
    EXPECT_EQ(DeviceMemoryReport::Get().GetRecordedResourceSize(AsObjectHandle(image)), 0u);
}

TEST_F(DeviceMemoryReportDispatchTests, ObjectNameEntryPointsHiddenWhenDriverLacksSupport) {
    // The layer does not claim VK_EXT_debug_utils or VK_EXT_debug_marker. Handing out an
    // interceptor for a command the driver does not implement would make the application believe
    // the extension is available.
    g_supports_object_names = false;
    FakeDevice device;

    EXPECT_EQ(vkGetDeviceProcAddr(device.handle(), "vkSetDebugUtilsObjectNameEXT"), nullptr);
    EXPECT_EQ(vkGetDeviceProcAddr(device.handle(), "vkDebugMarkerSetObjectNameEXT"), nullptr);
}

TEST_F(DeviceMemoryReportDispatchTests, ObjectNameEntryPointsInterceptedWhenDriverSupportsThem) {
    g_supports_object_names = true;
    FakeDevice device;

    PFN_vkVoidFunction debug_utils = vkGetDeviceProcAddr(device.handle(), "vkSetDebugUtilsObjectNameEXT");
    PFN_vkVoidFunction debug_marker = vkGetDeviceProcAddr(device.handle(), "vkDebugMarkerSetObjectNameEXT");

    // The layer's own interceptors must be returned, not the driver's implementations.
    EXPECT_EQ(debug_utils, reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT));
    EXPECT_EQ(debug_marker, reinterpret_cast<PFN_vkVoidFunction>(vkDebugMarkerSetObjectNameEXT));
}

TEST_F(DeviceMemoryReportDispatchTests, SetDebugUtilsObjectNameRecordsAndForwards) {
    g_supports_object_names = true;
    FakeDevice device;
    VkBuffer buffer = MakeHandle<VkBuffer>(0xC1000);

    VkDebugUtilsObjectNameInfoEXT name_info = {};
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    name_info.objectType = VK_OBJECT_TYPE_BUFFER;
    name_info.objectHandle = AsObjectHandle(buffer);
    name_info.pObjectName = "VertexBuffer";

    EXPECT_EQ(vkSetDebugUtilsObjectNameEXT(device.handle(), &name_info), VK_SUCCESS);

    EXPECT_TRUE(layersvt::VulkanObjectNames::Get().HasObjectName(VK_OBJECT_TYPE_BUFFER, AsObjectHandle(buffer), "VertexBuffer"));
    // The application's call must still reach the driver.
    EXPECT_EQ(g_set_debug_utils_object_name_calls, 1);
}

TEST_F(DeviceMemoryReportDispatchTests, DebugMarkerSetObjectNameRecordsAndForwards) {
    g_supports_object_names = true;
    FakeDevice device;
    VkImage image = MakeHandle<VkImage>(0xC2000);

    VkDebugMarkerObjectNameInfoEXT name_info = {};
    name_info.sType = VK_STRUCTURE_TYPE_DEBUG_MARKER_OBJECT_NAME_INFO_EXT;
    name_info.objectType = VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT;
    name_info.object = AsObjectHandle(image);
    name_info.pObjectName = "ShadowMap";

    EXPECT_EQ(vkDebugMarkerSetObjectNameEXT(device.handle(), &name_info), VK_SUCCESS);

    // Stored against the modern object type, exactly as the DebugMarker layer would store it.
    EXPECT_TRUE(layersvt::VulkanObjectNames::Get().HasObjectName(VK_OBJECT_TYPE_IMAGE, AsObjectHandle(image), "ShadowMap"));
    EXPECT_EQ(g_debug_marker_set_object_name_calls, 1);
}

}  // namespace

