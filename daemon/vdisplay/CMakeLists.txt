# SPDX-License-Identifier: Apache-2.0
#
# Virtual-display capture subdir owned by THETA. Wired into the daemon top-level
# via a single `add_subdirectory(vdisplay)` line at the end of
# daemon/CMakeLists.txt and via the integration patch in INTEGRATION.md.

add_library(fuvr_daemon_vdisplay STATIC
  src/sck_capture.mm
  src/virtual_display_session.cpp
)

target_include_directories(fuvr_daemon_vdisplay PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(fuvr_daemon_vdisplay PRIVATE
  ${CMAKE_SOURCE_DIR}/virtual-display-helper/include
)

target_link_libraries(fuvr_daemon_vdisplay PUBLIC
  fuvr_warnings
  fuvr_vdisplay_control
  "-framework CoreVideo"
  "-framework CoreMedia"
  "-framework CoreGraphics"
  "-framework Foundation"
  "-framework ScreenCaptureKit"
)

set_source_files_properties(src/sck_capture.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc"
)

option(FUVR_VDISPLAY_DAEMON_TESTS "Build daemon/vdisplay tests" ON)
if(FUVR_VDISPLAY_DAEMON_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(fuvr_vdisplay_daemon_tests
    tests/test_virtual_display_session.cpp
    tests/test_sck_capture.cpp
  )
  target_link_libraries(fuvr_vdisplay_daemon_tests PRIVATE
    fuvr_daemon_vdisplay
    GTest::gtest_main
  )
  include(GoogleTest)
  gtest_discover_tests(fuvr_vdisplay_daemon_tests)
endif()
