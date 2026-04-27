# TODO — virtual-display-helper

## Blockers / risks

- Private `CGVirtualDisplay` headers are signature-only and untested against a real macOS SDK. First link attempt on macOS 14/15 may surface mismatched selectors (`setSizeInMillimeters:` / `serialNum` are the typical churn points).
- `applySettings:` semantics on M4/M5 with split modes are unverified. The split logic in `clamp_dimensions` is a best-effort placeholder; real device validation needed.
- The control library's `display_id=` parser is line-based with a 10 s overall timeout (`50 ms × 200`). May need tuning for cold-start where WindowServer is busy.

## Follow-ups

- Wire the helper lifecycle into `runtime-macos` once the runtime exposes a session-start hook.
- Promote `display_id=` to a richer JSON envelope when a second helper-emitted field is needed (e.g. capability flags from M4/M5 detection).
- Add a Cap'n Proto control channel over the stdin pipe instead of one-shot args, if dynamic mode-switching becomes a requirement.

## Out of scope (per SPEC §3.1.1)

- **DriverKit display extension.** Apple does not document display extensions in DriverKit; only USB/HID/Audio guides exist. Will not pursue.
- **Legacy kext.** Requires Reduced Security on Apple Silicon, breaks distribution. Will not pursue.
