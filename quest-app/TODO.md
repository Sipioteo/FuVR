# quest-app TODO

Tracks the work still pending after the M0 skeleton lands.

## Build

- [ ] Vendor the official Gradle wrapper jar (`gradle/wrapper/gradle-wrapper.jar`)
      via `gradle wrapper --gradle-version 8.7`. Current `gradlew` is a stub
      that execs system `gradle`.
- [x] Pass 2: Cap'n Proto runtime vendored via `ExternalProject_Add`
      (release-1.0.2) inside `app/src/main/cpp/CMakeLists.txt`. We chose
      ExternalProject over a hypothetical `io.github.eclipse-capnproto:capnp-android`
      AAR because no such Maven artifact exists at the time of writing;
      this keeps the build hermetic and the runtime version pinned in tree.
- [ ] Add a SHA-256 hash to the `ExternalProject_Add(URL_HASH ...)` once a
      stable mirror is in place, to harden against upstream artifact rewrites.
- [ ] Cross-compile Cap'n Proto only the runtime (`libcapnp.a`, `libkj.a`):
      `BUILD_TOOLS=OFF` is set, but verify the static-lib paths land where
      `IMPORTED_LOCATION` expects (`<install>/lib/libcapnp.a`) on AGP CMake.

## OpenXR

- [ ] Wire `XR_KHR_composition_layer_color_scale_bias` and
      `XR_FB_swapchain_update_state` as optional extensions (advertise +
      detect at runtime).
- [ ] Real per-eye swapchain creation in `Compositor::create_swapchains`
      using `XrViewConfigurationView` extents and `GL_SRGB8_ALPHA8`.
- [ ] Acquire/wait/release swapchain images per frame and blit the decoded
      external texture into each eye's framebuffer with a dedicated
      shader (sampler2DOES sampler).
- [ ] Action handling beyond `xrSyncActions`: read state via
      `xrGetActionStateFloat` / `xrGetActionStateBoolean` / `xrGetActionStateVector2f`
      and feed them into `pose_forwarder` for the upstream input list.
- [ ] Placeholder "Connecting…" quad swapchain: create a 1x1 swapchain
      with a black-filled image so `build_placeholder_layer` is non-null.

## Decoder

- [ ] Output to `AImageReader` configured with
      `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE`, then back the MediaCodec
      surface with the AImageReader's window. This is what gives us
      AHardwareBuffer-backed AImages for zero-copy.
- [ ] Feed CSD (VPS/SPS/PPS) from the control channel before queueing
      regular fragments; set `BUFFER_FLAG_CODEC_CONFIG` accordingly.
- [ ] Reassemble fragments per `VideoFragmentHeader.fragmentIndex` into
      a single access unit before queueing into MediaCodec.
- [ ] Drop policy: when the queue grows past N pending AUs, drop until
      the next IDR (request one via control channel if missing).

## Transport

- [ ] UDP mode (configurable) with the same length-prefixed framing
      adapted to datagrams; FEC integration to mirror Rust crate.
- [ ] Backpressure on `send()` (current code blocks the caller).
- [ ] Reconnect loop with exponential backoff.

## Pose forwarder

- [ ] Cap'n Proto serialization of `UpstreamFrame` once `proto_gen/` is
      populated. Allocate a reusable `MallocMessageBuilder` per thread.
- [ ] Bind `xrGetActionStateFloat/Boolean/Vector2f` outputs into
      `TouchInputState` according to `proto/fuvr.capnp`.
- [ ] Replace `sleep_until` busy loop with a higher-precision timer; the
      Quest scheduler can drift several hundred microseconds.

## Compositor

- [ ] Side-by-side cropping: pass `imageRect` per eye so the left view
      samples `[0, w/2]` and the right `[w/2, w]` of the decoded frame.
- [ ] Apply ATW-friendly `renderedPose` from `VideoFragmentHeader`: the
      OpenXR system compositor uses `XrCompositionLayerProjectionView.pose`
      to reproject; we must report the pose the Mac actually used.

## App lifecycle

- [ ] Honor `APP_CMD_SAVE_STATE` / pause-resume properly so the session
      survives a quick controller put-down without tearing down EGL.
- [ ] Wire `XR_FB_display_refresh_rate` to negotiate 72/90/120 Hz with
      the Mac via the `control` channel.

## Pass 2 (Cap'n Proto + per-eye swapchains + side-by-side blit) — landed

Implemented in this pass:

- Cap'n Proto wire integration (`proto_codec.{hpp,cpp}`) for `UpstreamFrame`
  outbound, `VideoFragmentHeader` + `ControlMessage` inbound, all packed
  framing through the existing `[u32 BE][u8 channel][packed]` envelope. The
  capnp runtime is built locally with exceptions and RTTI; the rest of the
  app remains `-fno-exceptions -fno-rtti`.
- Channel ids match `transport-core/src/channel.rs` exactly
  (`Video=0, Audio=1, Pose=2, Input=3, Haptics=4, Control=5`); the alignment
  is documented in `transport_client.hpp`.
- `helloFromQuest` is sent on connect with Quest 3 capabilities
  (`2064×2208`, refresh `[72, 90, 120]`, codecs `[hevc, h264]`,
  `hasHandTracking=true`, `hasEyeTracking=false`).
- `clockSync` ping is echoed back as pong; `haptic` is forwarded to
  `xrApplyHapticFeedback` on the new `haptic` action bound to both
  `/user/hand/{left,right}/output/haptic`.
- `FragmentReassembler` keys partial AUs by `frameId`, accepts out-of-order
  fragments, deduplicates, and bounds memory by evicting partials older than
  `kMaxInflight = 8` frames. Validated by `tests/test_fragment_reassembly.cpp`.
- Per-eye `XrSwapchain`s created from the actual
  `XrViewConfigurationView::recommendedImageRect{Width,Height}` (no longer
  hardcoded), format chosen as `GL_SRGB8_ALPHA8` if available else `GL_RGBA8`,
  with `acquire/wait/release` driven inside the compositor's `render_eye`.
- `eye_blit.{hpp,cpp}` runs a single fullscreen-triangle GLES3 program that
  samples a `samplerExternalOES` with per-eye `(uUvOffset, uUvScale)` to
  cover the left or right half of the side-by-side decoded frame. GL state
  (program, vao, texture binding, blend, scissor, depth, cull) is saved and
  restored around the blit; FBO + viewport are saved/restored in the
  compositor's `render_eye` since those are the surface-level state that the
  OpenXR runtime cares about.

Device-side smoke that cannot be exercised from the coordinator sandbox:

- Actually loading the .so on a Quest and confirming the OpenXR session
  reaches `READY`, swapchain images are non-null, and the projection layer
  composites without dropped frames.
- End-to-end with the Mac side: `helloFromQuest` -> `helloFromMac` ->
  fragmented HEVC video flowing -> per-eye blit visually correct (no
  cross-eye bleed, IPD plausible).
- Verifying `xrApplyHapticFeedback` actually fires on both controllers when
  the Mac sends a `haptic` control message.
- Confirming the Cap'n Proto static libs land at
  `<build>/capnp/install/lib/libcapnp.a` after the AGP CMake invokes
  ExternalProject; AGP's nested CMake builds sometimes need the
  `BUILD_BYPRODUCTS` paths matched exactly to satisfy Ninja's missing-input
  detection.
