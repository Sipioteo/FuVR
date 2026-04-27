# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.22)
project(fuvr_quest_host_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# fragment_reassembler.cpp depends only on proto_codec.hpp's flag constants,
# not on the Cap'n Proto runtime. We compile it standalone here; no linkage
# against capnp/kj is required.
add_executable(test_fragment_reassembly
    test_fragment_reassembly.cpp
    ../fragment_reassembler.cpp
)
target_include_directories(test_fragment_reassembly PRIVATE ..)

# clock_sync depends only on <chrono>; no Android or capnp linkage needed.
add_executable(test_clock_sync
    test_clock_sync.cpp
    ../clock_sync.cpp
)
target_include_directories(test_clock_sync PRIVATE ..)

# Pure-logic input packer (Task 1).
add_executable(test_input_packing
    test_input_packing.cpp
    ../input_packer.cpp
)
target_include_directories(test_input_packing PRIVATE ..)

# Metrics line formatter (Task 5).
add_executable(test_metrics_format
    test_metrics_format.cpp
    ../metrics_format.cpp
)
target_include_directories(test_metrics_format PRIVATE ..)

# Adaptive bitrate / keyframe request tracker (Task 6).
add_executable(test_loss_tracker
    test_loss_tracker.cpp
    ../loss_tracker.cpp
)
target_include_directories(test_loss_tracker PRIVATE ..)

# Hand-tracking joint encoder (Task 4).
add_executable(test_hand_encoder
    test_hand_encoder.cpp
    ../hand_encoder.cpp
)
target_include_directories(test_hand_encoder PRIVATE ..)

# Connection-UI rasterizer (Task 3).
add_executable(test_connection_ui
    test_connection_ui.cpp
    ../connection_ui.cpp
)
target_include_directories(test_connection_ui PRIVATE ..)

# Stage-offset math used by Task 2.
add_executable(test_local_origin_math
    test_local_origin_math.cpp
)
target_include_directories(test_local_origin_math PRIVATE ..)

# ATW reprojection math (eye_blit fragment shader replicated in C++) +
# the shared quat helpers from quat_math.hpp. Header-only, no Android linkage.
add_executable(test_atw_math
    test_atw_math.cpp
)
target_include_directories(test_atw_math PRIVATE ..)
if(COMMAND add_test)
    add_test(NAME test_atw_math COMMAND test_atw_math)
endif()
