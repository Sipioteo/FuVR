# FuVR — Implementation status

Snapshot as of 2026-04-27, after coordinator pass 3.

## Where we are on the SPEC roadmap

End of pass 3: **late M0 / M1 in flight**. The cross-process IOSurface
handoff (ADR-0007) is implemented via XPC; runtime↔daemon clock sync is
real; per-frame `EncodeStats` flow back from the encoder; the OpenXR event
queue, reference spaces, and `xrLocateSpace` are real (head-pose path); the
Quest decoder pipeline is wired all the way through `AImageReader` →
`AHardwareBuffer` → `EGLImage` → `GL_TEXTURE_EXTERNAL_OES`. **End-to-end
hardware streaming has not been exercised yet.**

## Top-line verification

| Check | Result |
|---|---|
| `cmake --build build` | clean |
| `ctest --test-dir build` | **24/24 passed**, 1 disabled (vdisplay needs GUI) |
| Quest host tests (`test_fragment_reassembly` + `test_clock_sync`) | both passed |
| `cargo test --workspace` (transport) | passed (FEC, UDP loopback) |
| `swift test --package-path mac-app` | **10/10 passed** |
| `scripts/check-licenses.sh` | clean (131 files) |

## Per-component state (post pass 3)

### `proto/`

- `fuvr.capnp` — frozen at `@0xb1f5d4f7c2a830e5`.
- `fuvrd.capnp` — frozen at `@0xc8a4f30f6df21a7b`. Comments updated to
  reflect the XPC handoff path per the revised ADR-0007.

### `runtime-macos/`

Pass 3 additions:
- `IOSurfaceXpcClient` (Apple XPC client for the surface mach service) plus
  the `FUVR_INPROCESS_HANDOFF=1` test bypass via `InProcessSurfaceRegistry`.
- Real `EventQueue` per-instance, `xrPollEvent` drains it.
- `xrCreateReferenceSpace` (`VIEW`/`LOCAL`/`STAGE`), `xrCreateActionSpace`,
  `xrDestroySpace`, `xrLocateSpace`. Action-space locate honestly returns
  invalid bits because the daemon's `PoseSnapshot` does not yet carry
  controller pose (pass 4).
- Daemon-client now decodes `Envelope.encodeStats` and pushes into a
  rolling `EncoderStats` window; `fuvr-runtime-metrics` CLI prints
  encode fps / p95 / bitrate from the back-door diag header.
- Lifecycle session-state events emitted from the daemon connection state.

### `daemon/`

Pass 3 additions:
- `IOSurfaceXpcService` listener registered on `com.fuvr.daemon.surface`.
- Per-frame `EncodeStats` envelope fan-out, piggy-backed on existing
  `streamMetrics` subscription (no schema change).
- NTP-style `ClockSync` ping/pong state machine: 1 Hz ping thread, median
  of last 16 samples for `clockOffsetNs` and `oneWayDelayNs`. Populated
  in `StartSessionResponse` with a 200 ms wait window at session start.
- New tests: `test_clock_sync`, `test_encode_stats_forward`,
  `test_iosurface_xpc`.

### `quest-app/`

Pass 3 additions:
- Real MediaCodec output pipeline: configured against an `AImageReader`'s
  native window so decoded frames land directly as `AHardwareBuffer`s.
- `AHardwareBuffer` ladder (acquire / drop-old / EGLImage bind / release)
  fully accounted for; compositor consumes a real external texture.
- Clock-sync responder (`ClockSyncResponder`) replies to Mac pings with
  `t1`/`t2`.
- Quest-side health metrics (`fps`, `decode_p95_ms`) are emitted on the
  control channel piggy-backed on the `error` arm with the prefix
  `q-metrics:` (workaround documented in `quest-app/TODO.md`; a clean
  schema arm would require a major version bump).

### `mac-app/`

Unchanged in pass 3.

### `transport/`, `encoder-macos/`, `virtual-display-helper/`

Unchanged in pass 3.

### CI / infra

- `proto-check.yml` already pins both schema ids.
- Top-level CMake unchanged in pass 3 (cross-subdir target linkage handled
  via existing `add_subdirectory` order).

## What works end-to-end today (in-process)

1. `FUVR_INPROCESS_HANDOFF=1`-mode integration: the runtime's swapchain
   IOSurfaces are visible to the daemon's encoder host without XPC. Used by
   unit tests, also valid for one-machine smoke runs.
2. With XPC mode + the launchd plist installed
   (`scripts/install-launchd.sh`), runtime↔daemon handoff works
   cross-process. **Not exercised in CI**; the `FUVR_E2E_XPC=1` gate runs
   it in a developer environment only.

The host pipeline (Mac → Quest) still cannot be exercised end-to-end
because no hardware run has happened. The four M0 spike questions remain
the gate to M1.

## Critical M0 spikes (SPEC §5.M0)

| # | Question | Tool | State |
|---|----------|------|-------|
| 1 | ADB reverse over USB ≥100 Mbps with <15 ms RTT? | `transport-cli loopback-bench` | tool ready, hardware run pending |
| 2 | VideoToolbox HEVC `RealTime=true` <15 ms encode on M2/M3? | `fuvr-encode-synthetic` | tool ready, hardware run pending |
| 3 | Quest receive UDP + MediaCodec + projection layer @ 90 Hz? | `quest-app` debug build | decoder pipeline now real (pass 3); ready for device-side smoke |
| 4 | `CGVirtualDisplay` works on macOS 14/15/16? | `fuvr-vdisplay-helper --width ... --height ... --refresh ...` | tool ready, multi-version run pending |

## Next coordinator pass (pass 4)

1. **Controller pose forwarding**: extend `PoseSnapshot` (this is a daemon
   schema change — `fuvrd.capnp` only, the wire schema stays frozen) with
   left/right controller poses; the daemon already receives them on the
   wire; runtime action spaces start returning valid bits.
2. **Quest q-metrics → daemon metrics ingestion**: parse the `error` arm
   `q-metrics:` prefix in the daemon, emit the values as part of the
   `Metrics` envelope so the mac-app's diagnostics view shows decoder fps
   and decode p95 ms.
3. **Real input read-back**: connect `xrGetActionStateBoolean/Float/Vector2f`
   to the daemon's most recent `UpstreamFrame.inputs`.
4. **Audio path**: encoder side is `Opus` plumbing TBD; transport `Audio`
   channel is reserved; Quest side needs the `OpenSL ES`/`AAudio` output.
5. **First end-to-end hardware spike**: actually run M0 question 1 on a
   real Quest 3 + M2 Mac and record numbers.

## ADR index

- 0001: record architecture decisions
- 0002: OpenXR runtime in-process; daemon owns auxiliary work
- 0003: Cap'n Proto for the wire, JSON for the local control plane
- 0004: CGVirtualDisplay subprocess
- 0005: Reed-Solomon FEC, no ARQ
- 0006: ADB reverse over USB
- 0007: IOSurface handoff via XPC mach service (revised in pass 3)
