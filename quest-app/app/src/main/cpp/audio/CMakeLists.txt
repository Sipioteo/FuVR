# SPDX-License-Identifier: Apache-2.0
#
# Quest-side audio module. Implements the receive path:
#   transport Audio channel datagram
#       -> OpusAudioReceiver (parses proto::AudioPacket, decodes Opus)
#       -> AAudioOutput      (low-latency callback-driven AAudio stream)
#
# This file is built by the Gradle/NDK toolchain when fuvr_quest is built;
# it is never built by the host CMake. It does require:
#   - libopus, fetched via ExternalProject below for arm64-v8a, mirroring how
#     Cap'n Proto is built in the parent CMakeLists.txt.
#   - Cap'n Proto generated headers + runtime, exposed by the parent target.
#
# Host-side smoke tests live under tests/ and are intentionally omitted here:
# they run as part of the daemon / encoder ctest run on macOS, where libopus
# is available via Homebrew. The Android build path links against the
# ExternalProject opus only.

include(ExternalProject)

set(OPUS_VERSION "1.5.2")
set(OPUS_PREFIX  "${CMAKE_BINARY_DIR}/opus")
set(OPUS_INSTALL "${OPUS_PREFIX}/install")

ExternalProject_Add(opus_external
    URL              https://downloads.xiph.org/releases/opus/opus-${OPUS_VERSION}.tar.gz
    PREFIX           "${OPUS_PREFIX}"
    INSTALL_DIR      "${OPUS_INSTALL}"
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
        -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
        -DANDROID_ABI=${ANDROID_ABI}
        -DANDROID_PLATFORM=${ANDROID_PLATFORM}
        -DANDROID_STL=${ANDROID_STL}
        -DCMAKE_BUILD_TYPE=Release
        -DBUILD_SHARED_LIBS=OFF
        -DOPUS_BUILD_PROGRAMS=OFF
        -DOPUS_BUILD_TESTING=OFF
        -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
    BUILD_BYPRODUCTS
        ${OPUS_INSTALL}/lib/libopus.a
)

set(OPUS_INCLUDE_DIR "${OPUS_INSTALL}/include")
set(OPUS_INCLUDE_DIR_OPUS_SUBDIR "${OPUS_INSTALL}/include/opus")
set(OPUS_LIB_DIR     "${OPUS_INSTALL}/lib")
file(MAKE_DIRECTORY "${OPUS_INCLUDE_DIR}")

add_library(opus_static STATIC IMPORTED)
set_target_properties(opus_static PROPERTIES
    IMPORTED_LOCATION "${OPUS_LIB_DIR}/libopus.a"
)
add_dependencies(opus_static opus_external)

add_library(fuvr_quest_audio STATIC
    src/audio_receiver.cpp
    src/aaudio_output.cpp
    src/router_glue.cpp
)

target_include_directories(fuvr_quest_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(fuvr_quest_audio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../
    ${CMAKE_CURRENT_SOURCE_DIR}/../proto_gen
    ${CAPNP_INCLUDE_DIR}
    ${OPUS_INCLUDE_DIR}
    ${OPUS_INCLUDE_DIR_OPUS_SUBDIR}
)

# Cap'n Proto bridge needs exceptions + RTTI; the rest of the module does not.
set_source_files_properties(
    src/audio_receiver.cpp
    PROPERTIES COMPILE_FLAGS "-fexceptions -frtti"
)

target_link_libraries(fuvr_quest_audio
    PUBLIC
        opus_static
        capnp_static
        kj_static
        aaudio
        log
)
