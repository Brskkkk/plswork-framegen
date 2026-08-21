cmake_minimum_required(VERSION 3.22.1)
project(lsfg-android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Mali-G57 fallback: use NDK Vulkan headers instead of submodule volk
# Add NDK include directories for Vulkan
include_directories($ENV{ANDROID_NDK}/sources/android/native_app_glue)
include_directories($ENV{ANDROID_NDK}/sources/third_party/vulkan/src/include)

# Mali-G57 fallback: no lsfg-vk-android submodule needed
# The framegen code is commented out below, so we skip the submodule check
# set(LSFG_ROOT "${CMAKE_SOURCE_DIR}/../../../../../lsfg-vk-android")
# if(NOT EXISTS "${LSFG_ROOT}/framegen/CMakeLists.txt")
#     message(FATAL_ERROR "Can't find lsfg-vk sources at ${LSFG_ROOT}. Did you clone the submodules?")
# endif()

# Mali-G57: volk, pe-parse, dxbc all require lsfg-vk-android submodule
# These are only needed for shader extraction, which Mali doesn't use
# For Mali, we use pre-compiled SPIR-V and volk is loaded dynamically
# # add_library(volk ...)
# # add_library(pe-parse ...)
# # add_subdirectory(dxbc ...)

# -----------------------------------------------------------------------------
# framegen — the Vulkan frame-generation pipeline. Reused verbatim.
#
# Mali-G57 fallback build: skip framegen submodule entirely
# The blending path doesn't need lsfg-vk-framegen
# Uncomment the code below to re-enable framegen for non-Mali builds
#
# file(GLOB_RECURSE FRAMEGEN_WATCH_SOURCES CONFIGURE_DEPENDS
#     "${LSFG_ROOT}/framegen/src/*.cpp"
#     "${LSFG_ROOT}/framegen/src/*.hpp"
#     "${LSFG_ROOT}/framegen/include/*.hpp"
#     "${LSFG_ROOT}/framegen/public/*.hpp"
#     "${LSFG_ROOT}/framegen/v3.1_src/*.cpp"
#     "${LSFG_ROOT}/framegen/v3.1p_src/*.cpp"
# )
# set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
#     ${FRAMEGEN_WATCH_SOURCES}
# )
# add_subdirectory("${LSFG_ROOT}/framegen" "${CMAKE_BINARY_DIR}/framegen" EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# lsfg-android — JNI shared library.
# Phase 2 only exposes nativeVersion() plus placeholder symbols; later phases
# add the real shader extraction and Vulkan context entry points.
# -----------------------------------------------------------------------------
find_library(log-lib log)
find_library(neuralnetworks-lib neuralnetworks)

add_library(lsfg-android SHARED
    lsfg_jni.cpp
    android_shader_loader.cpp
    android_vk_probe.cpp
    android_vk_session.cpp
    ahb_image_bridge.cpp
    gpu_postprocess.cpp
    gpu_frame_blender.cpp
    lsfg_render_loop.cpp
    nnapi_npu.cpp
    nnapi_postprocess.cpp
    cpu_postprocess.cpp
    crash_reporter.cpp
)

target_link_libraries(lsfg-android
    PRIVATE
        # Mali-G57 fallback: link Vulkan directly from NDK
        vulkan
        ${log-lib}
        ${neuralnetworks-lib}
        android
)
