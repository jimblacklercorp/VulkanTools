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
#include "../debug_marker/debug_marker.h"
#include "../object_names/vulkan_object_names.h"
#include <vulkan/vulkan_core.h>
#include <gtest/gtest.h>
#include <stdlib.h>

static const char* kLayerName = "VK_LAYER_GOOGLE_DebugMarker";

class DebugMarkerTests : public VkTestFramework {
   public:
    ~DebugMarkerTests(){};

    static void SetUpTestSuite() {}
    static void TearDownTestSuite(){};
};

TEST_F(DebugMarkerTests, CombinedTest) {
    TEST_DESCRIPTION("Combined test for DebugMarker layer");

    auto& object_names = layersvt::VulkanObjectNames::Get();

    DebugMarker::Get().Clear();
    object_names.Clear();
    layer_test::VulkanInstanceBuilder inst_builder;
    inst_builder.AddExtension("VK_EXT_debug_utils");
    VkResult err = inst_builder.Init(kLayerName);
    EXPECT_EQ(err, VK_SUCCESS);

    VkInstance instance = inst_builder.GetInstance();
    EXPECT_NE(instance, VK_NULL_HANDLE);

    // Verify that GetInstanceProcAddr returns layer functions
    PFN_vkVoidFunction pfnSetDebugUtilsObjectNameEXT = vkGetInstanceProcAddr(instance, "vkSetDebugUtilsObjectNameEXT");
    EXPECT_NE(pfnSetDebugUtilsObjectNameEXT, nullptr);

    PFN_vkVoidFunction pfnCmdDebugMarkerBeginEXT = vkGetInstanceProcAddr(instance, "vkCmdDebugMarkerBeginEXT");
    EXPECT_NE(pfnCmdDebugMarkerBeginEXT, nullptr);

    // 1. Set instance name
    object_names.SetObjectName(0, VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance");

    EXPECT_TRUE(object_names.HasObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance"));

    // 2. Override new name
    object_names.SetObjectName(0, VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed");
    EXPECT_TRUE(object_names.HasObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
    EXPECT_FALSE(object_names.HasObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstance"));

    // 3. Clear
    object_names.Clear();
    EXPECT_FALSE(object_names.HasObjectName(VK_OBJECT_TYPE_INSTANCE, (uint64_t)instance, "MyInstanceRenamed"));
}
