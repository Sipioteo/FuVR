# mac-app TODO

- Move SwiftUI views off legacy `@AppStorage` keys onto the
  `SettingsBundle` v2 blob and call `SettingsMigration.purgeLegacy(:)`
  (TBD) once one stable release has shipped.
- Add SwiftUI Previews using `MockDaemon` for each view (currently only
  `AppState` exercises it through Connect).
- Sparkline x-axis with wall-clock time labels once daemon timestamps are
  authoritative.
- App icon (placeholder `AppIcon.appiconset` is empty).
- Localisation pass — strings are inline English.
- Quest device picker once multi-headset is supported.
- Hardware accelerometer for the status pill animation on connect.
- Replace simulated mDNS discovery in OnboardingView with real
  `NWBrowser` browse for `_fuvr-quest._tcp` once the daemon registers it.
- Wire the onboarding "test session" step to the daemon's
  `streamEncodeStats` arm (added @19 in pass 4) instead of counting
  metrics-stream ticks.
