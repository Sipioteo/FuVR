# FuVR — macOS Control Surface

User-facing SwiftUI app that drives the FuVR PCVR streaming daemon. This package
contains only the control surface; the runtime, encoder, and transport live in
sibling directories.

## Layout

- `Sources/FuVRApp/` — SwiftUI app target (`@main`)
- `Sources/FuVRControl/` — pure-Swift library: control protocol, metrics buffer,
  Network.framework UDS client, in-process mock daemon, settings migration
- `Sources/FuVRCapnp/` — hand-rolled Cap'n Proto encoder/decoder for the
  envelope subset exchanged with `fuvrd`
- `Tests/FuVRControlTests/` — XCTest coverage for the bridge, settings
  migration, and metrics math
- `Tests/FuVRCapnpTests/` — XCTest coverage for the Cap'n Proto wire format

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
external dependency. The mock now speaks packed Cap'n Proto envelopes
identical to those produced by the real daemon and emits metrics + log
streams at 10 Hz / 0.33 Hz respectively.

## Control protocol (v1, pass 4)

Pass 4 migrated the wire from line-delimited JSON to **packed Cap'n Proto**
envelopes over a Unix domain socket, matching `proto/fuvrd.capnp`. Each
frame on the socket is a 4-byte little-endian length prefix followed by
that many bytes of packed Cap'n Proto for an `Envelope` struct.

### Cap'n Proto strategy

The `FuVRCapnp` module is a **hand-rolled** encoder/decoder for the
envelope subset the mac-app actually exchanges (`startSession`,
`stopSession`, `streamMetrics`, `streamLogs`, `streamInputs`, `ping`,
`pong`, `ok`, `error`, `startSessionAck`, `metrics`, `log`). We chose
hand-rolling over a third-party Swift Cap'n Proto library because:

1. The available Swift Cap'n Proto packages are unmaintained.
2. The subset we exchange is small and stable; the schema is frozen at
   `@0xc8a4f30f6df21a7b`.
3. A subprocess bridge would add a build dependency on libcapnp and a
   second binary to ship; `FuVRCapnp` keeps the app a single SPM target.

The slot layout for each struct is documented inline in
`Sources/FuVRCapnp/CapnpCodec.swift`. The unit tests in
`Tests/FuVRCapnpTests/` round-trip every supported arm and assert
hand-computed reference bytes for a sentinel `ping` envelope so any
divergence from libcapnp's slot allocator is caught immediately.

## Settings migration (v2)

Pass 1 used `@AppStorage` per setting. Pass 4 introduces `SettingsBundle`,
a versioned Codable struct persisted as a single JSON blob under the
`fuvr.settings.v2` key. `SettingsMigration.load()` is invoked once at app
startup. It:

1. Returns the v2 blob if present.
2. Otherwise migrates by reading each legacy `fuvr.*` key, populating a
   v2 record, and persisting it.

Existing views still read legacy keys via `@AppStorage` so the migration
is non-destructive. A future pass will move the views to read directly
from the bundle. Unit-tested in `SettingsMigrationTests`.

## Design rationale

- **Network.framework over SwiftNIO** — `NWConnection.unix(path:)` is a single
  Apple framework with full async support, zero deps, and a tiny binary
  footprint.
- **No third-party SPM packages** — everything depends on Foundation, Network,
  SwiftUI, and Swift Charts. This keeps the app trivially Xcode-buildable
  and signable.
- **Liquid Glass where it fits** — `regularMaterial` / `thinMaterial`
  backgrounds throughout; `.glassEffect()` is only invoked under
  `if #available(macOS 26.0, *)` so the app renders cleanly on macOS 14+.

## Status surfaces

- **Diagnostics**: six sparkline cards (encoder fps / encode p95 / RTT,
  decoder fps / decode p95 / packet loss) over a 30-second rolling window,
  plus an "Active session" card with session id, codec, bitrate, virtual
  display id, and clock offset.
- **Log**: chip-style level filters (debug/info/warn/error), search box,
  pause toggle, sticky-bottom auto-scroll, color-coded levels. In-memory
  ring of the last 1000 lines; nothing written to disk.
- **Onboarding**: three-step wizard (install daemon, pair Quest, test
  session) shown on first launch and re-launchable from the About panel.
