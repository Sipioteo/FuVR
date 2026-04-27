# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22.1)
project(fuvr_quest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(openxr_loader REQUIRED CONFIG)

set(PROTO_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto_gen")
file(GLOB PROTO_GEN_SOURCES "${PROTO_GEN_DIR}/*.c++")

add_library(fuvr_quest SHARED
    main.cpp
    openxr_session.cpp
    decoder_pipeline.cpp
    transport_client.cpp
    pose_forwarder.cpp
    compositor.cpp
    ${PROTO_GEN_SOURCES}
)

target_include_directories(fuvr_quest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROTO_GEN_DIR}
)

target_compile_definitions(fuvr_quest PRIVATE
    XR_USE_PLATFORM_ANDROID=1
    XR_USE_GRAPHICS_API_OPENGL_ES=1
)

target_link_libraries(fuvr_quest PRIVATE
    OpenXR::openxr_loader
    EGL
    GLESv3
    android
    log
    mediandk
)

# native_app_glue lives inside the NDK; needed for android_main.
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(fuvr_quest PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)
target_link_libraries(fuvr_quest PRIVATE native_app_glue)

# Force-load android_main symbol so the NativeActivity loader finds it.
target_link_options(fuvr_quest PRIVATE "-u" "ANativeActivity_onCreate")
