# SPDX-License-Identifier: Apache-2.0

add_executable(fuvr_vdisplay_helper
  src/helper_main.mm
)
set_target_properties(fuvr_vdisplay_helper PROPERTIES
  OUTPUT_NAME "fuvr-vdisplay-helper"
)
target_include_directories(fuvr_vdisplay_helper PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(fuvr_vdisplay_helper PRIVATE
  fuvr_warnings
  "-framework CoreGraphics"
  "-framework Foundation"
)
target_compile_options(fuvr_vdisplay_helper PRIVATE -fobjc-arc)

add_library(fuvr_vdisplay_control STATIC
  src/control.mm
)
target_include_directories(fuvr_vdisplay_control PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(fuvr_vdisplay_control PRIVATE fuvr_warnings)
target_compile_options(fuvr_vdisplay_control PRIVATE -fobjc-arc)
target_compile_definitions(fuvr_vdisplay_control PRIVATE
  FUVR_VDISPLAY_HELPER_PATH="$<TARGET_FILE:fuvr_vdisplay_helper>"
)
add_dependencies(fuvr_vdisplay_control fuvr_vdisplay_helper)

option(FUVR_VDISPLAY_TESTS "Build virtual-display-helper tests" ON)
if(FUVR_VDISPLAY_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(fuvr_vdisplay_tests tests/test_vdisplay_spawn.cpp)
  target_link_libraries(fuvr_vdisplay_tests PRIVATE
    fuvr_vdisplay_control
    GTest::gtest_main
  )
  target_include_directories(fuvr_vdisplay_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
  )
  include(GoogleTest)
  gtest_discover_tests(fuvr_vdisplay_tests)
endif()
