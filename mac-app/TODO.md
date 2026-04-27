# mac-app TODO

- Replace JSON control envelope with Cap'n Proto once `proto/fuvr.capnp` has
  Swift bindings. The library boundary in `FuVRControl/ControlMessage.swift`
  is the single touch point.
- Wire real daemon discovery: `launchctl` agent or auto-start binary from
  the app bundle.
- Add SwiftUI Previews using `MockDaemon` for each view (currently only
  `AppState` exercises it through Connect).
- Sparkline x-axis with wall-clock time labels once daemon timestamps are
  authoritative.
- App icon (placeholder `AppIcon.appiconset` is empty).
- Localisation pass — strings are inline English.
- Persist log to disk ring buffer for post-mortem.
- Quest device picker once multi-headset is supported.
- Hardware accelerometer for the status pill animation on connect.
