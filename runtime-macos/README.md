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

### Event queue lifecycle

Each `Instance` owns a thread-safe MPSC `EventQueue` (capacity 64). The
daemon-client reader thread is the producer (session-state and
loss-pending events on connect/disconnect); the application's main thread
is the consumer via `xrPollEvent`. On overflow the oldest entries are
dropped and the next `pop` returns an `XR_TYPE_EVENT_DATA_EVENTS_LOST`
event. Connect emits `IDLE -> READY`; `xrBeginSession` emits
`SYNCHRONIZED -> VISIBLE -> FOCUSED` plus a one-shot
`INTERACTION_PROFILE_CHANGED`; `xrEndSession` emits `STOPPING -> IDLE`;
daemon disconnect emits `LOSS_PENDING`.

### Reference spaces

`xrCreateReferenceSpace` accepts `VIEW`, `LOCAL`, and `STAGE`; anything
else returns `XR_ERROR_REFERENCE_SPACE_UNSUPPORTED`. `xrLocateSpace` for a
`VIEW` space relative to `LOCAL`/`STAGE` queries the same `PosePredictor`
used by `xrLocateViews` and returns position+orientation valid+tracked
flags. `STAGE` is identity for now (real Stage tracking deferred).
`xrCreateActionSpace` succeeds and `xrLocateSpace` against it returns
`XR_SUCCESS` with cleared validity bits — controller pose forwarding from
the daemon is a pass 4 item (`PoseSnapshot` only carries HMD pose today).

### Encoder stats path

The daemon emits `EncodeStats` envelopes per frame after VideoToolbox
finishes. The reader thread inside `DaemonClient` decodes them and pushes
into a per-`Session` rolling 256-frame `EncoderStats` window
(encodeDurationNs, encodedSizeBytes, arrival time). `Session::encoderStats
Snapshot()` returns mean/p95 encode ms, fps from inter-arrival, and
bitrate. The `fuvr-runtime-metrics` CLI is a back-door consumer linked
against the runtime sources via `fuvr/internal/diag.hpp`.

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

## Daemon split (ADR-0002)

The runtime is loaded into the app's address space by the standard Khronos
OpenXR loader. Long-running work (transport, encoder, virtual display) lives
in a separate daemon `fuvrd`. The runtime opens a Cap'n Proto Unix-domain
socket connection to the daemon at:

- `$XDG_RUNTIME_DIR/fuvr/rpc.sock`, or
- `~/Library/Caches/fuvr/rpc.sock` as a fallback.

Connection is lazy and uses exponential backoff up to 1 s. When the daemon is
absent the runtime still functions enough for registration/lifecycle work
(`fuvr-register`, `xrCreateInstance`, extension enumeration, unit tests). For
streaming, `fuvrd` must be running.

### Frame submission protocol

Per ADR-0007 the IOSurface handoff goes over a parallel XPC mach service
(`com.fuvr.daemon.surface`), not the UDS Cap'n Proto socket — SCM_RIGHTS on
macOS is fd-only and cannot carry mach send-rights. `DaemonFrameSink::submit`
(see `src/frame_sink.cpp`) does:

1. Mints a per-frame correlation `token`.
2. `IOSurfaceXpcClient::sendSurface(token, surface)` — calls
   `IOSurfaceCreateMachPort`, packs it into an `xpc_dictionary` with
   `xpc_dictionary_set_mach_send`, sends to the daemon, and immediately
   `mach_port_deallocate`s the local right (XPC has internalised it).
3. `DaemonClient::submitFrame` — sends the `SubmitFrameRequest` envelope on
   the UDS with the same `surfaceToken` plus pose metadata.

The daemon's grace window (16 ms) handles either ordering; the XPC-first
order above is just an optimisation.

### Test bypass: `FUVR_INPROCESS_HANDOFF`

Setting `FUVR_INPROCESS_HANDOFF=1` makes the runtime skip the XPC connection
and instead `put` IOSurfaces into a process-local registry
(`fuvr/inprocess_surface_registry.hpp`). The daemon's bridge takes from the
same registry. Used by the unit tests, which run runtime+daemon in one
process where launchd cannot register a mach service for us.

### XR_FUVR_metal_enable

Apps select the Metal graphics binding by chaining
`XrGraphicsBindingMetalFUVR { mtlDevice }` into `XrSessionCreateInfo::next`.
Swapchain images are returned as `XrSwapchainImageMetalFUVR { texture }` —
each `texture` is an `id<MTLTexture>` backed by an `IOSurfaceRef` allocated
by the runtime. The runtime owns the IOSurface for the lifetime of the
swapchain (released on `xrDestroySwapchain`).

## Tests

```sh
cmake --build build --target fuvr_runtime_tests
ctest --test-dir build --output-on-failure -R pose_predictor
```
