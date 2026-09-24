file(GLOB IMAGES "${PROJECT_SOURCE_DIR}/layersvt/images/*")
file(COPY ${IMAGES} DESTINATION ${CMAKE_BINARY_DIR}/layersvt/images)

if(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
    # Minimize what Windows.h leaks
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
elseif(ANDROID)
    add_compile_definitions(VK_USE_PLATFORM_ANDROID_KHR)

elseif(APPLE)
    if (CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        add_compile_definitions(VK_USE_PLATFORM_MACOS_MVK)
    endif()
    if (IOS)
        add_compile_definitions(VK_USE_PLATFORM_IOS_MVK)
    endif()
elseif(CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU")
    option(BUILD_WSI_XCB_SUPPORT "Build XCB WSI support" ON)
    option(BUILD_WSI_WAYLAND_SUPPORT "Build WAYLAND WSI support" ON)


    find_package(PkgConfig REQUIRED)

    if (BUILD_WSI_XCB_SUPPORT)
        pkg_check_modules(XCB REQUIRED QUIET IMPORTED_TARGET xcb)
        add_compile_definitions(VK_USE_PLATFORM_XCB_KHR)
    endif()

    if (BUILD_WSI_WAYLAND_SUPPORT)
        pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
        add_compile_definitions(VK_USE_PLATFORM_WAYLAND_KHR)
    endif()

else()
    message(FATAL_ERROR "Unsupported Platform!")
endif()

if (WIN32)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if (MSVC)
        add_compile_options(/bigobj)
    endif()
else()
    add_compile_options(-Wpointer-arith)
endif()

if(BUILD_APIDUMP)
    find_package(Python3 REQUIRED)

    set(VULKAN_REGISTRY "${VULKAN_HEADERS_INSTALL_DIR}/${CMAKE_INSTALL_DATADIR}/vulkan/registry")

    if(IOS)
        add_library(VkLayer_api_dump SHARED)
    else()
        add_library(VkLayer_api_dump MODULE)
    endif()
    set_target_properties(VkLayer_api_dump PROPERTIES FOLDER "layers/api_dump")

    target_sources(VkLayer_api_dump PRIVATE
        api_dump_handwritten_dispatch.cpp
        generated/api_dump_dispatch.h
        generated/api_dump_implementation.h
        api_dump.h
        api_dump_handwritten_functions.h
        vk_layer_table.cpp
        vk_layer_table.h
        api_dump_layer.md
        json/VkLayer_api_dump.json.in
        ../scripts/generators/api_dump_generator.py
    )

    target_include_directories(VkLayer_api_dump PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    if(CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU")
        if (BUILD_WSI_XCB_SUPPORT)
            target_compile_definitions(VkLayer_api_dump PRIVATE VK_USE_PLATFORM_XLIB_KHR)
        endif()

        if (BUILD_WSI_WAYLAND_SUPPORT)
            target_compile_definitions(VkLayer_api_dump PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
        endif()
    endif()

    target_compile_definitions(VkLayer_api_dump PRIVATE VK_ENABLE_BETA_EXTENSIONS)

    if (VT_CODEGEN)
        add_dependencies(VkLayer_api_dump vt_codegen_as_needed)
    endif()
endif ()

if(BUILD_MONITOR)
    add_library(VkLayer_monitor MODULE)
    set_target_properties(VkLayer_monitor PROPERTIES FOLDER "layers/monitor")
    target_sources(VkLayer_monitor PRIVATE
        monitor.cpp
        vk_layer_table.cpp
        vk_layer_table.h
        monitor_layer.md
        json/VkLayer_monitor.json.in
    )
endif ()

if(BUILD_SCREENSHOT)
    add_library(VkLayer_screenshot MODULE)
    set_target_properties(VkLayer_screenshot PROPERTIES FOLDER "layers/screenshot")
    target_sources(VkLayer_screenshot PRIVATE
        screenshot/screenshot.cpp
        screenshot/screenshot_parsing.cpp
        screenshot/screenshot_parsing.h
        perfetto/perfetto.cc
        screenshot/screenshot_perfetto.cpp
        vk_layer_table.cpp
        vk_layer_table.h
        screenshot/screenshot_layer.md
        layer_keep_alive.cpp
        screenshot/json/VkLayer_screenshot.json.in
    )
endif()

if(BUILD_CPUTIMING)
    add_library(VkLayer_CPUTiming MODULE)
    set_target_properties(VkLayer_CPUTiming PROPERTIES FOLDER "layers/cputiming")
    target_sources(VkLayer_CPUTiming PRIVATE
        cpu_timing/cputiming_handwritten_dispatch.cpp
        generated/cputiming_dispatch.h
        generated/cputiming_implementation.h
        cpu_timing/cputiming_handwritten_functions.h
        cpu_timing/cputiming.h
        cpu_timing/cputiming.cpp
        perfetto/perfetto.cc
        perfetto/perfetto_helpers.cpp
        ../scripts/generators/cputiming_generator.py
        vk_layer_table.cpp
        vk_layer_table.h
        layer_keep_alive.cpp
        cpu_timing/VkLayer_CPUTiming.json.in
    )

    target_include_directories(VkLayer_CPUTiming PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/cpu_timing
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    if(CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU")
        if (BUILD_WSI_XCB_SUPPORT)
            target_compile_definitions(VkLayer_CPUTiming PRIVATE VK_USE_PLATFORM_XLIB_KHR)
        endif()

        if (BUILD_WSI_WAYLAND_SUPPORT)
            target_compile_definitions(VkLayer_CPUTiming PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
        endif()
    endif()

    target_compile_definitions(VkLayer_CPUTiming PRIVATE VK_ENABLE_BETA_EXTENSIONS)

    if (VT_CODEGEN)
        add_dependencies(VkLayer_CPUTiming vt_codegen_as_needed)
    endif()
endif()

if(BUILD_DEBUGMARKER)
    add_library(VkLayer_DebugMarker MODULE)
    set_target_properties(VkLayer_DebugMarker PROPERTIES FOLDER "layers/debugmarker")
    target_sources(VkLayer_DebugMarker PRIVATE
        debug_marker/debug_marker_handwritten_dispatch.cpp
        debug_marker/debug_marker_handwritten_functions.h
        debug_marker/debug_marker_handwritten_functions_vk_ext_debug_marker.h
        debug_marker/debug_marker_handwritten_functions_vk_ext_debug_utils.h
        debug_marker/debug_marker.h
        debug_marker/debug_marker.cpp
        debug_marker/debug_marker_perfetto.h
        debug_marker/debug_marker_perfetto.cpp
        perfetto/perfetto.cc
        vk_layer_table.cpp
        vk_layer_table.h
        layer_keep_alive.cpp
        debug_marker/VkLayer_DebugMarker.json.in
    )

    target_include_directories(VkLayer_DebugMarker PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/debug_marker
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    if(CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU")
        if (BUILD_WSI_XCB_SUPPORT)
            target_compile_definitions(VkLayer_DebugMarker PRIVATE VK_USE_PLATFORM_XLIB_KHR)
        endif()

        if (BUILD_WSI_WAYLAND_SUPPORT)
            target_compile_definitions(VkLayer_DebugMarker PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
        endif()
    endif()

    target_compile_definitions(VkLayer_DebugMarker PRIVATE VK_ENABLE_BETA_EXTENSIONS)
endif()

if(BUILD_DEVICEMEMORYREPORT)
    add_library(VkLayer_DeviceMemoryReport MODULE)
    set_target_properties(VkLayer_DeviceMemoryReport PROPERTIES FOLDER "layers/devicememoryreport")
    target_sources(VkLayer_DeviceMemoryReport PRIVATE
        device_memory_report/device_memory_report_handwritten_dispatch.cpp
        device_memory_report/device_memory_report_handwritten_functions.h
        device_memory_report/device_memory_report.h
        device_memory_report/device_memory_report.cpp
        device_memory_report/device_memory_report_perfetto.h
        device_memory_report/device_memory_report_perfetto.cpp
        perfetto/perfetto.cc
        vk_layer_table.cpp
        vk_layer_table.h
        layer_keep_alive.cpp
        device_memory_report/VkLayer_DeviceMemoryReport.json.in
    )

    target_include_directories(VkLayer_DeviceMemoryReport PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/device_memory_report
        ${CMAKE_CURRENT_BINARY_DIR}
    )

    if(CMAKE_SYSTEM_NAME MATCHES "Linux|BSD|DragonFly|GNU")
        if (BUILD_WSI_XCB_SUPPORT)
            target_compile_definitions(VkLayer_DeviceMemoryReport PRIVATE VK_USE_PLATFORM_XLIB_KHR)
        endif()

        if (BUILD_WSI_WAYLAND_SUPPORT)
            target_compile_definitions(VkLayer_DeviceMemoryReport PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
        endif()
    endif()

    target_compile_definitions(VkLayer_DeviceMemoryReport PRIVATE VK_ENABLE_BETA_EXTENSIONS)
    if(UNIX AND NOT APPLE)
        target_link_options(VkLayer_DeviceMemoryReport PRIVATE "-Wl,-Bsymbolic-functions")
    endif()
endif()

if (BUILD_TESTS AND NOT RUN_ON_GITHUB)
    add_subdirectory(test)
endif()

list(APPEND TOOL_LAYERS "VkLayer_api_dump" "VkLayer_screenshot" "VkLayer_monitor" "VkLayer_CPUTiming" "VkLayer_DebugMarker" "VkLayer_DeviceMemoryReport")
foreach(layer ${TOOL_LAYERS})
    if (NOT TARGET "${layer}")
        continue()
    endif()

    if (layer STREQUAL "VkLayer_DebugMarker")
        set(layer_dir "debug_marker/")
    elseif(layer STREQUAL "VkLayer_CPUTiming")
        set(layer_dir "cpu_timing/")
    elseif(layer STREQUAL "VkLayer_screenshot")
        set(layer_dir "screenshot/")
    elseif(layer STREQUAL "VkLayer_DeviceMemoryReport")
        set(layer_dir "device_memory_report/")
    else()
        set(layer_dir "")
    endif()

    target_link_Libraries(${layer} PRIVATE Vulkan::Headers Vulkan::UtilityHeaders Vulkan::LayerSettings)

    if (ANDROID)
        target_link_Libraries(${layer} PRIVATE log android atomic)
    endif()

    if (WIN32)
        target_link_Libraries(${layer} PRIVATE ws2_32)
    endif()

    target_include_directories(${layer} PRIVATE .)

    if (MSVC)
        target_link_options(${layer} PRIVATE /DEF:${CMAKE_CURRENT_SOURCE_DIR}/${layer_dir}${layer}.def)
    elseif(MINGW)
        target_sources(${layer} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/${layer_dir}${layer}.def)
        target_compile_definitions(${layer} PRIVATE uid_t=int)
    endif()

    if (APPLE)
        # IOS and APPLE can both be true (FYI)
        if(IOS)
            set_target_properties(${layer} PROPERTIES
		FRAMEWORK			TRUE
                MACOSX_FRAMEWORK_BUNDLE_VERSION "${VulkanHeaders_VERSION}"
                MACOSX_FRAMEWORK_SHORT_VERSION_STRING "${VulkanHeaders_VERSION}"
		MACOSX_FRAMEWORK_IDENTIFIER 	com.khronos.${layer}
            )
        else()
            set_target_properties(${layer} PROPERTIES SUFFIX ".dylib")
        endif()
    endif()

    if (ANDROID)
        install(TARGETS ${layer} DESTINATION ${CMAKE_INSTALL_LIBDIR})
        continue()
    endif()

    if (layer STREQUAL "VkLayer_DebugMarker")
        set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/debug_marker/${layer}.json.in")
    elseif(layer STREQUAL "VkLayer_CPUTiming")
        set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cpu_timing/${layer}.json.in")
    elseif(layer STREQUAL "VkLayer_screenshot")
        set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/screenshot/json/${layer}.json.in")
    elseif(layer STREQUAL "VkLayer_DeviceMemoryReport")
        set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/device_memory_report/${layer}.json.in")
    else()
        set(INPUT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/json/${layer}.json.in")
    endif()


    set(INTERMEDIATE_FILE "${CMAKE_CURRENT_BINARY_DIR}/json/intermediate-${layer}.json")
    set(OUTPUT_FILE_FINAL_NAME "${layer}.json")
    set(LAYER_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR})
    if (WIN32)
        set(LAYER_INSTALL_DIR ${CMAKE_INSTALL_BINDIR})
    endif()

    if (WIN32)
        set(JSON_LIBRARY_PATH ".\\\\${layer}.dll")
    elseif(APPLE)
        set(JSON_LIBRARY_PATH "./lib${layer}.dylib")
    else()
        set(JSON_LIBRARY_PATH "./lib${layer}.so")
    endif()

    set(JSON_VERSION ${VulkanHeaders_VERSION})

    configure_file(${INPUT_FILE} ${INTERMEDIATE_FILE} @ONLY)

    add_custom_command(TARGET ${layer} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${INTERMEDIATE_FILE} $<TARGET_FILE_DIR:${layer}>/${OUTPUT_FILE_FINAL_NAME}
    )

    if (UNIX)
        set(UNIX_INTERMEDIATE_FILE "${CMAKE_CURRENT_BINARY_DIR}/json/unix_install_${layer}.json")

        if(APPLE)
            set(JSON_LIBRARY_PATH "lib${layer}.dylib")
        else()
            set(JSON_LIBRARY_PATH "lib${layer}.so")
        endif()

        configure_file(${INPUT_FILE} ${UNIX_INTERMEDIATE_FILE} @ONLY)

        install(FILES ${UNIX_INTERMEDIATE_FILE} DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/vulkan/explicit_layer.d RENAME ${OUTPUT_FILE_FINAL_NAME})
    endif()

    if (WIN32)
        install(FILES ${INTERMEDIATE_FILE} DESTINATION ${LAYER_INSTALL_DIR} RENAME ${OUTPUT_FILE_FINAL_NAME})
    endif()
    if (MSVC)
        install(FILES $<TARGET_PDB_FILE:${layer}> DESTINATION ${LAYER_INSTALL_DIR})
    endif()

    install(TARGETS ${layer} DESTINATION ${LAYER_INSTALL_DIR})
endforeach()
