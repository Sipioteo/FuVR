# SPDX-License-Identifier: Apache-2.0

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED opus)

add_library(fuvr_audio_capture STATIC
    src/capture.mm
    src/opus_encoder.cpp
)

target_include_directories(fuvr_audio_capture PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(fuvr_audio_capture PRIVATE
    ${OPUS_INCLUDE_DIRS}
)
target_link_directories(fuvr_audio_capture PUBLIC
    ${OPUS_LIBRARY_DIRS}
)
target_link_libraries(fuvr_audio_capture
    PUBLIC
        fuvr_warnings
        ${OPUS_LIBRARIES}
        "-framework Foundation"
        "-framework AVFoundation"
        "-framework CoreMedia"
        "-framework CoreAudio"
        "-framework AudioToolbox"
        "-framework ScreenCaptureKit"
)

set_source_files_properties(src/capture.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -fexceptions"
)

option(FUVR_AUDIO_TESTS "Build audio tests" ON)
if(FUVR_AUDIO_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(fuvr_audio_smoke
        tests/test_capture_smoke.cpp
        tests/test_opus_encoder.cpp
    )
    set_source_files_properties(tests/test_opus_encoder.cpp PROPERTIES
        COMPILE_FLAGS "-fexceptions"
    )
    set_source_files_properties(tests/test_capture_smoke.cpp PROPERTIES
        COMPILE_FLAGS "-fexceptions"
    )
    target_include_directories(fuvr_audio_smoke PRIVATE
        ${OPUS_INCLUDE_DIRS}
    )
    target_link_directories(fuvr_audio_smoke PRIVATE
        ${OPUS_LIBRARY_DIRS}
    )
    target_link_libraries(fuvr_audio_smoke
        PRIVATE
            fuvr_audio_capture
            GTest::gtest_main
            ${OPUS_LIBRARIES}
    )
    add_test(NAME fuvr_audio_smoke COMMAND fuvr_audio_smoke)
endif()
