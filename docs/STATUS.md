# FuVR — Implementation status

Snapshot as of 2026-04-27. Updated by the coordinator at the end of each
multi-component scaffolding pass.

## Where we are on the SPEC roadmap

We are at the very start of **M0 — Spike Plan** (SPEC §5). The repo now has a
complete, buildable, tested skeleton across all components, with stubs marking
the work that the M0 spikes will validate.

## Per-component state

### `proto/` — wire schema

- `fuvr.capnp` complete for v1: math types, upstream (HMD/controllers/inputs),
  video fragments, audio packets, haptics, control messages.
- Schema id frozen: `@0xb1f5d4f7c2a830e5`. CI guards it.
- Code generation script: `scripts/gen-proto.sh`.

### `runtime-macos/` — OpenXR runtime

- 16 files. Configures and builds clean.
- Real: loader negotiation, dispatch table covering ~50 entries, instance/
  session/swapchain/action lifecycle with handle registries, frame loop,
  pose predictor (5 unit tests passing), `fuvr-register` CLI.
- Stubs returning `XR_ERROR_FUNCTION_UNSUPPORTED`: reference/action spaces,
  path-to-string, swapchain image enumeration, action pose state. Listed in
  `runtime-macos/TODO.md`.
- Vulkan platform define removed until MoltenVK lands; Metal vendor extension
  reserved.

### `encoder-macos/` — VideoToolbox wrapper

- Builds clean; static library `libfuvr_encoder.a`.
- Real: VTCompressionSession with HEVC + H.264 paths (low-latency mode for
  H.264 per WWDC21), AVCC→Annex-B conversion, CSD extraction & emission,
  forced IDR.
- M0 spike tool: `fuvr-encode-synthetic` (4128×2208 @ 90 Hz HEVC).
- Smoke test wired (GoogleTest via FetchContent).
- Each frame currently emits as a single `endOfFrame` fragment; transport-side
  fragmentation handles MTU.

### `transport/` — Rust workspace

- 5 crates: `transport-core`, `transport-usb`, `transport-udp`, `transport-cli`,
  `transport-ffi`.
- Builds clean; tests pass: 10k random frames with 5% loss round-tripping
  through Reed-Solomon FEC, plus in-process UDP loopback.
- CLI: `fuvr-transport` with `loopback-bench`, `dump`, `clock-sync`. This is
  the M0 spike tool for SPEC §5.M0 question 1 (ADB reverse throughput/latency).
- C ABI exported via `cbindgen` to `transport-ffi/include/fuvr_transport.h`.

### `quest-app/` — Android NDK client

- 24 files. Gradle project + NDK CMake.
- Real: OpenXR loader bootstrap, action set (touch_plus_controller),
  instance/session/spaces, frame loop with projection + quad-placeholder paths,
  MediaCodec async skeleton (HEVC default, H.264 fallback), TCP transport
  client matching Rust framing, 1 kHz pose forwarder, EGL/GLES3 + zero-copy
  AHardwareBuffer → external texture upload.
- Stubs in `quest-app/TODO.md`: Cap'n Proto wire serialisation, per-eye
  swapchain enumeration, decoder Surface↔AImageReader plumbing, UDP transport
  mode, action state read-back.
- Not exercised by Gradle here (no Android SDK available in coordinator
  sandbox); CI workflow `android.yml` will exercise on real runners.

### `mac-app/` — SwiftUI control surface

- Swift Package; `swift build` clean, `swift test` 10/10 passing.
- Targets: executable `FuVR` + library `FuVRControl` + tests.
- Views: Session, EncoderSettings, TransportSettings, Diagnostics, Log, About.
- Control plane: line-delimited JSON over Unix domain socket (`Network.framework`).
- `MockDaemon` allows standalone development.
- Liquid Glass aesthetic via `regularMaterial`/`thinMaterial`; `glassEffect()`
  guarded for macOS 26+.

### `virtual-display-helper/` — phase-2 subprocess

- Builds clean in isolation.
- Real: argv parsing, descriptor + settings + applySettings, posix_spawn'd
  control library with stdin pipe lifetime, M4/M5 6720-pixel-pipe-0 split,
  reverse-engineered `CGVirtualDisplay` Obj-C interface declarations vendored
  with per-symbol risk notes.
- E2E test marked `DISABLED_` (requires logged-in GUI session).

### CI / infra

- 6 GitHub Actions workflows: macOS CMake matrix, Rust (Ubuntu),
  Android, Swift, proto schema-id guard, license check.
- All actions pinned to major version, all workflows have minimum permissions
  and concurrency cancellation on non-default refs.
- `.clang-format`, `.editorconfig`, `CODEOWNERS`, PR/issue templates.

## What works end-to-end today

Nothing streams a frame yet. By component:

- The Rust transport workspace can be exercised with `cargo run -p transport-cli --bin fuvr-transport -- loopback-bench` to measure local throughput and FEC behaviour.
- The mac-app can be run standalone with the mock daemon: `swift run --package-path mac-app FuVR`.
- The runtime can be registered with `fuvr-register` and will be picked up by an OpenXR loader, but it does not yet expose any way for the encoder to receive its frames.

## Critical M0 spikes (SPEC §5.M0)

| # | Question | Tool | State |
|---|----------|------|-------|
| 1 | ADB reverse over USB ≥100 Mbps with <15 ms RTT? | `transport-cli loopback-bench` | tool ready, hardware run pending |
| 2 | VideoToolbox HEVC `RealTime=true` <15 ms encode on M2/M3? | `fuvr-encode-synthetic` | tool ready, hardware run pending |
| 3 | Quest receive UDP + MediaCodec + projection layer @ 90 Hz? | `quest-app` debug build | needs Cap'n Proto + per-eye swapchain wiring |
| 4 | `CGVirtualDisplay` works on macOS 14/15/16? | `fuvr-vdisplay-helper --width ... --height ... --refresh ...` | tool ready, multi-version run pending |

## Next coordinator pass

1. Daemon (`fuvrd/`) — owns the encoder + transport + (later) virtual display.
2. Runtime ↔ daemon RPC over UDS (Cap'n Proto, separate schema).
3. Wire the runtime's `FrameSink` to encoder → transport.
4. Quest: serialize `UpstreamFrame` over Cap'n Proto on the pose channel;
   parse `VideoFragmentHeader` on the video channel.
5. Per-eye swapchain creation in the quest-app compositor.
