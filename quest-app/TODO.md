# quest-app TODO

Tracks the work still pending after the M0 skeleton lands.

## Build

- [ ] Vendor the official Gradle wrapper jar (`gradle/wrapper/gradle-wrapper.jar`)
      via `gradle wrapper --gradle-version 8.7`. Current `gradlew` is a stub
      that execs system `gradle`.
- [ ] Verify `capnp` toolchain availability in CI; pin Cap'n Proto runtime
      version and add a `find_package(CapnProto)` block in `CMakeLists.txt`
      once we link the generated sources.
- [ ] Add a `prefab` block for the Cap'n Proto C++ runtime (or vendor it
      under `app/src/main/cpp/third_party/`).

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
