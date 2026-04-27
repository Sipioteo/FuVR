# SPDX-License-Identifier: Apache-2.0

add_library(fuvr_daemon_core STATIC
  src/daemon.cpp
  src/rpc_server.cpp
  src/session.cpp
  src/iosurface_bridge.mm
  src/iosurface_xpc_service.mm
  src/pose_router.cpp
  src/metrics.cpp
  src/transport_ffi_glue.cpp
  src/json_bridge.cpp
  src/clock_sync.cpp
)

target_include_directories(fuvr_daemon_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(fuvr_daemon_core PRIVATE
  ${CMAKE_SOURCE_DIR}/proto/gen/cpp
  ${CMAKE_SOURCE_DIR}/transport/transport-ffi/include
  ${CMAKE_SOURCE_DIR}/virtual-display-helper/include
)

target_link_libraries(fuvr_daemon_core PUBLIC
  fuvr_proto
  fuvr_warnings
  fuvr_encoder
  fuvr_vdisplay_control
  fuvr_inprocess_handoff
  "-framework CoreVideo"
  "-framework IOSurface"
  "-framework CoreFoundation"
  "-framework Foundation"
)
# Why: XPC ships in libSystem (no separate framework needed) but is implicit
# only via Foundation linkage above; keep this comment so future readers know
# we did not forget an `-framework XPC`.

set_source_files_properties(src/iosurface_bridge.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc -fexceptions"
)
set_source_files_properties(src/iosurface_xpc_service.mm PROPERTIES
  COMPILE_FLAGS "-fno-objc-arc"
)

find_library(FUVR_TRANSPORT_LIB fuvr_transport
  HINTS ${CMAKE_SOURCE_DIR}/transport/target/release
        ${CMAKE_SOURCE_DIR}/transport/target/debug
)
if(FUVR_TRANSPORT_LIB)
  message(STATUS "fuvrd: linking transport at ${FUVR_TRANSPORT_LIB}")
  target_link_libraries(fuvr_daemon_core PUBLIC ${FUVR_TRANSPORT_LIB})
else()
  message(STATUS "fuvrd: libfuvr_transport not built; stubbing transport calls")
  target_compile_definitions(fuvr_daemon_core PUBLIC FUVR_DAEMON_NO_TRANSPORT=1)
endif()

add_executable(fuvrd src/main.cpp)
target_link_libraries(fuvrd PRIVATE fuvr_daemon_core)

option(FUVR_DAEMON_TESTS "Build fuvrd tests" ON)
if(FUVR_DAEMON_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(fuvr_daemon_tests
    tests/test_metrics.cpp
    tests/test_rpc_envelope.cpp
    tests/test_pose_router.cpp
    tests/test_iosurface_xpc.cpp
    tests/test_clock_sync.cpp
    tests/test_encode_stats_forward.cpp
  )
  target_link_libraries(fuvr_daemon_tests PRIVATE
    fuvr_daemon_core
    GTest::gtest_main
  )
  target_include_directories(fuvr_daemon_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/proto/gen/cpp
  )
  enable_testing()
  add_test(NAME fuvr_daemon_tests COMMAND fuvr_daemon_tests)
endif()
