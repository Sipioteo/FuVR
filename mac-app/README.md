# FuVR — macOS Control Surface

User-facing SwiftUI app that drives the FuVR PCVR streaming daemon. This package
contains only the control surface; the runtime, encoder, and transport live in
sibling directories.

## Layout

- `Sources/FuVRApp/` — SwiftUI app target (`@main`)
- `Sources/FuVRControl/` — pure-Swift library: control protocol, metrics buffer,
  Network.framework UDS client, in-process mock daemon
- `Tests/FuVRControlTests/` — XCTest coverage for protocol round-trip and rolling stats

## Build & run

### Swift Package Manager

```bash
swift build  --package-path mac-app
swift run   --package-path mac-app FuVR
swift test  --package-path mac-app
```

The first `swift run` launches the app from the SPM-built bundle. For full
icon/Info.plist treatment, open `Package.swift` in Xcode.

### Xcode

```bash
open mac-app/Package.swift
```

Pick the `FuVR` scheme and hit run. The app target uses macOS 14 as its
deployment minimum.

## Mock daemon

The app ships with an in-process mock daemon (`MockDaemon` in
`FuVRControl`). When the **Run mock daemon in-process** toggle in the Session
view is on, hitting **Connect** spins up the mock listener on the configured
socket path and the same SwiftUI state machine drives end-to-end without any
external dependency. The mock advertises a Quest 3 capability set, accepts a
`helloFromMac`, and streams synthetic `metrics` samples at 10 Hz.

## Control protocol (v0)

The control plane is **JSON, line-delimited, over a Unix domain socket**.
Default path resolves to `~/Library/Caches/fuvr/control.sock`, falling back
to `/tmp/fuvr.sock`.

Envelope:

```json
{ "v": 1, "type": "helloFromMac", "payload": { ... } }
```

The Swift types in `ControlMessage.swift` mirror the Cap'n Proto schema in
`proto/fuvr.capnp` exactly for the relevant union arms (`helloFromQuest`,
`helloFromMac`, `sessionStart`, `sessionStop`, `error`) and add two
control-surface-only event types — `metrics` and `log` — for the diagnostics
and log views. Once the daemon side stabilises, this layer will be replaced
by Cap'n Proto without changing the SwiftUI surface.

## Design rationale

- **Network.framework over SwiftNIO** — `NWConnection.unix(path:)` is a single
  Apple framework with full async support, zero deps, and a tiny binary
  footprint. SwiftNIO would add ~MB of binary size for no benefit on a
  control plane that handles tens of messages per second.
- **JSON over UDS for v0** — keeps daemon iteration unblocked. The Cap'n
  Proto schema is the long-term wire contract for the data plane (video
  fragments, pose at 1 kHz); the control plane has no throughput budget that
  JSON cannot meet.
- **No third-party SPM packages** — everything depends on Foundation, Network,
  SwiftUI, and Swift Charts. This keeps the app trivially Xcode-buildable
  and signable.
- **Liquid Glass where it fits** — `regularMaterial` / `thinMaterial`
  backgrounds throughout; `.glassEffect()` is only invoked under
  `if #available(macOS 26.0, *)` so the app renders cleanly on macOS 14+.

## Screenshots

_Placeholder — populate after first internal build._

## Status surfaces

The Diagnostics view tracks the metrics flagged as risk-bearing in `SPEC.md` §6:
encode latency (VideoToolbox HEVC RealTime), RTT and packet loss (ADB / Wi-Fi
fallback). The Log view pipes daemon log lines verbatim with level filtering.
