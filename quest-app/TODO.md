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

- [x] Pass 3: Output to `AImageReader` configured with
      `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
      AHARDWAREBUFFER_USAGE_VIDEO_DECODE_OUTPUT_BUFFER`,
      `AIMAGE_FORMAT_PRIVATE`, max-images=4. MediaCodec is configured with
      the reader's `ANativeWindow`; the on-image listener acquires the
      latest AImage, extracts the AHardwareBuffer (`AHardwareBuffer_acquire`
      to own a ref independent of the AImage), stores it in a single-slot
      drop-old queue, and releases the previously-buffered ref. The
      compositor consumes via `pop_latest`, binds an EGLImage as
      `samplerExternalOES`, immediately destroys the EGLImage (the GL
      texture keeps the buffer alive), and releases its ref.
- [ ] Feed CSD (VPS/SPS/PPS) from the control channel before queueing
      regular fragments; set `BUFFER_FLAG_CODEC_CONFIG` accordingly.
- [x] Pass 2: Reassemble fragments per `VideoFragmentHeader.fragmentIndex`
      into a single access unit before queueing into MediaCodec
      (`FragmentReassembler`).
- [ ] Drop policy: when the queue grows past N pending AUs, drop until
      the next IDR (request one via control channel if missing).
- [ ] Restart MediaCodec when SessionConfig dimensions disagree with
      the AImageReader's. Pass 3 stores the negotiated size as a hint via
      `DecoderPipeline::set_output_size` but does not yet hot-restart.
- [ ] Device-side smoke: actually exercise the AImageReader callback +
      EGLImage path on a Quest. AImageReader, AHardwareBuffer, EGL
      ANDROID-extension behavior cannot be unit-tested on the host (the
      symbols aren't available in the desktop NDK headers). Track in a
      device-only integration test once we have an instrumented run.

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

## Pass 3 (decoder→compositor zero-copy + clock sync responder + metrics) — landed

Implemented in this pass:

- `DecoderPipeline` now owns an `AImageReader` (format
  `AIMAGE_FORMAT_PRIVATE`, usage `GPU_SAMPLED_IMAGE |
  VIDEO_DECODE_OUTPUT_BUFFER`, max images = 4) and configures MediaCodec
  with the reader's `ANativeWindow`. Decoded frames arrive via the
  image-available listener, are unwrapped to `AHardwareBuffer*` with an
  explicit `AHardwareBuffer_acquire` (since `AImage_getHardwareBuffer`
  does not transfer ownership), and parked in a single-slot drop-old
  queue. `pop_latest()` hands the caller the live ref.
- `Compositor::submit_frame` now consumes that ref: builds the EGLImage
  via `eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID, ...)`, binds the
  external texture with `glEGLImageTargetTexture2DOES`, destroys the
  EGLImage immediately (the texture keeps the buffer alive), and then
  releases the AHardwareBuffer ref the decoder produced. No double-
  acquire, no buffer leak across frames.
- `ClockSyncResponder` (`clock_sync.{hpp,cpp}`) responds to ping arms with
  `Pong{ t0, t1=now_ns, t2=now_ns }`, where `t1` is the receive instant
  and `t2` is sampled immediately before the wire send. `t2 >= t1` is
  asserted; the cross-epoch difference is the whole point of the protocol.
- Decoder metrics (rolling 256-frame window): `fps` from inter-arrival
  intervals at the AImageReader callback, `decode_p95_ms` from the
  per-PTS map between `queueInputBuffer` and the AImageReader callback.
  `ProtocolRouter::send_metrics_if_due` emits every 100 ms via the
  `ControlMessage.error :Text` arm with prefix `q-metrics: ` (see below).
- Host tests: `tests/test_clock_sync.cpp` covers `build_pong`,
  `build_pong_now`, the `t2 >= t1` clamp, and `now_ns()` monotonicity.

## Pass 4 (input forwarding + stage tracking + adaptive bitrate + connection UI) — landed

Implemented in this pass:

- **Task 1 — real input action state forwarding.** `OpenXrSession` action set
  expanded with the full Touch Plus binding surface (trigger touch, thumbstick
  click + touch, button A/B touch, system click, thumbrest force). New
  `OpenXrSession::sync_actions()` and `read_action_state(hand, ActionStateBundle&)`
  read live state via `xrGetActionStateBoolean/Float/Vector2f`. `pose_forwarder`
  now syncs once per 1 kHz tick and uses the pure-logic `InputPacker` to fill
  `PlainTouchInputState` for both hands; `xrSyncActions` was removed from
  `poll_events` to avoid double-sync racing the forwarder thread.
- **Task 2 — STAGE reference space.** Both `XR_REFERENCE_SPACE_TYPE_LOCAL` and
  `XR_REFERENCE_SPACE_TYPE_STAGE` are created (stage falls back to local when
  guardian is unconfigured). `OpenXrSession::capture_local_origin_if_needed`
  computes `stagePose⁻¹ × hmdPose` once at session start using the new pose-
  inverse helper (covered by `test_local_origin_math`). The pose forwarder
  stores the offset as `local_origin_pose_` Quest-side; per-frame application
  is implicit in the existing locate path. **Hardware blocker:** verifying
  guardian-bounds correctness still requires a real Quest run (no host harness
  for STAGE).
- **Task 3 — connection UI.** Head-locked `XrCompositionLayerQuad` text
  overlay scaffolding via the new `connection_ui` module. State enum covers
  Discovering/Connecting/Negotiating/WaitingForFrames/Connected.
  `ConnectionUi::render` produces an RGBA8 buffer (kQuadW × kQuadH) that the
  compositor uploads as a quad swapchain image. **Asset workaround:** the
  shipping path expects `app/src/main/assets/font_atlas.png` +
  `font_atlas.json`; until those are vendored, `connection_ui.cpp` paints a
  procedural per-character speckle pattern so the quad has non-empty content.
  Real bitmap atlas is a pure asset task (no code changes), tracked here.
- **Task 4 — hand tracking forwarding.** `XR_EXT_hand_tracking` advertised in
  the enabled extension list. `HandEncoder` (`hand_encoder.{hpp,cpp}`) packs
  2 × 26 × 7 floats as binary16 + base64 with the stable `q-hand: ` prefix on
  the `ControlMessage.error` arm — workaround pending a wire-schema bump
  (see ADR-0008). The actual `xrLocateHandJointsEXT` call is **not yet wired
  into pose_forwarder**: that needs `XrHandTrackerEXT` creation against the
  KHR loader's runtime function pointers; deferred to follow-up because
  `XR_EXT_HAND_TRACKING_EXTENSION_NAME` only takes effect after the runtime
  reports the extension as supported. Round-trip encode/decode covered by
  `test_hand_encoder`.
- **Task 5 — `q-metrics` correctness.** `MetricsFormatter` (pure-logic)
  guarantees every line passes the daemon's parser regex
  `q-metrics: k=v(?:, k=v)*$`. Sanitizes NaN/Inf/negatives, caps
  `transport_loss_pct` at 100. `decode_avg_ms`, `dropped_frames`, and
  `transport_loss_pct` added; `DecoderPipeline` now tracks dropped frames in
  the existing drop-old replacement and exposes the average decode latency
  alongside p95. Emission stays at exactly 10 Hz (100 ms period) decoupled
  from render rate. 100-sample fuzz test covers well-formedness.
- **Task 6 — adaptive bitrate / keyframe request.** `LossTracker` tracks
  reassembly losses in a 1 s sliding window and emits `bitrate-req: kbps=N`
  at most once per second when sustained loss crosses the threshold (default
  5/sec); `keyframe-req: now` is emitted on a flagged decode failure with a
  250 ms rate limit. `ProtocolRouter::poll_adaptive_signals()` is called
  once per render iteration in `main.cpp`. Wiring `note_loss_frame()` into
  the actual reassembler-detected gaps and `note_decode_failure()` into the
  MediaCodec error callback is a small follow-up (the entry points exist
  but the producer side still needs threading through). Exhaustive coverage
  in `test_loss_tracker`.
- **CMake.** `add_subdirectory(audio)` appended conditionally on the EPSILON-
  owned `audio/CMakeLists.txt` existing; the build succeeds either way.

Host tests (all in `app/src/main/cpp/tests`, passing under the existing
`/tmp/quest-host-tests` setup):

- `test_input_packing` — Task 1, ActionStateBundle → PlainTouchInputState.
- `test_metrics_format` — Task 5, well-formedness on 100 fuzz samples.
- `test_loss_tracker` — Task 6, threshold + rate-limit + window pruning.
- `test_hand_encoder` — Task 4, 364-float round trip via base64+f16.
- `test_connection_ui` — Task 3, distinct non-empty pixels per state.
- `test_local_origin_math` — Task 2, pose_inverse ∘ pose ≈ identity.

## Cross-team coordination items

- The metrics-over-`error`-arm workaround needs a matching parser in the
  Mac-side daemon: when a `ControlMessage.error` text starts with
  `q-metrics: ` it should be lifted to the diagnostics view rather than
  shown as an error toast. Coordination item with the daemon agent;
  scope: not in `quest-app/`.
- A proper Quest→Mac metrics arm in `proto/fuvr.capnp` would replace
  this hack. That is a wire-schema bump (major version) and is
  deliberately out of scope for v1.

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
