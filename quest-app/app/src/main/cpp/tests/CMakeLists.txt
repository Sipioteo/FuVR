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
