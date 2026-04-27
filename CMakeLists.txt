cmake_minimum_required(VERSION 3.27)
project(fuvr LANGUAGES C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)

if(NOT APPLE)
  message(FATAL_ERROR "fuvr is macOS-only on the host side")
endif()

# Enforce arm64-only host builds. The Quest side is built separately via Gradle.
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
endif()

# Common warning flags for first-party C++ targets.
add_library(fuvr_warnings INTERFACE)
target_compile_options(fuvr_warnings INTERFACE
  -Wall -Wextra -Wpedantic
  -Wno-nullability-completeness
  -fno-rtti -fno-exceptions
)

# Cap'n Proto generated bindings live under proto/gen/cpp after running
# scripts/gen-proto.sh. We expose them as an INTERFACE library so each
# component can `target_link_libraries(... PRIVATE fuvr_proto)`.
add_library(fuvr_proto INTERFACE)
target_include_directories(fuvr_proto INTERFACE
  ${CMAKE_SOURCE_DIR}/proto/gen/cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CAPNP REQUIRED capnp)
target_include_directories(fuvr_proto INTERFACE ${CAPNP_INCLUDE_DIRS})
target_link_directories(fuvr_proto INTERFACE ${CAPNP_LIBRARY_DIRS})
target_link_libraries(fuvr_proto INTERFACE ${CAPNP_LIBRARIES})

include(CTest)
enable_testing()

# Component subdirectories. Each is independently buildable.
add_subdirectory(encoder-macos)
add_subdirectory(runtime-macos)
add_subdirectory(virtual-display-helper)
add_subdirectory(daemon)
