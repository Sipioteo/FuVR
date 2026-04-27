# macOS quirks for `CGVirtualDisplay`

This document catalogues version-specific and hardware-specific quirks for the
private `CGVirtualDisplay` API surface that `fuvr-vdisplay-helper` uses. See
[ADR-0004](../../docs/adr/0004-cgvirtualdisplay-subprocess.md) and SPEC §3.1.1
for the rationale of why we use this API at all.

Re-validate this document after every macOS major release.

## macOS 14 (Sonoma) — baseline

- `CGVirtualDisplay`/`CGVirtualDisplayDescriptor`/`CGVirtualDisplayMode`
  selectors stable as documented in
  [`include/private/CGVirtualDisplay.h`](../include/private/CGVirtualDisplay.h).
- `applySettings:` returns `YES` and `displayID` returns a valid
  `CGDirectDisplayID` within ~250 ms.
- ScreenCaptureKit (macOS 13+) can capture the resulting display once
  WindowServer publishes it (additional 1 frame after `applySettings:` returns).

**Reproduction:**
```
./fuvr-vdisplay-helper --width 1920 --height 1080 --refresh 60
# Expected: display_id=NNN within 2 s; display visible in System Settings → Displays.
```

## macOS 15 (Sequoia) — known regressions

- `CGVirtualDisplaySettings.setHiDPI:` ignored on certain GPU configurations;
  the resulting display is created at 1x even when HiDPI=1 is requested. No
  workaround in the helper; downstream consumers should query backing scale.
- WindowServer occasionally drops the display on display-arrangement changes
  (user opens System Settings → Displays). The helper detects this only via
  ScreenCaptureKit returning no frames; recovery is parent-driven (kill +
  respawn).

**Reproduction:**
```
./fuvr-vdisplay-helper --width 2560 --height 1440 --refresh 60
# Open System Settings → Displays, drag the virtual display.
# Expected (regression): SCStream stops delivering frames silently.
```

## macOS 16 ("Tahoe") — M4/M5 DCP pipe-0 6720-pixel limit

- Apple's M4/M5 DCP firmware caps any single mode's width at 6720 pixels on
  pipe 0 (the only pipe a virtual display gets). Requesting `width > 6720`
  via a single `CGVirtualDisplayMode` causes `applySettings:` to either
  return `NO` or, worse, succeed but produce a black framebuffer.
- Workaround: split the mode list across two `CGVirtualDisplayMode` entries
  whose widths sum to the requested width. `clamp_dimensions` in the helper
  performs this split automatically when `width > kPipe0PixelLimit`.
- Per-eye stereo at 4128×2208 is **below** the limit (single-mode is fine).
  Composited side-by-side stereo at 8256×2208 (some prototypes) requires the
  split.

**Reproduction:**
```
./fuvr-vdisplay-helper --width 8192 --height 2160 --refresh 90
# Expected on M4/M5 (Tahoe):
#   stderr "WARNING 8192x2160 exceeds M4/M5 pipe-0 limit (6720 px); splitting into 2 modes."
#   stdout "display_id=NNN"
# Without the split, the display goes black after first frame.
```

## Multi-display setups

- A `CGVirtualDisplay` does NOT receive WindowServer focus events the same
  way a real display does when running in headless / closed-lid mode. In
  particular, `CGDisplayBounds` reflects the virtual display, but the
  cursor cannot be warped onto it via `CGWarpMouseCursorPosition` until at
  least one app window exists on it.
- ScreenCaptureKit captures the virtual display correctly even when it is
  the **background** monitor (i.e. not the active focus). This is the
  primary reason FuVR uses SCK over CGDisplayStream.
- Closed-lid (clamshell) operation: `CGVirtualDisplay` survives. The internal
  display goes dark; the virtual one keeps publishing.

**Reproduction:**
```
# With external monitor as primary, lid closed:
./fuvr-vdisplay-helper --width 2880 --height 1800 --refresh 90
# Expected: virtual display appears, captured frames have the wallpaper of
# the active user session. Switching primary in System Settings has no effect.
```

## Watchdog / lifetime

- The helper's parent-death policy is **stdin-EOF**: when the parent closes
  the write end of the helper's stdin pipe, `read(STDIN, …)` returns 0 and
  the helper exits, releasing the `CGVirtualDisplay` cleanly. This is the
  simplest cross-version mechanism — no `prctl(PR_SET_PDEATHSIG)` (Linux-only)
  and no Mach exception ports needed.
- `--watchdog` (default ON) is documented as the stdin-EOF behaviour. Pass
  `--no-watchdog` to disable and rely on explicit SIGTERM only (used for
  detached debugging sessions).

## Cross-references

- SPEC §3.1.1 (Virtual Display strategy)
- ADR-0004 (CGVirtualDisplay subprocess decision)
- `virtual-display-helper/src/clamp_dimensions.h` (split logic)
