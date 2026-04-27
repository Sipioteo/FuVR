include(FetchContent)

set(OPENXR_SDK_TAG "release-1.1.36" CACHE STRING "OpenXR-SDK tag")

FetchContent_Declare(
  OpenXR-SDK
  GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
  GIT_TAG        ${OPENXR_SDK_TAG}
  GIT_SHALLOW    TRUE
)

set(BUILD_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_CONFORMANCE_TESTS OFF CACHE BOOL "" FORCE)
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(OpenXR-SDK)

add_library(fuvr_openxr_headers INTERFACE)
target_include_directories(fuvr_openxr_headers INTERFACE
  ${openxr-sdk_SOURCE_DIR}/include
  ${openxr-sdk_SOURCE_DIR}/src/common
)

set(FUVR_RUNTIME_SOURCES
  src/runtime.cpp
  src/dispatch.cpp
  src/instance.cpp
  src/session.cpp
  src/swapchain.cpp
  src/actions.cpp
  src/loader_export.cpp
  src/pose_predictor.cpp
  src/frame_sink.cpp
  src/daemon_client.cpp
  src/iosurface_swapchain.mm
)

add_library(fuvr_openxr_runtime MODULE ${FUVR_RUNTIME_SOURCES})

set_target_properties(fuvr_openxr_runtime PROPERTIES
  OUTPUT_NAME "fuvr_openxr_runtime"
  PREFIX "lib"
  SUFFIX ".dylib"
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(fuvr_openxr_runtime PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(fuvr_openxr_runtime
  PRIVATE
    fuvr_proto
    fuvr_warnings
    fuvr_openxr_headers
    "-framework Metal"
    "-framework IOSurface"
    "-framework CoreFoundation"
    "-framework Foundation"
)

target_compile_definitions(fuvr_openxr_runtime PRIVATE
  XR_OS_APPLE
)

# Why: -fno-exceptions trips Cap'n Proto's KJ which uses kj::Exception
# control flow internally. Limit the no-exceptions flag to non-Cap'n code.
set_source_files_properties(src/daemon_client.cpp PROPERTIES
  COMPILE_FLAGS "-fexceptions"
)
set_source_files_properties(src/iosurface_swapchain.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc"
)

add_executable(fuvr-register src/register_runtime.cpp)
target_link_libraries(fuvr-register PRIVATE fuvr_warnings)
target_include_directories(fuvr-register PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

option(FUVR_RUNTIME_TESTS "Build runtime-macos unit tests" ON)

if(FUVR_RUNTIME_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()
  add_executable(fuvr_runtime_tests
    tests/test_pose_predictor.cpp
    tests/test_daemon_client.cpp
    tests/test_iosurface_handoff.cpp
    src/pose_predictor.cpp
    src/daemon_client.cpp
    src/iosurface_swapchain.mm
  )
  target_include_directories(fuvr_runtime_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )
  target_link_libraries(fuvr_runtime_tests
    PRIVATE
      fuvr_proto
      fuvr_openxr_headers
      GTest::gtest_main
      "-framework Metal"
      "-framework IOSurface"
      "-framework CoreFoundation"
      "-framework Foundation"
  )
  target_compile_options(fuvr_runtime_tests PRIVATE
    -Wall -Wextra -Wpedantic -fno-rtti
  )
  set_source_files_properties(src/iosurface_swapchain.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc"
  )
  include(GoogleTest)
  gtest_discover_tests(fuvr_runtime_tests)
endif()
