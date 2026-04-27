# runtime-macos

OpenXR 1.1 runtime for macOS. This is the in-process library that XR
applications on the Mac side load (via the standard Khronos OpenXR loader)
in order to render to a Meta Quest over the FuVR transport.

## Architecture

```
+---------------------+        +-----------------------+
|  XR App (Blender,   |        |  Khronos OpenXR       |
|  Godot, Unity, ...) | <----> |  loader (libopenxr_   |
+---------------------+        |  loader.dylib)        |
                               +----------+------------+
                                          |
                                          v xrNegotiateLoaderRuntimeInterface
                               +-----------------------+
                               |  libfuvr_openxr_      |
                               |  runtime.dylib        |
                               |                       |
                               |  +-----------------+  |
                               |  | dispatch.cpp    |  |
                               |  | xrGetInstance-  |  |
                               |  | ProcAddr table  |  |
                               |  +--------+--------+  |
                               |           |           |
                               |  +--------v--------+  |
                               |  | instance/sess./ |  |
                               |  | swapchain/      |  |
                               |  | actions/frame   |  |
                               |  +--------+--------+  |
                               |           |           |
                               |  +--------v--------+  |
                               |  | PosePredictor   |  | <-- pose samples
                               |  | (ring buffer)   |  |     from Quest
                               |  +-----------------+  |
                               |                       |
                               |  +-----------------+  |
                               |  | FrameSink iface |--+--> encoder-macos
                               |  | (NullFrameSink) |  |    + transport
                               |  +-----------------+  |
                               +-----------------------+
```

The runtime is a CMake `MODULE` target named `fuvr_openxr_runtime` and is
emitted as `libfuvr_openxr_runtime.dylib`.

### Frame loop

`xrWaitFrame` returns a synthetic predicted display time at the configured
refresh rate. `xrEndFrame` invokes `FrameSink::submit` with the swapchain
references and the target display time. The default `NullFrameSink` drops
frames; the encoder/transport components plug in a real sink that owns the
Metal capture + VideoToolbox + UDP/ADB pipeline (see `SPEC.md` §3.1.3 and
§3.1.4).

### Pose flow

The transport layer (Quest -> Mac) feeds `PosePredictor::push` with
`HmdPoseSample` entries. When the app calls `xrLocateViews` with a future
`displayTime`, the predictor extrapolates linearly using a 4-sample window
plus orientation extrapolation via SLERP derivative.

### Extensions

- `XR_KHR_vulkan_enable2` (reserved): the cross-platform path via MoltenVK.
- `XR_FUVR_metal_enable` (vendor, draft): native Metal integration we plan
  to pitch to Khronos as an industry extension.

## Registering as the active runtime

Build `fuvr-register` and run it pointing at the dylib produced by the
build:

```sh
cmake -S . -B build -G Ninja
cmake --build build --target fuvr-register fuvr_openxr_runtime
./build/runtime-macos/fuvr-register \
    "$PWD/build/runtime-macos/libfuvr_openxr_runtime.dylib"
```

This writes `~/Library/Application Support/OpenXR/1/active_runtime.json`
pointing at the dylib (idempotent — re-running just rewrites the file).

To unregister:

```sh
./build/runtime-macos/fuvr-register --unregister
```

## Tests

```sh
cmake --build build --target fuvr_runtime_tests
ctest --test-dir build --output-on-failure -R pose_predictor
```
