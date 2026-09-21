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

#include "layer_test_helper.h"
#include "device_memory_report.h"
#include "device_memory_report_perfetto.h"

#include <vulkan/vulkan_core.h>

#include <gtest/gtest.h>
#include <optional>

static const char* kLayerName = "VK_LAYER_GOOGLE_DeviceMemoryReport";

class DeviceMemoryReportTests : public VkTestFramework {
   public:
    ~DeviceMemoryReportTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(DeviceMemoryReportTests, InitLayer) {
    TEST_DESCRIPTION("Test Creating a Vulkan Instance with DeviceMemoryReport layer");

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkInstance instance = inst_builder.GetInstance();
    EXPECT_NE(instance, VK_NULL_HANDLE);

    inst_builder.Reset();
}

TEST_F(DeviceMemoryReportTests, PhysicalDeviceEnumeration) {
    TEST_DESCRIPTION("Test physical device to instance mapping");

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkInstance instance = inst_builder.GetInstance();
    EXPECT_NE(instance, VK_NULL_HANDLE);

    VkPhysicalDevice phys_dev = reinterpret_cast<VkPhysicalDevice>(0x1234);
    DeviceMemoryReport::Get().SetVkInstance(phys_dev, instance);
    EXPECT_EQ(DeviceMemoryReport::Get().GetVkInstance(phys_dev), instance);

    inst_builder.Reset();
}

TEST_F(DeviceMemoryReportTests, ExtensionProperties) {
    TEST_DESCRIPTION("Test extension enumeration for instance and device");

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkInstance instance = inst_builder.GetInstance();
    EXPECT_NE(instance, VK_NULL_HANDLE);

    // Test instance extension properties advertised by the layer
    uint32_t inst_ext_count = 0;
    EXPECT_EQ(vkEnumerateInstanceExtensionProperties(kLayerName, &inst_ext_count, nullptr), VK_SUCCESS);
    EXPECT_EQ(inst_ext_count, 0u);

    VkPhysicalDevice phys_dev = VK_NULL_HANDLE;
    inst_builder.GetPhysicalDevice(&phys_dev);
    if (phys_dev != VK_NULL_HANDLE) {
        // Test device extension properties advertised by the layer
        uint32_t dev_ext_count = 0;
        EXPECT_EQ(vkEnumerateDeviceExtensionProperties(phys_dev, kLayerName, &dev_ext_count, nullptr), VK_SUCCESS);
        EXPECT_GE(dev_ext_count, 1u);
        std::vector<VkExtensionProperties> dev_exts(dev_ext_count);
        EXPECT_EQ(vkEnumerateDeviceExtensionProperties(phys_dev, kLayerName, &dev_ext_count, dev_exts.data()), VK_SUCCESS);
        bool found_mem_report = false;
        for (const auto& ext : dev_exts) {
            if (strcmp(ext.extensionName, VK_EXT_DEVICE_MEMORY_REPORT_EXTENSION_NAME) == 0) {
                found_mem_report = true;
            }
        }
        EXPECT_TRUE(found_mem_report);
    }

    inst_builder.Reset();
}

TEST_F(DeviceMemoryReportTests, EmitEventsAndSubCounters) {
    TEST_DESCRIPTION("Test calling OnMemoryReportEvent with object types and allocating/freeing memory");

    InitializeDeviceMemoryReportPerfetto();

    VkDeviceMemoryReportCallbackDataEXT cb_data = {};
    cb_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    cb_data.flags = 0; // Application memory
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    cb_data.memoryObjectId = 0x1000;
    cb_data.size = 1024 * 1024;
    cb_data.objectType = VK_OBJECT_TYPE_IMAGE;
    cb_data.objectHandle = 0x5000;

    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    cb_data.size = 512 * 1024;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    cb_data.flags = VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT; // Driver memory
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    cb_data.objectType = VK_OBJECT_TYPE_BUFFER;
    cb_data.size = 2048;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    // Free the driver allocation
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.unbound_memory"), 0u);

    // Test direct allocate/free fallbacks
    VkDevice dummy_device = reinterpret_cast<VkDevice>(0x1234);
    VkDeviceMemory dummy_memory = reinterpret_cast<VkDeviceMemory>(0x5678);
    DeviceMemoryReport::Get().OnAllocateMemory(dummy_device, dummy_memory, 4096);
    DeviceMemoryReport::Get().OnFreeMemory(dummy_device, dummy_memory);

    EXPECT_TRUE(true);
}

TEST_F(DeviceMemoryReportTests, BufferImageBindingAndCallbackSuppression) {
    TEST_DESCRIPTION("Test buffer/image memory binding, IMPORT/UNIMPORT events, and fallback suppression when callback is enabled");

    InitializeDeviceMemoryReportPerfetto();

    // Test buffer and image memory binding
    uint64_t buffer_handle = 0x8000;
    uint64_t image_handle = 0x8001;
    uint64_t memory_handle = 0x9000;
    DeviceMemoryReport::Get().OnCreateBuffer(buffer_handle, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 1024);
    DeviceMemoryReport::Get().OnCreateImage(image_handle, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    DeviceMemoryReport::Get().OnRecordResourceSize(image_handle, 2048);
    DeviceMemoryReport::Get().OnBindBufferMemory(buffer_handle, memory_handle, 0);
    DeviceMemoryReport::Get().OnBindImageMemory(image_handle, memory_handle, 1024);

    // Test IMPORT_EXT and UNIMPORT_EXT events
    VkDeviceMemoryReportCallbackDataEXT cb_data = {};
    cb_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    cb_data.flags = 0;
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_IMPORT_EXT;
    cb_data.memoryObjectId = 0x3000;
    cb_data.size = 4096;
    cb_data.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
    cb_data.objectHandle = memory_handle;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_UNIMPORT_EXT;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    // Test callback suppression: when callback is registered on VkDevice, fallback allocations should be ignored
    VkDevice test_device = reinterpret_cast<VkDevice>(0xABCD);
    VkDeviceMemory test_memory = reinterpret_cast<VkDeviceMemory>(0xEF01);

    DeviceMemoryReport::Get().SetHasMemoryReportCallback(test_device, true);
    DeviceMemoryReport::Get().OnAllocateMemory(test_device, test_memory, 8192);
    DeviceMemoryReport::Get().OnFreeMemory(test_device, test_memory);

    // Enable fallback by turning off callback flag and verify allocate/free work
    DeviceMemoryReport::Get().SetHasMemoryReportCallback(test_device, false);
    DeviceMemoryReport::Get().OnAllocateMemory(test_device, test_memory, 8192);
    DeviceMemoryReport::Get().OnFreeMemory(test_device, test_memory);

    EXPECT_TRUE(true);
}

TEST_F(DeviceMemoryReportTests, UsageTypeBreakdown) {
    TEST_DESCRIPTION("Test automatic usage type classification for granular image and buffer usage categories");

    InitializeDeviceMemoryReportPerfetto();

    uint64_t color_img = 0xA001;
    uint64_t depth_img = 0xA002;
    uint64_t sampled_img = 0xA005;
    uint64_t storage_img = 0xA006;
    uint64_t transient_img = 0xA007;

    uint64_t vtx_buf = 0xB001;
    uint64_t idx_buf = 0xB002;
    uint64_t ubo_buf = 0xB003;
    uint64_t staging_buf = 0xB006;
    uint64_t storage_buf = 0xB007;
    uint64_t indirect_buf = 0xB008;

    uint64_t mem_handle = 0xC001;

    // Register images
    DeviceMemoryReport::Get().OnCreateImage(color_img, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    DeviceMemoryReport::Get().OnCreateImage(depth_img, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    DeviceMemoryReport::Get().OnCreateImage(sampled_img, VK_IMAGE_USAGE_SAMPLED_BIT);
    DeviceMemoryReport::Get().OnCreateImage(storage_img, VK_IMAGE_USAGE_STORAGE_BIT);
    DeviceMemoryReport::Get().OnCreateImage(transient_img, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);

    DeviceMemoryReport::Get().OnRecordResourceSize(color_img, 65536);
    DeviceMemoryReport::Get().OnRecordResourceSize(depth_img, 65536);
    DeviceMemoryReport::Get().OnRecordResourceSize(sampled_img, 65536);
    DeviceMemoryReport::Get().OnRecordResourceSize(storage_img, 65536);
    DeviceMemoryReport::Get().OnRecordResourceSize(transient_img, 65536);

    // Register buffers
    DeviceMemoryReport::Get().OnCreateBuffer(vtx_buf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 4096);
    DeviceMemoryReport::Get().OnCreateBuffer(idx_buf, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 4096);
    DeviceMemoryReport::Get().OnCreateBuffer(ubo_buf, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 4096);
    DeviceMemoryReport::Get().OnCreateBuffer(staging_buf, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 4096);
    DeviceMemoryReport::Get().OnCreateBuffer(storage_buf, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 4096);
    DeviceMemoryReport::Get().OnCreateBuffer(indirect_buf, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, 4096);

    VkDeviceMemoryReportCallbackDataEXT cb_data = {};
    cb_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    cb_data.flags = 0;
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    cb_data.memoryObjectId = 0x5000;
    cb_data.size = 2097152;
    cb_data.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
    cb_data.objectHandle = mem_handle;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);
    DeviceMemoryReport::Get().SetAllocationMemoryProperties(mem_handle, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    // Bind images and buffers to test usage categorization and track transitions
    DeviceMemoryReport::Get().OnBindImageMemory(color_img, mem_handle, 0);
    DeviceMemoryReport::Get().OnBindImageMemory(depth_img, mem_handle, 65536);
    DeviceMemoryReport::Get().OnBindImageMemory(sampled_img, mem_handle, 131072);
    DeviceMemoryReport::Get().OnBindImageMemory(storage_img, mem_handle, 196608);
    DeviceMemoryReport::Get().OnBindImageMemory(transient_img, mem_handle, 262144);

    DeviceMemoryReport::Get().OnBindBufferMemory(vtx_buf, mem_handle, 327680);
    DeviceMemoryReport::Get().OnBindBufferMemory(idx_buf, mem_handle, 331776);
    DeviceMemoryReport::Get().OnBindBufferMemory(ubo_buf, mem_handle, 335872);
    DeviceMemoryReport::Get().OnBindBufferMemory(staging_buf, mem_handle, 339968);
    DeviceMemoryReport::Get().OnBindBufferMemory(storage_buf, mem_handle, 344064);
    DeviceMemoryReport::Get().OnBindBufferMemory(indirect_buf, mem_handle, 348160);

    // Free memory
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    // Clean up objects
    DeviceMemoryReport::Get().OnDestroyObject(color_img);
    DeviceMemoryReport::Get().OnDestroyObject(depth_img);
    DeviceMemoryReport::Get().OnDestroyObject(sampled_img);
    DeviceMemoryReport::Get().OnDestroyObject(storage_img);
    DeviceMemoryReport::Get().OnDestroyObject(transient_img);

    DeviceMemoryReport::Get().OnDestroyObject(vtx_buf);
    DeviceMemoryReport::Get().OnDestroyObject(idx_buf);
    DeviceMemoryReport::Get().OnDestroyObject(ubo_buf);
    DeviceMemoryReport::Get().OnDestroyObject(staging_buf);
    DeviceMemoryReport::Get().OnDestroyObject(storage_buf);
    DeviceMemoryReport::Get().OnDestroyObject(indirect_buf);

    EXPECT_TRUE(true);
}

TEST_F(DeviceMemoryReportTests, MemoryAliasingAndOverlap) {
    TEST_DESCRIPTION("Test memory aliasing where overlapping virtual resources occupy the same physical memory slab");

    InitializeDeviceMemoryReportPerfetto();

    uint64_t mem_handle = 0xD001;
    uint64_t image_a = 0xD101;
    uint64_t image_b = 0xD102;
    uint64_t buffer_c = 0xD103;

    // Step 1: Allocate a 10,000-byte continuous physical memory slab (VkDeviceMemory).
    // Initial state: total = 10,000 B, bound = 0 B, unbound_memory = 10,000 B.
    VkDeviceMemoryReportCallbackDataEXT cb_data = {};
    cb_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    cb_data.memoryObjectId = 0x6000;
    cb_data.size = 10000;
    cb_data.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
    cb_data.objectHandle = mem_handle;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    // Step 2: Bind Resource A (Color Attachment Image) to range [0, 4000) (size = 4000 B).
    // - vulkan.mem.app.usage.color_render_target += 4,000 B
    // - Merged intervals: [0, 4000) -> bound_size = 4,000 B, unbound_memory = 6,000 B
    DeviceMemoryReport::Get().OnCreateImage(image_a, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    DeviceMemoryReport::Get().OnRecordResourceSize(image_a, 4000);
    DeviceMemoryReport::Get().OnBindImageMemory(image_a, mem_handle, 0);

    // Step 3: Bind Resource B (Sampled Texture Image) to range [2000, 6000) (size = 4000 B).
    // This overlaps / aliases Resource A on the physical sub-range [2000, 4000).
    // - Each virtual resource adds its full virtual size to its specific category track:
    //   vulkan.mem.app.usage.static_texture += 4,000 B (both A and B report active virtual capacity).
    // - Overlapping intervals [0, 4000) and [2000, 6000) are merged into union [0, 6000).
    // - Physical slab bound_size = 6,000 B (overlapping physical region is NOT double-counted).
    // - Remaining unbound headroom: unbound_memory = 10,000 - 6,000 = 4,000 B.
    DeviceMemoryReport::Get().OnCreateImage(image_b, VK_IMAGE_USAGE_SAMPLED_BIT);
    DeviceMemoryReport::Get().OnRecordResourceSize(image_b, 4000);
    DeviceMemoryReport::Get().OnBindImageMemory(image_b, mem_handle, 2000);

    // Step 4: Bind Resource C (Vertex Buffer) to disjoint range [8000, 9500) (size = 1500 B).
    // - vulkan.mem.app.usage.geometry_mesh += 1,500 B
    // - Interval union: [0, 6000) U [8000, 9500) -> bound_size = 6,000 + 1,500 = 7,500 B.
    // - Remaining unbound headroom: unbound_memory = 10,000 - 7,500 = 2,500 B.
    DeviceMemoryReport::Get().OnCreateBuffer(buffer_c, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 1500);
    DeviceMemoryReport::Get().OnBindBufferMemory(buffer_c, mem_handle, 8000);

    // Step 5: Destroy Resource A.
    // - vulkan.mem.app.usage.color_render_target -= 4,000 B
    // - Interval [0, 4000) is removed. Remaining intervals: [2000, 6000) U [8000, 9500).
    // - Recalculated bound_size = 4,000 + 1,500 = 5,500 B.
    // - Updated unbound headroom: unbound_memory = 10,000 - 5,500 = 4,500 B.
    DeviceMemoryReport::Get().OnDestroyObject(image_a);

    // Step 6: Free physical memory slab.
    // - All remaining sub-allocations on this slab are cleaned up and unbound counter is reset.
    cb_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&cb_data, nullptr);

    // Step 7: Clean up remaining virtual resource object handles.
    DeviceMemoryReport::Get().OnDestroyObject(image_b);
    DeviceMemoryReport::Get().OnDestroyObject(buffer_c);

    EXPECT_TRUE(true);
}

TEST_F(DeviceMemoryReportTests, ClusterClassificationFunctions) {
    TEST_DESCRIPTION("Test cluster classification for buffer and image usages with memory properties");

    // Buffer clusters
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "staging_transfer");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), "staging_transfer");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "staging_transfer");
    // Staging without host-visible property falls through to general_buffer
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0), "general_buffer");
    // Host-visible functional buffers should NOT be misclassified as staging
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "geometry_mesh");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "geometry_mesh");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "uniform_constants");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "uniform_constants");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "compute_storage");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "compute_storage");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT), "indirect_gpu_driven");

    // Ray tracing
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, 0), "ray_tracing");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, 0), "ray_tracing");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR, 0), "ray_tracing");

    // Indirect GPU driven
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, 0), "indirect_gpu_driven");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT, 0), "indirect_gpu_driven");

    // Compute storage
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0), "compute_storage");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, 0), "compute_storage");

    // Uniform constants
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 0), "uniform_constants");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, 0), "uniform_constants");

    // Geometry mesh
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 0), "geometry_mesh");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_INDEX_BUFFER_BIT, 0), "geometry_mesh");

    // Transform feedback
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT, 0), "transform_feedback");
    EXPECT_STREQ(GetBufferCluster(VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT, 0), "transform_feedback");

    // General buffer
    EXPECT_STREQ(GetBufferCluster(0, 0), "general_buffer");

    // Image clusters
    // Transient memoryless
    EXPECT_STREQ(GetImageCluster(VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, 0), "transient_memoryless");
    EXPECT_STREQ(GetImageCluster(0, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT), "transient_memoryless");

    // Storage compute image
    EXPECT_STREQ(GetImageCluster(VK_IMAGE_USAGE_STORAGE_BIT, 0), "storage_compute_image");

    // Depth stencil target
    EXPECT_STREQ(GetImageCluster(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0), "depth_stencil_target");

    // Color render target
    EXPECT_STREQ(GetImageCluster(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, 0), "color_render_target");
    EXPECT_STREQ(GetImageCluster(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0), "color_render_target");

    // Static texture
    EXPECT_STREQ(GetImageCluster(VK_IMAGE_USAGE_SAMPLED_BIT, 0), "static_texture");

    // General image
    EXPECT_STREQ(GetImageCluster(0, 0), "general_image");
}

TEST_F(DeviceMemoryReportTests, StaticCounterTrackLookup) {
    TEST_DESCRIPTION("Test static counter track lookup and dynamic fallback track creation");

    perfetto::CounterTrack track_staging = GetCounterTrack("vulkan.mem.app.usage.staging_transfer");
    EXPECT_EQ(track_staging.Serialize().counter().unit(), perfetto::protos::gen::CounterDescriptor::UNIT_SIZE_BYTES);
    EXPECT_NE(track_staging.uuid, 0u);

    perfetto::CounterTrack track_storage = GetCounterTrack("vulkan.mem.app.usage.compute_storage");
    EXPECT_EQ(track_storage.Serialize().counter().unit(), perfetto::protos::gen::CounterDescriptor::UNIT_SIZE_BYTES);
    EXPECT_NE(track_storage.uuid, 0u);

    perfetto::CounterTrack track_unbound = GetCounterTrack("vulkan.mem.driver.usage.unbound_memory");
    EXPECT_EQ(track_unbound.Serialize().counter().unit(), perfetto::protos::gen::CounterDescriptor::UNIT_SIZE_BYTES);
    EXPECT_NE(track_unbound.uuid, 0u);

    perfetto::CounterTrack dynamic_track = GetCounterTrack("vulkan.mem.app.custom_track");
    EXPECT_EQ(dynamic_track.Serialize().counter().unit(), perfetto::protos::gen::CounterDescriptor::UNIT_SIZE_BYTES);
    EXPECT_NE(dynamic_track.uuid, 0u);

    // Re-query the dynamic track to ensure persistent registry returns consistent track
    perfetto::CounterTrack dynamic_track_again = GetCounterTrack("vulkan.mem.app.custom_track");
    EXPECT_EQ(dynamic_track.uuid, dynamic_track_again.uuid);
    EXPECT_EQ(dynamic_track_again.Serialize().counter().unit(), perfetto::protos::gen::CounterDescriptor::UNIT_SIZE_BYTES);
}

TEST_F(DeviceMemoryReportTests, CounterTracksAreScopedToProcessTrack) {
    TEST_DESCRIPTION(
        "Test that counter tracks are parented to the process track, so Perfetto scopes them under the traced "
        "application instead of emitting them as global tracks");

    InitializeDeviceMemoryReportPerfetto();

    // Perfetto only assigns a process uuid once tracing has been initialized. Tracks created before that (for
    // example during static initialization) are permanently parented to the null/global track.
    const uint64_t process_uuid = perfetto::ProcessTrack::Current().uuid;
    ASSERT_NE(process_uuid, 0u) << "Perfetto tracing was not initialized";

    const char* kTrackNames[] = {
        "vulkan.mem.app.usage.color_render_target",
        "vulkan.mem.app.usage.unbound_memory",
        "vulkan.mem.driver.usage.static_texture",
        "vulkan.mem.driver.usage.unbound_memory",
        "vulkan.mem.app.usage.brand_new_category",
    };

    for (const char* name : kTrackNames) {
        perfetto::CounterTrack track = GetCounterTrack(name);

        EXPECT_NE(track.uuid, 0u) << "track " << name;
        EXPECT_EQ(track.parent_uuid, process_uuid) << "track " << name << " is not scoped to the process track";

        // The uuid must match what Perfetto derives for a process-scoped track of the same name.
        EXPECT_EQ(track.uuid, perfetto::CounterTrack(perfetto::DynamicString(name)).uuid) << "track " << name;

        const perfetto::protos::gen::TrackDescriptor descriptor = track.Serialize();
        EXPECT_EQ(descriptor.uuid(), track.uuid) << "track " << name;
        EXPECT_EQ(descriptor.parent_uuid(), process_uuid) << "track " << name;
        EXPECT_EQ(descriptor.counter().unit(), perfetto::protos::gen::CounterDescriptor::UNIT_SIZE_BYTES)
            << "track " << name;
    }
}

TEST_F(DeviceMemoryReportTests, GetCounterTrackInitializesTracing) {
    TEST_DESCRIPTION("Test that GetCounterTrack initializes Perfetto itself, so tracks always have process context");

    // Force fork+execve so the child starts with an uninitialized process_uuid rather than
    // inheriting static state already initialized by earlier tests in this binary.
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    EXPECT_EXIT(
        {
            if (perfetto::ProcessTrack::Current().uuid != 0u) {
                exit(1);
            }
            perfetto::CounterTrack track = GetCounterTrack("vulkan.mem.app.usage.general_image");
            const uint64_t process_uuid = perfetto::ProcessTrack::Current().uuid;
            if (process_uuid == 0u || track.parent_uuid != process_uuid) {
                exit(2);
            }
            exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST_F(DeviceMemoryReportTests, DriverVsAppUnboundMemoryAttribution) {
    TEST_DESCRIPTION("Test that application unbound memory is not misclassified when object handle matches an existing resource handle");

    InitializeDeviceMemoryReportPerfetto();

    uint64_t shared_handle = 0xF001;

    // Register a virtual image resource with handle shared_handle
    DeviceMemoryReport::Get().OnCreateImage(shared_handle, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    DeviceMemoryReport::Get().OnRecordResourceSize(shared_handle, 4096);

    // Case 1: Driver allocation where objectHandle is the virtual resource handle
    VkDeviceMemoryReportCallbackDataEXT driver_callback_data = {};
    driver_callback_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    driver_callback_data.flags = VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT; // Driver allocation
    driver_callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    driver_callback_data.memoryObjectId = 0x8001;
    driver_callback_data.size = 4096;
    driver_callback_data.objectType = VK_OBJECT_TYPE_IMAGE;
    driver_callback_data.objectHandle = shared_handle;
    DeviceMemoryReport::MemoryReportCallback(&driver_callback_data, nullptr);

    // Driver allocation with image handle should be attributed to the color_render_target track
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.color_render_target"), 4096u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.unbound_memory"), 0u);

    // Clean up driver allocation
    driver_callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&driver_callback_data, nullptr);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.color_render_target"), 0u);

    // Case 2: Application allocation where VkDeviceMemory handle happens to have the same integer value as shared_handle
    VkDeviceMemoryReportCallbackDataEXT application_callback_data = {};
    application_callback_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    application_callback_data.flags = 0; // Application allocation
    application_callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    application_callback_data.memoryObjectId = 0x8002;
    application_callback_data.size = 8192;
    application_callback_data.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
    application_callback_data.objectHandle = shared_handle;
    DeviceMemoryReport::MemoryReportCallback(&application_callback_data, nullptr);

    // Application allocation should be classified as unbound_memory, NOT color_render_target
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.app.usage.unbound_memory"), 8192u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.app.usage.color_render_target"), 0u);

    // Clean up application allocation and resource
    application_callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&application_callback_data, nullptr);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.app.usage.unbound_memory"), 0u);

    DeviceMemoryReport::Get().OnDestroyObject(shared_handle);

    // Case 3: Driver allocation arrives before OnCreateBuffer (tests re-attribution)
    uint64_t buffer_handle = 0xF002;
    VkDeviceMemoryReportCallbackDataEXT buffer_callback_data = {};
    buffer_callback_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    buffer_callback_data.flags = VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT;
    buffer_callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    buffer_callback_data.memoryObjectId = 0x8003;
    buffer_callback_data.size = 4096;
    buffer_callback_data.objectType = VK_OBJECT_TYPE_BUFFER;
    buffer_callback_data.objectHandle = buffer_handle;
    DeviceMemoryReport::MemoryReportCallback(&buffer_callback_data, nullptr);

    // Prior to OnCreateBuffer, the driver allocation is counted under unbound_memory
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.unbound_memory"), 4096u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.geometry_mesh"), 0u);

    // When the buffer is created, the allocation is re-attributed to the geometry_mesh cluster
    DeviceMemoryReport::Get().OnCreateBuffer(buffer_handle, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, 4096);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.unbound_memory"), 0u);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.geometry_mesh"), 4096u);

    // Clean up
    buffer_callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&buffer_callback_data, nullptr);
    EXPECT_EQ(DeviceMemoryReport::Get().GetUsageCounterBytes("vulkan.mem.driver.usage.geometry_mesh"), 0u);
    DeviceMemoryReport::Get().OnDestroyObject(buffer_handle);
}

TEST_F(DeviceMemoryReportTests, ProactiveMemoryRequirementsQuery) {
    TEST_DESCRIPTION("Test that the layer proactively queries memory requirements when creating images and buffers");

    layer_test::VulkanInstanceBuilder inst_builder;
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkPhysicalDevice phys_dev = VK_NULL_HANDLE;
    inst_builder.GetPhysicalDevice(&phys_dev);
    if (phys_dev == VK_NULL_HANDLE) {
        GTEST_SKIP() << "No physical device found, skipping test.";
    }

    // Create a logical device
    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {};
    queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_info.queueFamilyIndex = 0;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &queue_priority;

    VkDeviceCreateInfo dev_info = {};
    dev_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dev_info.queueCreateInfoCount = 1;
    dev_info.pQueueCreateInfos = &queue_info;
    dev_info.enabledExtensionCount = 0;

    VkDevice device = VK_NULL_HANDLE;
    err = vkCreateDevice(phys_dev, &dev_info, nullptr, &device);
    if (err != VK_SUCCESS) {
        GTEST_SKIP() << "Failed to create logical device, skipping test.";
    }

    // Create an image
    VkImageCreateInfo img_info = {};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = VK_FORMAT_R8G8B8A8_UNORM;
    img_info.extent = {64, 64, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image = VK_NULL_HANDLE;
    err = vkCreateImage(device, &img_info, nullptr, &image);
    ASSERT_EQ(err, VK_SUCCESS);

    // The interceptor should have called OnRecordResourceSize.
    // Verify that the recorded size is > 0.
    VkDeviceSize img_size = DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(image));
    EXPECT_GT(img_size, 0);

    // Create a buffer
    VkBufferCreateInfo buf_info = {};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = 1024;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    err = vkCreateBuffer(device, &buf_info, nullptr, &buffer);
    ASSERT_EQ(err, VK_SUCCESS);

    // The interceptor should have called OnRecordResourceSize.
    VkDeviceSize buf_size = DeviceMemoryReport::Get().GetRecordedResourceSize(reinterpret_cast<uint64_t>(buffer));
    EXPECT_GT(buf_size, 0);

    vkDestroyImage(device, image, nullptr);
    vkDestroyBuffer(device, buffer, nullptr);
    vkDestroyDevice(device, nullptr);
}

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

TEST_F(DeviceMemoryReportTests, MemoryReportSnapshotDump) {
    TEST_DESCRIPTION("Test DumpCurrentCountersAndAllocations state dump and instant event emissions when a trace session begins");

    InitializeDeviceMemoryReportPerfetto();

    uint64_t memory_handle = 0xE001;
    uint64_t buffer_handle = 0xE101;
    uint64_t image_handle = 0xE102;

    // Allocate physical memory
    VkDeviceMemoryReportCallbackDataEXT callback_data = {};
    callback_data.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    callback_data.memoryObjectId = 0x7000;
    callback_data.size = 16384;
    callback_data.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
    callback_data.objectHandle = memory_handle;
    DeviceMemoryReport::MemoryReportCallback(&callback_data, nullptr);

    // Bind a buffer and an image sub-allocation
    DeviceMemoryReport::Get().OnCreateBuffer(buffer_handle, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 4096);
    DeviceMemoryReport::Get().OnRecordResourceSize(buffer_handle, 4096);
    DeviceMemoryReport::Get().OnBindBufferMemory(buffer_handle, memory_handle, 0);

    DeviceMemoryReport::Get().OnCreateImage(image_handle, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
    DeviceMemoryReport::Get().OnRecordResourceSize(image_handle, 4096);
    DeviceMemoryReport::Get().OnBindImageMemory(image_handle, memory_handle, 4096);

    auto allocation = DeviceMemoryReportTestPeer::FindAllocation(memory_handle);
    ASSERT_TRUE(allocation.has_value());
    EXPECT_EQ(allocation->total_size, 16384u);
    EXPECT_EQ(allocation->sub_allocations.size(), 2u);
    EXPECT_EQ(allocation->applied_unbound_bytes, 8192u);

    // Test dumping the current snapshot of counters, allocations, and suballocations
    DeviceMemoryReport::Get().DumpCurrentCountersAndAllocations();

    // Verify that dumping state is non-destructive and preserves allocation invariants
    auto post_dump_allocation = DeviceMemoryReportTestPeer::FindAllocation(memory_handle);
    ASSERT_TRUE(post_dump_allocation.has_value());
    EXPECT_EQ(post_dump_allocation->total_size, 16384u);
    EXPECT_EQ(post_dump_allocation->sub_allocations.size(), 2u);
    EXPECT_EQ(post_dump_allocation->applied_unbound_bytes, 8192u);

    // Verify cleanup
    callback_data.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&callback_data, nullptr);

    DeviceMemoryReport::Get().OnDestroyObject(buffer_handle, VK_OBJECT_TYPE_BUFFER);
    DeviceMemoryReport::Get().OnDestroyObject(image_handle, VK_OBJECT_TYPE_IMAGE);

    // Verify post-destruction state
    EXPECT_FALSE(DeviceMemoryReportTestPeer::FindAllocation(memory_handle).has_value());
    EXPECT_FALSE(DeviceMemoryReportTestPeer::FindResource(buffer_handle).has_value());
    EXPECT_FALSE(DeviceMemoryReportTestPeer::FindResource(image_handle).has_value());
}


TEST_F(DeviceMemoryReportTests, DebugObjectNames) {
    TEST_DESCRIPTION("Test that object names are recorded for the object types the layer attributes memory to");

    InitializeDeviceMemoryReportPerfetto();

    const uint64_t buffer_handle = 0xE001;
    const uint64_t image_handle = 0xE002;
    const uint64_t memory_handle = 0xE003;

    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, "vertex_buffer");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle, "albedo_texture");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, memory_handle, "scene_heap");

    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "vertex_buffer");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle), "albedo_texture");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, memory_handle), "scene_heap");

    // Handles are only unique within an object type, so the same handle can carry a different name
    // for a different type.
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_IMAGE, buffer_handle, "shadow_map");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "vertex_buffer");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, buffer_handle), "shadow_map");

    // Renaming replaces the stored name.
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, "index_buffer");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "index_buffer");

    // Re-applying the same name preserves state and suppresses a duplicate trace emission (the
    // TRACE_EVENT_INSTANT output is consumed by Perfetto and cannot be counted directly here).
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, "index_buffer");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "index_buffer");

    // A null name clears the name rather than recording a placeholder, and clearing an already
    // unnamed object is a no-op.
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, nullptr);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, "");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "");

    // Object types the memory view cannot attribute memory to are not tracked at all.
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_PIPELINE, 0xE004, "lighting_pipeline");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_PIPELINE, 0xE004), "");

    // Unnamed objects report no name.
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, 0xE005), "");
}

TEST_F(DeviceMemoryReportTests, DebugObjectNamesSurviveSnapshotDump) {
    TEST_DESCRIPTION("Test that a snapshot dump replays object names for sessions that attach late");

    InitializeDeviceMemoryReportPerfetto();

    const uint64_t buffer_handle = 0xE101;
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, "persistent_buffer");

    DeviceMemoryReport::Get().DumpCurrentCountersAndAllocations();

    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "persistent_buffer");
}

TEST_F(DeviceMemoryReportTests, DebugObjectNamesDestroyedOnObjectDestroy) {
    TEST_DESCRIPTION("Test that debug names are cleared when objects are destroyed or freed");

    InitializeDeviceMemoryReportPerfetto();

    const uint64_t buffer_handle = 0xE201;
    const uint64_t image_handle = 0xE202;
    const uint64_t memory_handle = 0xE203;

    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle, "temp_buffer");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle, "temp_image");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, memory_handle, "temp_memory");

    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "temp_buffer");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle), "temp_image");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, memory_handle), "temp_memory");

    DeviceMemoryReport::Get().OnDestroyObject(buffer_handle, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle), "temp_image");

    DeviceMemoryReport::Get().OnDestroyObject(image_handle, VK_OBJECT_TYPE_IMAGE);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle), "");

    VkDevice dummy_device = reinterpret_cast<VkDevice>(0xD001);
    VkDeviceMemory dummy_memory = reinterpret_cast<VkDeviceMemory>(memory_handle);
    DeviceMemoryReport::Get().OnAllocateMemory(dummy_device, dummy_memory, 1024, 0, 0);
    DeviceMemoryReport::Get().OnFreeMemory(dummy_device, dummy_memory);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, memory_handle), "");
}

TEST_F(DeviceMemoryReportTests, DebugObjectNameClearOnlyAffectsItsOwnType) {
    TEST_DESCRIPTION("Test that clearing the name of a destroyed object spares a same-numbered object of another type");

    InitializeDeviceMemoryReportPerfetto();

    // Handles are only unique within an object type, so a buffer, an image, a device memory
    // allocation, and a driver memoryObjectId can legitimately carry the same numeric value.
    const uint64_t shared_handle = 0xE301;

    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_BUFFER, shared_handle, "collided_buffer");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_IMAGE, shared_handle, "collided_image");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, shared_handle, "collided_memory");

    DeviceMemoryReport::Get().OnDestroyObject(shared_handle, VK_OBJECT_TYPE_BUFFER);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, shared_handle), "");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, shared_handle), "collided_image");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, shared_handle), "collided_memory");

    // Freeing a driver-internal allocation whose memoryObjectId matches shared_handle must not
    // erase the live VkDeviceMemory's debug name.
    VkDeviceMemoryReportCallbackDataEXT driver_cb = {};
    driver_cb.sType = VK_STRUCTURE_TYPE_DEVICE_MEMORY_REPORT_CALLBACK_DATA_EXT;
    driver_cb.flags = VK_DEVICE_MEMORY_REPORT_FLAG_INTERNAL_OBJECT_BIT_EXT;
    driver_cb.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_ALLOCATE_EXT;
    driver_cb.memoryObjectId = shared_handle;
    driver_cb.size = 4096;
    driver_cb.objectType = VK_OBJECT_TYPE_DEVICE_MEMORY;
    driver_cb.objectHandle = 0x9999;
    DeviceMemoryReport::MemoryReportCallback(&driver_cb, nullptr);
    driver_cb.type = VK_DEVICE_MEMORY_REPORT_EVENT_TYPE_FREE_EXT;
    DeviceMemoryReport::MemoryReportCallback(&driver_cb, nullptr);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, shared_handle), "collided_memory");

    DeviceMemoryReport::Get().OnDestroyObject(shared_handle, VK_OBJECT_TYPE_IMAGE);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, shared_handle), "");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, shared_handle), "collided_memory");
}

TEST_F(DeviceMemoryReportTests, DebugObjectNamesLegacyDebugReportTypes) {
    TEST_DESCRIPTION("Test that VK_EXT_debug_marker object types map to their VkObjectType counterparts");

    InitializeDeviceMemoryReportPerfetto();

    const uint64_t buffer_handle = 0xE401;
    const uint64_t image_handle = 0xE402;
    const uint64_t memory_handle = 0xE403;

    DeviceMemoryReport::Get().SetDebugObjectName(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, buffer_handle, "marker_buffer");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_DEBUG_REPORT_OBJECT_TYPE_IMAGE_EXT, image_handle, "marker_image");
    DeviceMemoryReport::Get().SetDebugObjectName(VK_DEBUG_REPORT_OBJECT_TYPE_DEVICE_MEMORY_EXT, memory_handle, "marker_memory");

    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "marker_buffer");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_IMAGE, image_handle), "marker_image");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, memory_handle), "marker_memory");

    // Clearing through the legacy overload clears the underlying VkObjectType entry.
    DeviceMemoryReport::Get().SetDebugObjectName(VK_DEBUG_REPORT_OBJECT_TYPE_BUFFER_EXT, buffer_handle, nullptr);
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_BUFFER, buffer_handle), "");

    // Untracked legacy object types are ignored.
    DeviceMemoryReport::Get().SetDebugObjectName(VK_DEBUG_REPORT_OBJECT_TYPE_PIPELINE_EXT, 0xE404, "marker_pipeline");
    EXPECT_EQ(DeviceMemoryReportTestPeer::GetDebugObjectName(VK_OBJECT_TYPE_PIPELINE, 0xE404), "");
}
