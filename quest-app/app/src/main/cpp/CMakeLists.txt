# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.22.1)
project(fuvr_quest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(ExternalProject)

find_package(openxr_loader REQUIRED CONFIG)

# ---------------------------------------------------------------------------
# Cap'n Proto runtime, built from source for arm64-v8a as part of the build.
#
# Why ExternalProject_Add over a Maven AAR: at the time of writing no
# widely-available `eclipse-capnproto:capnp-android` prefab exists, and
# building from a pinned source tarball keeps the runtime version in tree.
# ---------------------------------------------------------------------------
set(CAPNP_VERSION "1.0.2")
set(CAPNP_PREFIX  "${CMAKE_BINARY_DIR}/capnp")
set(CAPNP_INSTALL "${CAPNP_PREFIX}/install")

ExternalProject_Add(capnproto_external
    URL              https://capnproto.org/capnproto-c++-${CAPNP_VERSION}.tar.gz
    PREFIX           "${CAPNP_PREFIX}"
    INSTALL_DIR      "${CAPNP_INSTALL}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_STL=${ANDROID_STL}
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_TESTING=OFF
        -DBUILD_TOOLS=OFF
        -DCAPNP_LITE=OFF
        -DEXTERNAL_CAPNP=ON
    BUILD_BYPRODUCTS
        ${CAPNP_INSTALL}/lib/libcapnp.a
        ${CAPNP_INSTALL}/lib/libkj.a
)

set(CAPNP_INCLUDE_DIR "${CAPNP_INSTALL}/include")
set(CAPNP_LIB_DIR     "${CAPNP_INSTALL}/lib")
file(MAKE_DIRECTORY "${CAPNP_INCLUDE_DIR}")

add_library(capnp_static STATIC IMPORTED)
set_target_properties(capnp_static PROPERTIES IMPORTED_LOCATION "${CAPNP_LIB_DIR}/libcapnp.a")
add_dependencies(capnp_static capnproto_external)

add_library(kj_static STATIC IMPORTED)
set_target_properties(kj_static PROPERTIES IMPORTED_LOCATION "${CAPNP_LIB_DIR}/libkj.a")
add_dependencies(kj_static capnproto_external)

# ---------------------------------------------------------------------------
# Generated Cap'n Proto sources (produced by the Gradle generateCapnp task).
# ---------------------------------------------------------------------------
set(PROTO_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto_gen")
file(GLOB PROTO_GEN_SOURCES "${PROTO_GEN_DIR}/*.c++")

# ---------------------------------------------------------------------------
# Main shared library.
# ---------------------------------------------------------------------------
add_library(fuvr_quest SHARED
    main.cpp
    openxr_session.cpp
    decoder_pipeline.cpp
    transport_client.cpp
    pose_forwarder.cpp
    compositor.cpp
    eye_blit.cpp
    fragment_reassembler.cpp
    protocol_router.cpp
    proto_codec.cpp
    clock_sync.cpp
    input_packer.cpp
    metrics_format.cpp
    loss_tracker.cpp
    hand_encoder.cpp
    connection_ui.cpp
    ${PROTO_GEN_SOURCES}
)

# Cap'n Proto requires exceptions and RTTI; the rest of the app does not.
set_source_files_properties(
    proto_codec.cpp
    ${PROTO_GEN_SOURCES}
    PROPERTIES COMPILE_FLAGS "-fexceptions -frtti"
)

target_include_directories(fuvr_quest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROTO_GEN_DIR}
    ${CAPNP_INCLUDE_DIR}
)

target_compile_definitions(fuvr_quest PRIVATE
    XR_USE_PLATFORM_ANDROID=1
    XR_USE_GRAPHICS_API_OPENGL_ES=1
)

target_link_libraries(fuvr_quest PRIVATE
    OpenXR::openxr_loader
    capnp_static
    kj_static
    EGL
    GLESv3
    android
    log
    mediandk
)

add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
)
target_include_directories(fuvr_quest PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
)
target_link_libraries(fuvr_quest PRIVATE native_app_glue)

target_link_options(fuvr_quest PRIVATE "-u" "ANativeActivity_onCreate")

# Audio subsystem (EPSILON-owned). Append-only: presence is optional, the
# top-level build must succeed whether or not the subdir exists yet.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/audio/CMakeLists.txt")
    add_subdirectory(audio)
endif()
