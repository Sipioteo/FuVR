# FuVR — Implementation status

Snapshot as of 2026-04-27, after coordinator pass 2. Updated at the end of
each multi-component pass.

## Where we are on the SPEC roadmap

End of pass 2: **late M0 / very early M1**. The skeleton is wired
end-to-end on the host: top-level CMake builds every component, daemon and
runtime are bound by Cap'n Proto RPC over a Unix domain socket, the Quest
app speaks Cap'n Proto on every wire channel, and per-eye swapchains plus a
side-by-side blit shader are in place. **Nothing has run on real hardware
yet.**

## Top-line verification

| Check | Result |
|---|---|
| `cmake -S . -B build` (top level) | clean configure |
| `cmake --build build` | clean build, all targets |
| `ctest --test-dir build` | **11/11 passed**, 1 disabled (vdisplay needs GUI) |
| `cargo test --workspace` (transport) | **all suites passed** (FEC, UDP loopback) |
| `swift test --package-path mac-app` | **10/10 passed** |
| `bash scripts/check-licenses.sh` | clean (105 files) |

## Per-component state (post pass 2)

### `proto/` — schemas

- `fuvr.capnp` (the Mac↔Quest wire) — schema id `@0xb1f5d4f7c2a830e5`. Frozen.
- `fuvrd.capnp` (the runtime↔daemon RPC) — schema id `@0xc8a4f30f6df21a7b`.
  Frozen. Covers session lifecycle, frame submission with IOSurface handoff,
  pose / metrics / log streaming, ping/pong.
- Both schemas guarded by `proto-check.yml`.

### `runtime-macos/` — OpenXR runtime + daemon client

- Pass 1: lifecycle dispatch, pose predictor, registration CLI.
- Pass 2: `DaemonClient` (UDS Cap'n Proto, lazy connect with backoff), pose
  stream subscription feeding `PosePredictor`, IOSurface-backed swapchains
  (3 images, BGRA8, Metal `MTLTexture` + companion `IOSurfaceRef`), the
  `XR_FUVR_metal_enable` extension surface (`XrSwapchainImageMetalFUVR`),
  `DaemonFrameSink` that mints a mach send-right and submits per-frame to
  the daemon.
- 7/7 tests passing.

### `encoder-macos/` — VideoToolbox wrapper

- Unchanged in pass 2. Still: HEVC + H.264 low-latency,
  AVCC→Annex-B framing, CSD emission, IDR forcing, `fuvr-encode-synthetic`
  M0 spike tool, smoke test passing.

### `transport/` — Rust workspace

- Unchanged in pass 2. 5 crates, FEC round-trip + UDP loopback tests
  passing.

### `quest-app/` — Android NDK client

- Pass 1: skeleton OpenXR + MediaCodec + transport + pose forwarder.
- Pass 2: Cap'n Proto C++ runtime as `ExternalProject_Add` (capnproto-c++
  1.0.2) for arm64-v8a; real `UpstreamFrame` packed serialisation at 1 kHz;
  `VideoFragmentHeader` parse + fragment reassembly indexed by
  `frameId`/`fragmentIndex`; `ControlMessage` handling (helloFromMac,
  clockSync, haptic); per-eye swapchain creation honoring
  `recommendedImageRect{Width,Height}`; GLES3 side-by-side blit shader
  splitting `4128×2208` into two `2064×2208` per-eye targets with full GL
  state save/restore; host-side `test_fragment_reassembly` covering
  in-order, out-of-order, duplicate, interleaved, and eviction cases.

### `daemon/` (new in pass 2)

- 16 files: UDS Cap'n Proto server, per-session encoder + transport-ffi
  bridge, IOSurface→CVPixelBuffer mach-port lookup, pose router fanning
  inbound `UpstreamFrame` messages out to subscribers, virtual-display
  spawn, 10 Hz metrics ticker, JSON↔Cap'n Proto bridge for the mac-app
  control plane.
- Configure-time fallback when the Rust transport dylib isn't built:
  `FUVR_DAEMON_NO_TRANSPORT` stubs the FFI calls so the daemon still
  builds.
- 5/5 tests passing (rolling-window metrics, RPC envelope round-trip, pose
  router fan-out).

### `mac-app/` — SwiftUI control surface

- Unchanged in pass 2. `FuVRControl` library + `FuVR` executable +
  `MockDaemon`. 10/10 tests passing.

### `virtual-display-helper/` — phase-2 subprocess

- Unchanged in pass 2. Builds clean; e2e test gated on a GUI session.

### CI / infra

- 6 workflows from pass 1.
- `proto-check.yml` extended in pass 2 to also pin the daemon schema id
  `@0xc8a4f30f6df21a7b`.
- Top-level `enable_testing()` added so `ctest --test-dir build` aggregates
  every component's CTest registration.

## What works end-to-end today

In-process integration test:

1. Start `fuvrd` from `build/daemon/fuvrd`.
2. Set `OPENXR_ACTIVE_RUNTIME` to the runtime dylib (or use the
   `fuvr-register` CLI).
3. Run any OpenXR-capable test app linked against `fuvr_openxr_runtime`.
4. The runtime connects to the daemon, starts a session, hands off
   IOSurface-backed swapchain images per frame.

This works today **only when runtime and daemon share a mach task** (i.e.
the in-process unit tests). True cross-process IOSurface handoff needs the
mach-service side channel from ADR-0007, which is the first item of pass 3.

## Critical M0 spikes (SPEC §5.M0)

| # | Question | Tool | State |
|---|----------|------|-------|
| 1 | ADB reverse over USB ≥100 Mbps with <15 ms RTT? | `transport-cli loopback-bench` | tool ready, hardware run pending |
| 2 | VideoToolbox HEVC `RealTime=true` <15 ms encode on M2/M3? | `fuvr-encode-synthetic` | tool ready, hardware run pending |
| 3 | Quest receive UDP + MediaCodec + projection layer @ 90 Hz? | `quest-app` debug build | transport + per-eye swapchains landed; needs decoder Surface↔AImageReader plumbing for visual output |
| 4 | `CGVirtualDisplay` works on macOS 14/15/16? | `fuvr-vdisplay-helper --width ... --height ... --refresh ...` | tool ready, multi-version run pending |

## Next coordinator pass (pass 3)

1. **ADR-0007 implementation**: mach service `com.fuvr.daemon.surface` on
   the daemon side; `bootstrap_look_up` + per-frame `mach_msg_send` on the
   runtime side. Symmetric work, must land together.
2. **MediaCodec output → AHardwareBuffer plumbing on the Quest**, so the
   blit shader sees real frames instead of `current_texture_=0`.
3. **EncodeStats response path**: encoder → daemon → runtime, fed into a
   metrics view in the mac-app.
4. **Real clock sync**: implement the `ClockSync` ping/pong on both sides
   so `StartSessionResponse.clockOffsetNs` is actually populated.
5. **`xrPollEvent`** emitting `XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED` from
   daemon connection state transitions.
6. **`xrLocateSpace` + reference spaces**: currently stubbed; needed before
   any real OpenXR app can run.

## ADR index

- 0001: record architecture decisions
- 0002: OpenXR runtime in-process; daemon owns auxiliary work
- 0003: Cap'n Proto for the wire, JSON for the local control plane
- 0004: CGVirtualDisplay subprocess
- 0005: Reed-Solomon FEC, no ARQ
- 0006: ADB reverse over USB
- 0007: IOSurface handoff via parallel mach service (NEW)
