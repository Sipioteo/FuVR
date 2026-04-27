# SPDX-License-Identifier: Apache-2.0

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)

add_library(fuvr_daemon_audio STATIC
    src/audio_session.cpp
)

target_include_directories(fuvr_daemon_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(fuvr_daemon_audio PRIVATE
    ${CMAKE_SOURCE_DIR}/proto/gen/cpp
    ${CMAKE_SOURCE_DIR}/transport/transport-ffi/include
    ${CMAKE_SOURCE_DIR}/daemon/include
    ${CMAKE_SOURCE_DIR}/encoder-macos/include
)

set_source_files_properties(src/audio_session.cpp PROPERTIES
    COMPILE_FLAGS "-fexceptions"
)

target_link_libraries(fuvr_daemon_audio
    PUBLIC
        fuvr_warnings
        fuvr_proto
        fuvr_audio_capture
)

option(FUVR_DAEMON_AUDIO_TESTS "Build daemon audio tests" ON)
if(FUVR_DAEMON_AUDIO_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(fuvr_daemon_audio_tests
        tests/test_audio_session.cpp
        # Host-mirror of the Quest receiver smoke test. The Quest source
        # tree builds the Android-targeted version; this build picks up the
        # same .cpp via the source path so we get coverage from CI without
        # an Android toolchain.
        ${CMAKE_SOURCE_DIR}/quest-app/app/src/main/cpp/audio/src/audio_receiver.cpp
        ${CMAKE_SOURCE_DIR}/quest-app/app/src/main/cpp/audio/tests/test_audio_decode.cpp
    )
    set_source_files_properties(
        tests/test_audio_session.cpp
        ${CMAKE_SOURCE_DIR}/quest-app/app/src/main/cpp/audio/src/audio_receiver.cpp
        ${CMAKE_SOURCE_DIR}/quest-app/app/src/main/cpp/audio/tests/test_audio_decode.cpp
        PROPERTIES COMPILE_FLAGS "-fexceptions -frtti"
    )
    target_include_directories(fuvr_daemon_audio_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/quest-app/app/src/main/cpp/audio/include
        ${OPUS_INCLUDE_DIRS}
    )
    target_link_directories(fuvr_daemon_audio_tests PRIVATE
        ${OPUS_LIBRARY_DIRS}
    )
    target_link_libraries(fuvr_daemon_audio_tests
        PRIVATE
            fuvr_daemon_audio
            fuvr_daemon_core
            GTest::gtest_main
            ${OPUS_LIBRARIES}
    )
    add_test(NAME fuvr_daemon_audio_tests COMMAND fuvr_daemon_audio_tests)
endif()
