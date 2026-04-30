# SPDX-License-Identifier: Apache-2.0
#
# Mock libopenvr_api.dylib — a drop-in replacement for Valve's OpenVR
# library that translates SteamVR API calls into FuVR daemon RPC.
#
# Built as a Universal Binary (x86_64 + arm64) so legacy Mac SteamVR titles
# running through Rosetta 2 can dlopen() it transparently.
#
# IPC strategy: a dedicated `/tmp/fuvr_openvr.sock` Unix domain socket with a
# small hand-rolled binary protocol (see src/wire.hpp). IOSurface handoff
# rides the existing `com.fuvr.daemon.surface` XPC mach service.
# Cap'n Proto is intentionally NOT a dependency here — Homebrew's capnp ships
# arm64-only and a universal build would require fetching+building from
# source for both arches.

cmake_minimum_required(VERSION 3.27)

# Universal-binary scoping: the parent project pins arm64-only globally for
# its native targets (which depend on arm64-only static libs like the Rust
# transport FFI and Homebrew opus). We make ONLY this target universal by
# setting `OSX_ARCHITECTURES` per-target below — never with a global cache
# override, which would force every sibling to attempt x86_64 too and fail.

set(OPENVR_SHIM_SOURCES
  src/vr_entry.cpp
  src/mock_state.cpp
  src/mock_system.cpp
  src/mock_compositor.cpp
  src/mock_input.cpp
  src/mock_chaperone.cpp
  src/mock_misc.cpp
  # FnTable wrappers — LWJGL OpenVR bindings expect a flat struct of
  # plain C function pointers per interface ("FnTable:" lookup prefix).
  # The C++ vtable layout cannot be reused because it requires an
  # implicit `this` arg. Each fntable_*.cpp wraps every virtual method
  # of one IVR* class with an `extern "C"` thunk and exports a
  # singleton struct in declaration order.
  src/fntable_compositor.cpp
  src/fntable_system.cpp
  src/fntable_input.cpp
  src/fntable_chaperone.cpp
  src/fntable_applications.cpp
  src/fntable_rendermodels.cpp
  src/fntable_settings.cpp
  # JNI dispatch trampolines that LWJGL 3.3.3's stock liblwjgl.dylib does
  # not export but Vivecraft's custom openvr binding calls. Linking them
  # into libopenvr_api.dylib makes them visible to the JVM's native-method
  # lookup the moment the game `System.loadLibrary("lwjgl_openvr")`s us.
  src/lwjgl_jni_trampolines.c
  # Per-method JNI thunks for the 10 openvr methods LWJGL declares as
  # `private static native` directly (bypassing JNI.callXXX dispatch).
  # Without these, the JVM throws UnsatisfiedLinkError for symbols like
  # Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetHiddenAreaMesh on first
  # use. See file header for the canonical list and ABI notes.
  src/lwjgl_openvr_natives.c
  src/daemon_rpc.cpp
  src/pose_math.cpp
  src/iosurface_xpc.mm
  src/texture_bridge_gl.mm
  src/texture_bridge_metal.mm
  src/log.cpp
)

# The output is named exactly as Valve's library so games dlopen() it
# verbatim. Place it in the build tree under `lib/openvr/`; the install
# step (TODO) copies it next to the user's game.
add_library(openvr_api SHARED ${OPENVR_SHIM_SOURCES})

set_target_properties(openvr_api PROPERTIES
  OUTPUT_NAME "openvr_api"
  PREFIX "lib"
  SUFFIX ".dylib"
  POSITION_INDEPENDENT_CODE ON
  # Universal binary — applied here so it is scoped to this target and
  # does not contaminate sibling targets.
  OSX_ARCHITECTURES "x86_64;arm64"
  # Match Valve's install_name so games that rely on absolute @rpath lookups
  # still resolve. Override with `install_name_tool` at deploy time if
  # needed.
  INSTALL_NAME_DIR "@rpath"
)

target_include_directories(openvr_api PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr
)

# JNI headers — needed by `lwjgl_jni_trampolines.c`. We pick whichever
# JDK is reachable via `java_home`. The trampolines never call back into
# Java (they only forward to a function pointer), so JDK version is
# irrelevant to runtime correctness.
find_package(JNI)
if(JNI_FOUND)
  target_include_directories(openvr_api PRIVATE ${JNI_INCLUDE_DIRS})
else()
  message(FATAL_ERROR "JNI headers not found — install a JDK and re-run cmake")
endif()

target_compile_definitions(openvr_api PRIVATE
  VR_API_EXPORT=1
)

# Why -fobjc-arc only on .mm files: keeps the C++ TUs lightweight and matches
# the rest of the FuVR codebase pattern (see runtime-macos/CMakeLists.txt).
set_source_files_properties(
  src/texture_bridge_gl.mm
  src/texture_bridge_metal.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)
# iosurface_xpc.mm uses xpc_release(), which ARC disallows since macOS 10.15
# (XPC objects became NSObject subclasses). Match runtime-macos's pattern
# and keep this single file under manual retain/release semantics.
set_source_files_properties(
  src/iosurface_xpc.mm
  PROPERTIES COMPILE_FLAGS "-fno-objc-arc"
)

# OpenVR's interface classes use virtual dispatch; we cannot disable RTTI in
# the same blanket way the rest of the project does because some games may
# probe `dynamic_cast` on the returned interface pointers. Exceptions stay
# off — Cocoa / Metal / GL APIs do not throw.
target_compile_options(openvr_api PRIVATE
  -Wall -Wextra
  -Wno-nullability-completeness
  -Wno-unused-parameter
  -fno-exceptions
)
# Apply -std=c++17 only to C++ TUs — the lwjgl_jni_trampolines.c file is
# C99 and clang rejects -std=c++17 for C inputs.
target_compile_options(openvr_api PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>
)

target_link_libraries(openvr_api PRIVATE
  "-framework Foundation"
  "-framework CoreFoundation"
  "-framework IOSurface"
  "-framework Metal"
  "-framework OpenGL"
  "-framework CoreGraphics"
)

# Hide unused symbols — keeps the export table to just `VR_*` C entry points
# plus the abstract interface vtables, mimicking Valve's default visibility.
set_target_properties(openvr_api PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  C_VISIBILITY_PRESET   hidden
  VISIBILITY_INLINES_HIDDEN ON
)

# OpenGL is deprecated on macOS 10.14+; silence the noise.
target_compile_definitions(openvr_api PRIVATE GL_SILENCE_DEPRECATION)

# Stage the produced dylib into the SwiftUI app's resource bundle so the
# user never has to copy files by hand. The mac-app's `Package.swift` ships
# `Resources/openvr/` as a `.copy(...)` resource; the runtime helper
# `FuVRControlBundle.openvrShimPath()` returns this path.
set(FUVR_SHIM_BUNDLE_DIR
  "${CMAKE_SOURCE_DIR}/mac-app/Sources/FuVRControl/Resources/openvr"
)
add_custom_command(TARGET openvr_api POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${FUVR_SHIM_BUNDLE_DIR}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "$<TARGET_FILE:openvr_api>"
          "${FUVR_SHIM_BUNDLE_DIR}/libopenvr_api.dylib"
  COMMENT "Staging libopenvr_api.dylib into mac-app resource bundle"
)

# Quick smoke test that the produced dylib actually exports the required
# symbols (VR_Init, VR_Shutdown, VR_GetGenericInterface…) and is universal.
add_test(
  NAME openvr_shim_symbols
  COMMAND ${CMAKE_COMMAND}
    -DDYLIB=$<TARGET_FILE:openvr_api>
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_symbols.cmake
)
