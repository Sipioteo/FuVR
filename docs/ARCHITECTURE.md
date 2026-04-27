# FuVR — Architecture overview

This document is a working map of how the components in this repo fit together.
The authoritative design rationale lives in [`SPEC.md`](../SPEC.md); this file
is the engineering view: what each subdirectory actually owns, what crosses the
boundaries, and where the seams are.

## One-screen diagram

```
   ┌─────────────────────────── macOS host (arm64) ───────────────────────────┐
   │                                                                          │
   │   XR app  ──▶  runtime-macos  ──▶  encoder-macos  ──▶  transport (Rust)  │
   │  (Blender,   (libfuvr_openxr_      (HEVC/H.264          ▼  cdylib FFI    │
   │   Godot,      runtime.dylib)        Annex-B)        ┌───────────┐        │
   │   Unity)         ▲                                  │ usb (ADB) │        │
   │                  │ pose / input                     │ udp+FEC   │        │
   │                  └──────────────────────────────────┴─────┬─────┘        │
   │                                                          ▲ │            │
   │   mac-app (SwiftUI control surface) ── UDS JSON ─▶ daemon │ │            │
   │   virtual-display-helper (subprocess) ── CGVirtualDisplay │ │            │
   └──────────────────────────────────────────────────────────┼─┼────────────┘
                                                              │ │  USB-C / Wi-Fi 6
   ┌──────────────────────────────────────────────────────────┼─┼────────────┐
   │  Meta Quest (Android NDK)                                ▼ │            │
   │   quest-app/  TCP/UDP receiver ──▶ MediaCodec ──▶ AHardwareBuffer        │
   │                       │                                    │             │
   │                       └─▶ pose forwarder (1 kHz) ──────────┘             │
   │                                                                          │
   │            OpenXR system runtime (composition + ATW + reprojection)      │
   └──────────────────────────────────────────────────────────────────────────┘
```

## Component ownership

| Path | Owns | Wire/build boundary |
|---|---|---|
| `proto/fuvr.capnp` | The wire schema. Schema id is frozen at `@0xb1f5d4f7c2a830e5`. | `scripts/gen-proto.sh` produces C++ and Rust bindings into `proto/gen/`. |
| `runtime-macos/` | The macOS-side OpenXR 1.1 runtime, registered via `~/Library/Application Support/OpenXR/1/active_runtime.json`. Hosts the swapchain/frame loop, pose predictor, and a `FrameSink` interface that is fed to the encoder. | C++20 dylib + a registration CLI. |
| `encoder-macos/` | VideoToolbox encoder wrapper. Consumes `CVPixelBuffer` from the runtime, emits Annex-B fragments through a `FrameSink` callback. | Static C++ library + smoke test + `fuvr-encode-synthetic` M0 spike tool. |
| `transport/` | Rust workspace: USB (ADB-reverse loopback) and UDP (MTU fragmentation + Reed-Solomon FEC) datagram transports, multiplexed over channels (`Video`, `Audio`, `Pose`, `Input`, `Haptics`, `Control`). Used by the host via a C ABI in `transport-ffi`. | Rust cdylib + `cbindgen`-generated `fuvr_transport.h`. |
| `quest-app/` | Android NDK app that runs on the headset. OpenXR client; hardware HEVC/H.264 decoder via MediaCodec; zero-copy `AHardwareBuffer` → `GL_TEXTURE_EXTERNAL_OES` upload to OpenXR projection layers; 1 kHz pose forwarder. | APK built by Gradle. |
| `mac-app/` | SwiftUI control surface. Talks to a local daemon over a Unix domain socket using line-delimited JSON (v0 control plane). Includes `MockDaemon` for standalone development. | Swift Package; macOS 14+. |
| `virtual-display-helper/` | Subprocess that creates a `CGVirtualDisplay` via the **private** API surface, so TCC and WindowServer interactions are isolated from the daemon. Phase 2 — not on the M0/M1 critical path. | Tiny Obj-C++ exe + `posix_spawn` C control library. |

## Cross-component contracts

### Wire format

The Cap'n Proto schema in `proto/fuvr.capnp` is the **single source of truth**.
Every consumer (the C++ host, the Rust transport crate, the Quest NDK client)
generates its own bindings from this file. The schema id is frozen and CI
enforces this via `.github/workflows/proto-check.yml`.

Video on the wire is `[capnp-encoded VideoFragmentHeader][raw codec bytes]`.
The transport layer sees a single opaque blob per frame and may fragment it
across multiple datagrams (UDP) or length-delimited frames (USB) using
Reed-Solomon (10, 4) FEC by default.

### FrameSink

Both the OpenXR runtime and the encoder expose a `FrameSink`-style interface:

- `runtime-macos`: `fuvr::FrameSink` receives the swapchain image at
  `xrEndFrame`. Currently a `NullFrameSink` drops frames; the encoder will
  later install itself here.
- `encoder-macos`: `fuvr::FrameSink` receives Annex-B fragments. The transport
  FFI installs a callback that publishes onto the video channel.

This keeps the runtime and encoder independently testable and avoids any
direct dependency between them — the daemon (when it lands) wires them up.

### Control plane

The mac-app talks to the daemon over a Unix domain socket using a
JSON-over-newline envelope `{ "v": 1, "type": "...", "payload": ... }`. The
arms mirror the `ControlMessage` union in `proto/fuvr.capnp` and add two
control-surface-only types (`metrics`, `log`). The data plane (video, audio,
pose, input, haptics) never goes through this socket — it stays in
`transport/`.

### Pose flow

```
Quest pose_forwarder (1 kHz) ──▶ transport (pose channel) ──▶ runtime-macos
                                                                     │
                                                                     ▼
                                                          PosePredictor (ring N=32)
                                                                     │
                                                              xrLocateViews
                                                                     │
                                                                     ▼
                                                         predicted pose to the app
```

The Quest also embeds the pose used by the app for each rendered frame in the
`VideoFragmentHeader.renderedLeft/Right`. The Quest's system compositor uses
that pose for ATW; without it, motion sickness is guaranteed (SPEC §3.2.3).

## Build matrix

| Component | Toolchain | CI workflow |
|---|---|---|
| `runtime-macos`, `encoder-macos`, `virtual-display-helper`, top-level CMake | CMake 3.27+, Ninja, AppleClang, Cap'n Proto 1.x | `macos-cmake.yml` |
| `transport` | Rust stable, capnp 1.x | `rust.yml` |
| `quest-app` | JDK 17, AGP 8.5+, Gradle 8.7+, NDK r26+ | `android.yml` |
| `mac-app` | Swift 5.10, macOS 14+ | `swift.yml` |
| `proto` | capnp | `proto-check.yml` (schema id guard) |

## Why this seam, not that one

A few decisions worth memorialising; ADRs in `docs/adr/` go deeper.

- **OpenXR runtime is in-process, not a daemon.** The standard OpenXR loader
  contract is `dlopen` of the runtime dylib in the app's address space. The
  daemon (when needed for encoder, transport, virtual display lifecycle) is a
  separate process the runtime talks to. See ADR-0002.
- **Cap'n Proto for everything except the macOS control plane.** Zero-copy
  reads on the Quest matter at 1 kHz pose; for a 10 Hz local control socket
  we use JSON to keep iteration fast. ADR-0003.
- **Private `CGVirtualDisplay` API in a subprocess.** TCC isolation and the
  Lumen-style pattern. ADR-0004.
- **Reed-Solomon FEC, no ARQ.** Latency budget < 15 ms; retransmits don't
  arrive in time. ADR-0005.
- **ADB reverse over USB, not raw bulk.** Spec §3.1.4: zero driver work,
  Developer Mode is enough; AOA is escalation, not v1. ADR-0006.
