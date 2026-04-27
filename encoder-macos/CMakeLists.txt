# SPDX-License-Identifier: Apache-2.0

add_library(fuvr_encoder STATIC
    src/encoder.mm
)

target_include_directories(fuvr_encoder
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set_source_files_properties(src/encoder.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc"
)

target_link_libraries(fuvr_encoder
    PRIVATE
        fuvr_warnings
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreVideo"
        "-framework Foundation"
        "-framework IOSurface"
)

add_executable(fuvr-encode-synthetic
    tools/encode_synthetic.mm
)
set_source_files_properties(tools/encode_synthetic.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc"
)
target_link_libraries(fuvr-encode-synthetic
    PRIVATE fuvr_encoder
        "-framework Foundation"
        "-framework CoreVideo"
        "-framework IOSurface"
)

option(FUVR_ENCODER_TESTS "Build encoder smoke tests" ON)
if(FUVR_ENCODER_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(fuvr_encoder_smoke
        tests/test_encoder_smoke.mm
    )
    set_source_files_properties(tests/test_encoder_smoke.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
    target_link_libraries(fuvr_encoder_smoke
        PRIVATE
            fuvr_encoder
            GTest::gtest_main
            "-framework Foundation"
            "-framework CoreVideo"
    )
    enable_testing()
    add_test(NAME fuvr_encoder_smoke COMMAND fuvr_encoder_smoke)
endif()

add_subdirectory(audio)
