cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ANDROID_PLATFORM)
    set(ANDROID_PLATFORM "android-30" CACHE STRING "Android API level")
endif()

project(nfsu2_android LANGUAGES CXX)

if(NOT ANDROID)
    message(FATAL_ERROR "NFSU2 Android requires the Android NDK")
endif()

if(NOT DEFINED ANDROID_PLATFORM)
    message(FATAL_ERROR "ANDROID_PLATFORM must be specified")
endif()

string(REGEX REPLACE "^android-" "" NFSU2_ANDROID_API "${ANDROID_PLATFORM}")

if(NOT NFSU2_ANDROID_API MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Invalid Android platform: ${ANDROID_PLATFORM}")
endif()

if(NFSU2_ANDROID_API LESS 30)
    message(FATAL_ERROR "NFSU2 Android requires API 30 or higher")
endif()

message(STATUS "NFSU2 Android API level: ${NFSU2_ANDROID_API}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(
    NFSU2_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    ABSOLUTE
)

set(NFSU2_GENERATED_DIR "${NFSU2_ROOT}/generated/game")
set(NFSU2_RUNTIME_DIR "${NFSU2_ROOT}/sdk/runtime")

file(GLOB NFSU2_RECOMP_SOURCES
    CONFIGURE_DEPENDS
    "${NFSU2_GENERATED_DIR}/x86_recomp.*.cpp"
)

if(NOT NFSU2_RECOMP_SOURCES)
    message(FATAL_ERROR "Generated NFSU2 sources not found")
endif()

list(LENGTH NFSU2_RECOMP_SOURCES NFSU2_PARTITION_COUNT)
message(STATUS "NFSU2 recompilation partitions: ${NFSU2_PARTITION_COUNT}")

add_library(nfsu2_recompiled STATIC
    ${NFSU2_RECOMP_SOURCES}
    "${NFSU2_GENERATED_DIR}/x86_func_mapping.cpp"
)

target_include_directories(nfsu2_recompiled PRIVATE
    "${NFSU2_RUNTIME_DIR}"
    "${NFSU2_GENERATED_DIR}"
)

target_compile_features(nfsu2_recompiled PRIVATE cxx_std_23)

target_compile_options(nfsu2_recompiled PRIVATE
    -fno-strict-aliasing
    -fsigned-char
    -w
)

set_target_properties(nfsu2_recompiled PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# XboxXBERecomp core runtime for Android ARM64
set(NFSU2_CORE_RUNTIME_SOURCES
    "${NFSU2_RUNTIME_DIR}/core/log.cpp"
    "${NFSU2_RUNTIME_DIR}/cpu/dispatch.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/memory.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/threads.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/objects.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/ordinals.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/rtl.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/crypto.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/iocp.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/files.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/disc.cpp"
    "${NFSU2_RUNTIME_DIR}/kernel/xboxkrnl.cpp"
)

add_library(nfsu2_runtime_core STATIC
    ${NFSU2_CORE_RUNTIME_SOURCES}
)

target_include_directories(nfsu2_runtime_core PRIVATE
    "${NFSU2_RUNTIME_DIR}"
    "${NFSU2_GENERATED_DIR}"
)

target_compile_features(nfsu2_runtime_core PRIVATE cxx_std_23)

target_compile_options(nfsu2_runtime_core PRIVATE
    -fno-strict-aliasing
    -fsigned-char
    -Wall
    -Wextra
)

set_target_properties(nfsu2_runtime_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Xbox executable boot support
target_sources(nfsu2_runtime_core PRIVATE
    "${NFSU2_RUNTIME_DIR}/kernel/boot.cpp"
)

# Xbox XDK base services
add_library(nfsu2_xdk_base STATIC
    "${NFSU2_RUNTIME_DIR}/xdk/xapi.cpp"
    "${NFSU2_RUNTIME_DIR}/xdk/xnet.cpp"
)

target_include_directories(nfsu2_xdk_base PRIVATE
    "${NFSU2_RUNTIME_DIR}"
    "${NFSU2_GENERATED_DIR}"
)

target_compile_features(nfsu2_xdk_base PRIVATE cxx_std_23)

target_compile_options(nfsu2_xdk_base PRIVATE
    -fno-strict-aliasing
    -fsigned-char
    -Wall
    -Wextra
)

set_target_properties(nfsu2_xdk_base PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Xbox Direct3D 8 high-level emulation and GPU utilities
add_library(nfsu2_xdk_graphics STATIC
    "${NFSU2_RUNTIME_DIR}/xdk/d3d8.cpp"
    "${NFSU2_RUNTIME_DIR}/gpu/texture.cpp"
    "${NFSU2_RUNTIME_DIR}/gpu/vertex.cpp"
    "${NFSU2_RUNTIME_DIR}/gpu/shaders.cpp"
)

# Xbox DirectSound high-level emulation
add_library(nfsu2_xdk_audio STATIC
    "${NFSU2_RUNTIME_DIR}/xdk/dsound.cpp"
)

# Shared Android compilation settings
foreach(target IN ITEMS nfsu2_xdk_graphics nfsu2_xdk_audio)
    target_include_directories(${target} PRIVATE
        "${NFSU2_RUNTIME_DIR}"
        "${NFSU2_GENERATED_DIR}"
    )

    target_compile_features(${target} PRIVATE cxx_std_23)

    target_compile_options(${target} PRIVATE
        -fno-strict-aliasing
        -fsigned-char
        -Wall
        -Wextra
    )

    set_target_properties(${target} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
    )
endforeach()

# Vulkan rendering backend for Android ARM64
add_library(nfsu2_vulkan_backend STATIC
    "${NFSU2_RUNTIME_DIR}/gpu/render_thread.cpp"
    "${NFSU2_RUNTIME_DIR}/gpu/vulkan.cpp"
    "${NFSU2_RUNTIME_DIR}/gpu/spirv.cpp"
)

target_include_directories(nfsu2_vulkan_backend PRIVATE
    "${NFSU2_RUNTIME_DIR}"
    "${NFSU2_GENERATED_DIR}"
)

target_compile_features(nfsu2_vulkan_backend PRIVATE cxx_std_23)

target_compile_options(nfsu2_vulkan_backend PRIVATE
    -fno-strict-aliasing
    -fsigned-char
    -Wall
    -Wextra
)

set_target_properties(nfsu2_vulkan_backend PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Android native display host
add_library(nfsu2_android_host STATIC
    "${CMAKE_CURRENT_LIST_DIR}/android_host.cpp"
)

target_include_directories(nfsu2_android_host PRIVATE
    "${NFSU2_RUNTIME_DIR}"
    "${NFSU2_GENERATED_DIR}"
)

target_compile_features(nfsu2_android_host PRIVATE cxx_std_23)

target_compile_definitions(nfsu2_android_host PRIVATE
    VK_USE_PLATFORM_ANDROID_KHR
)

target_compile_options(nfsu2_android_host PRIVATE
    -fno-strict-aliasing
    -fsigned-char
    -Wall
    -Wextra
)

set_target_properties(nfsu2_android_host PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Android native game runtime and JNI
add_library(nfsu2 SHARED
    "${CMAKE_CURRENT_LIST_DIR}/android_jni.cpp"
)

target_compile_features(nfsu2 PRIVATE cxx_std_23)

# Require every symbol to be resolved.
# Do not allow the linker to silently skip unused archive objects.
target_link_libraries(nfsu2 PRIVATE
    "-Wl,--whole-archive"

    nfsu2_recompiled
    nfsu2_runtime_core
    nfsu2_xdk_base
    nfsu2_xdk_graphics
    nfsu2_xdk_audio
    nfsu2_vulkan_backend
    nfsu2_android_host

    "-Wl,--no-whole-archive"

    vulkan
    android
    log
    dl
    m
)

target_link_options(nfsu2 PRIVATE
    -fuse-ld=lld
    "LINKER:--no-undefined"
    "LINKER:--error-limit=0"
)

# Android native input, audio and lifecycle
target_sources(nfsu2_android_host PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/android_input.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/android_audio.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/android_lifecycle.cpp"
)

target_link_libraries(nfsu2 PRIVATE
    aaudio
)
