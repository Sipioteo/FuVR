# FuVR — Implementation status

Snapshot as of 2026-04-27, after coordinator pass 4.

## Where we are on the SPEC roadmap

End of pass 4: **all pre-hardware code work is complete**. M0 spike tools,
M1 pipeline plumbing, M2 controller pose / input read-back / clock sync,
M3 OpenXR runtime maturity (action state, reference spaces, multi-layer
endFrame, hand/eye stub extensions, reconnect resilience), M4 audio path
and DX/release tooling all landed. **The first hardware run is the next
gate** and is scripted in `docs/HARDWARE_RUNBOOK.md`.

## Top-line verification (pass 4 close)

| Check | Result |
|---|---|
| `cmake --build build` (top level) | clean |
| `ctest --test-dir build` | **57/57 passed**, 2 disabled (vdisplay E2E + SckCapture E2E both need GUI/HW) |
| `cargo test --workspace` (transport) | **18 passed** across 14 suites |
| `swift test --package-path mac-app` | **29/29 passed** |
| `scripts/check-licenses.sh` | clean (211 files) |

## What pass 4 added

### Schema (additive — schema id `@0xc8a4f30f6df21a7b` unchanged)
- `PoseSnapshot` extended with controller pose + velocity (28 new fields per controller).
- New `InputSnapshot` + `ControllerInput` structs.
- `Envelope.body` union: `streamInputs`, `inputSnapshot`, `streamEncodeStats` arms.

### `runtime-macos/` (ALPHA — 37/37 tests, up from 24)
- Real `xrGetActionState{Boolean,Float,Vector2f,Pose}` reading from a thread-safe `ActionStateCache` populated by `streamInputs`.
- Real path registry (`xrStringToPath`/`xrPathToString` with seeded interaction-profile atoms).
- Real `xrEnumerateSwapchainImages`.
- Multi-layer `xrEndFrame` walk with one surface token per layer.
- `XR_EXT_hand_tracking` and `XR_EXT_eye_gaze_interaction` advertised, locate stubs return invalid bits with a clear path to enabling them post-1.0.
- Session loss / reconnect resilience with backoff capped at 5 s; `fuvr-runtime-metrics` reports daemon liveness.

### `daemon/` (BETA — 22/22 tests)
- Controller pose forwarding from wire `UpstreamFrame.controllers` → `PoseSnapshot.{left,right}Controller*`.
- `InputSnapshot` fan-out via dedicated `streamInputs` subscription.
- `q-metrics:` parser feeding `Metrics.decoderFps` / `decoderDecodeMsP95`.
- Reconnect FSM (100 ms init, 5 s cap exponential backoff).
- Structured `Logger` with ring buffer + `streamLogs` replay.
- Dedicated `streamEncodeStats` arm (legacy `streamMetrics` piggy-back kept with deprecation log).
- Audio session start/stop wired into `Session::Session`/`~Session` gated on `cfg.audioEnabled`.

### `transport/` (GAMMA — 18 tests, clippy clean)
- New `transport-mdns` crate (Bonjour macOS + `mdns-sd` Linux fallback) implementing ADR-0009 service `_fuvr._udp.local.`.
- `transport-udp/jitter.rs` — depth-bounded reorder buffer with deadline-driven release.
- `transport-core/control.rs` — bitrate-req / keyframe-req piggy-back on `error` arm.
- `transport-usb/aoa.rs` — escalation stub.
- `transport-ffi` — `fuvr_transport_stats` for daemon Metrics.

### `quest-app/` (DELTA — 7 host tests)
- Real input action state forwarding (touch_plus_controller surface) at 1 kHz.
- STAGE reference space math (`stagePose⁻¹ × hmdPose`).
- Connection UI scaffold with state machine (font atlas pending; renderer ready).
- Hand-tracking forwarding via `q-hand:` base64-f16 prefix on `error` arm.
- q-metrics correctness pass (NaN/range clamping, fixed 10 Hz emit).
- Adaptive bitrate / keyframe request emission with rate-limiting.
- `Channel::Audio` dispatch wiring `install_audio_handler()` (Android-guarded).

### Audio path (EPSILON — cross-cutting, all green)
- `encoder-macos/audio/`: ScreenCaptureKit `Capture`, `OpusEncoderWrap` (low-delay 20 ms 128 kbps stereo).
- `daemon/audio/`: `AudioSession` builds `proto::AudioPacket`, ships on transport `Audio` channel; `startAudioFor`/`stopAudioFor` integrated into `Session` lifecycle.
- `quest-app/.../audio/`: `OpusAudioReceiver` + `AAudioOutput` (low-latency PCM_I16 ring); host-side smoke test.

### `mac-app/` (ZETA — 29/29 tests, up from 10)
- Hand-rolled Swift Cap'n Proto module `FuVRCapnp` (no external deps) covering the runtime↔mac-app envelope subset.
- `ControlClient` migrated from JSON to packed Cap'n Proto.
- Real Diagnostics view with Swift Charts (encoder fps / encode p95 / RTT / decoder fps / decode p95 / loss).
- Real Log view with level chips, search, pause toggle, sticky-bottom auto-scroll.
- Onboarding wizard (3 steps: install daemon, pair Quest, test session).
- Settings v2 migration with idempotence test.

### `virtual-display-helper/` + `daemon/vdisplay/` (THETA — 16 tests)
- `SckCapture` (ScreenCaptureKit against `CGVirtualDisplay`), gated on `FUVR_VDISPLAY_E2E=1`.
- `VirtualDisplaySession` orchestrator (helper spawn + capture + encoder feed) with full unit-test coverage via fakes.
- Helper additions: `--list`, `--mode`, `--watchdog` flags + CLI parser tests.
- `MACOS_QUIRKS.md` documenting macOS 14/15/16 quirks with reproduction recipes.

### DX / release (ETA)
- `scripts/fuvrctl` CLI: `install`, `uninstall`, `status`, `logs`, `quest install`, `quest reverse`, `bench`.
- `scripts/install-quest.sh`, `scripts/install-launchd.sh`, `scripts/uninstall-launchd.sh`.
- `Formula/fuvr.rb` Brew formula stub.
- `quest-app/sidequest.json` SideQuest manifest.
- `docs/RELEASE.md`, `docs/TROUBLESHOOTING.md`, `docs/DEVELOPMENT.md`.
- `CHANGELOG.md` seeded.
- `.github/SECURITY.md`, `FUNDING.yml`, `dependabot.yml`, `hardware-test-report.yml` issue template.

## Cross-team integrations completed at finalization
- Audio session start/stop in `daemon/src/session.cpp` (gated on `cfg.audioEnabled`).
- `Channel::Audio` dispatch in `quest-app/.../protocol_router.cpp` calling `audio::install_audio_handler()` (Android-guarded).
- `daemon/CMakeLists.txt` PUBLIC link `fuvr_daemon_audio` so `Session` resolves the audio symbols.

## Deferred (Pass 5 / pass 6)
- vdisplay full integration in `Session` (THETA's 3-line patch in `daemon/vdisplay/INTEGRATION.md`) — requires hardware verification cycle.
- Daemon-side `q-hand:` base64-f16 parser + `xrLocateHandJointsEXT` data path (wire schema major bump).
- Connection UI font atlas asset (DELTA's renderer is ready; just needs the PNG/JSON pair).
- `xrLocateHandJointsEXT` plumbed through OpenXR loader function-pointer lookup on the Quest side.
- `MockDaemonRoundtripTests` over a real `NWListener` socket (currently bypasses the socket because `NWListener` doesn't bind under SPM test harness).

## What works end-to-end today (pre-hardware)

In-process, with `FUVR_INPROCESS_HANDOFF=1`:
1. Runtime + daemon link in the test binaries; full lifecycle from `xrCreateSession` to multi-layer `xrEndFrame` exercised.
2. Cross-process: launchd plist installable via `scripts/install-launchd.sh`; XPC service `com.fuvr.daemon.surface` carries IOSurface mach send-rights.
3. `fuvrd` connects to `transport/` Rust crate via FFI when `libfuvr_transport.dylib` is present (build it with `cargo build --release --manifest-path transport/Cargo.toml`).
4. mac-app diagnostics view shows live (mock-daemon-driven) metrics.

What does NOT work without hardware:
- Actually streaming a frame to a real Quest. **First hardware run is in `docs/HARDWARE_RUNBOOK.md`.**
- vdisplay E2E (needs GUI session + Screen Recording TCC).
- ScreenCaptureKit audio capture E2E (TCC microphone consent + GUI session).

## Critical M0 spikes — runbook

See `docs/HARDWARE_RUNBOOK.md` for the exact command sequence with the Quest connected. Status of each spike's tooling:

| # | Question | Tool | Tooling state |
|---|----------|------|---|
| 1 | ADB reverse over USB ≥100 Mbps with <15 ms RTT? | `transport-cli loopback-bench` | ready |
| 2 | VideoToolbox HEVC `RealTime=true` <15 ms encode on M2/M3? | `fuvr-encode-synthetic` | ready |
| 3 | Quest receive UDP + MediaCodec + projection layer @ 90 Hz? | quest-app debug build + daemon + Mac app | ready |
| 4 | `CGVirtualDisplay` works on macOS 14/15/16? | `fuvr-vdisplay-helper` | ready |

## ADR index

- 0001: record architecture decisions
- 0002: OpenXR runtime in-process; daemon owns auxiliary work
- 0003: Cap'n Proto for the wire, JSON for the local control plane (deprecated by mac-app pass 4 migration to Cap'n Proto)
- 0004: CGVirtualDisplay subprocess
- 0005: Reed-Solomon FEC, no ARQ
- 0006: ADB reverse over USB
- 0007: IOSurface handoff via XPC mach service
- 0008: OpenXR extension scope for v1
- 0009: mDNS / Bonjour discovery for Wi-Fi mode
