# FuVR — Open-Source PCVR Streaming for macOS

Stream VR content from an Apple Silicon Mac to a Meta Quest headset over USB-C
or Wi-Fi, with bidirectional pose, controller and (optionally) hand-tracking
data flowing back. Designed from the ground up for macOS — no SteamVR, no
Quest Link, no reverse engineering of Meta's proprietary protocol.

> **Status:** Pre-alpha. Active scaffolding. Nothing works end-to-end yet.
> See [`SPEC.md`](SPEC.md) for the full architectural specification.

## Repository layout

| Path | Component | Language |
|---|---|---|
| `proto/` | Cap'n Proto schemas, the source of truth for the wire format | Cap'n Proto |
| `runtime-macos/` | OpenXR 1.1 runtime for macOS (registers as `active_runtime.json`) | C++20 |
| `encoder-macos/` | VideoToolbox HEVC/H.264 low-latency encoder wrapper | C++ / Obj-C++ |
| `transport/` | Reusable transport layer (USB/ADB tunnel + UDP + FEC) | Rust |
| `virtual-display-helper/` | Subprocess helper for `CGVirtualDisplay` (phase 2) | Obj-C++ |
| `mac-app/` | SwiftUI control surface, settings, status, log viewer | Swift |
| `quest-app/` | Android NDK client: receiver, decoder, OpenXR compositor | C++ NDK + Kotlin |
| `docs/` | Architecture notes, ADRs, build instructions | Markdown |
| `scripts/` | Build / dev / packaging scripts | Bash |

## Build prerequisites

- macOS 14+ on Apple Silicon (M1 or newer; M2 Pro recommended)
- Xcode 15+ command line tools
- CMake 3.27+
- Cap'n Proto compiler (`brew install capnp`)
- Rust stable (`rustup default stable`)
- Android NDK r26+ and Android SDK with API 33+ (for `quest-app`)
- Cap'n Proto Java/Kotlin runtime is pulled by Gradle for the Quest side

## Building (current scaffolding)

```bash
# generate protocol code for all targets
./scripts/gen-proto.sh

# build the macOS-side components
cmake -S . -B build -G Ninja
cmake --build build

# build the Rust transport crate
cargo build --manifest-path transport/Cargo.toml

# build the Quest app
(cd quest-app && ./gradlew assembleDebug)
```

## License

Apache License 2.0 — see [`LICENSE`](LICENSE). Contributions are accepted under
the Apache ICLA. The patent grant is intentional and non-negotiable.

## Status & roadmap

See [`SPEC.md`](SPEC.md) §5 for the milestone plan. The current state of the
tree corresponds to the very beginning of **M0 — Spike Plan**: scaffolding,
build wiring, and protocol schema only. Nothing in this tree streams a frame
yet, and most files are deliberately minimal stubs that compile but do not
implement runtime behavior.
