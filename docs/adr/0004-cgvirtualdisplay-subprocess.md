# ADR-0004: CGVirtualDisplay lives in a subprocess helper

- Status: accepted
- Date: 2026-04-27

## Context

The "extended display" feature — exposing the Quest as a second monitor of the
Mac — needs a virtual display. Apple does not provide a public API for this.
Three options exist:

1. **`CGVirtualDisplay`** — private API, undocumented, used by BetterDisplay,
   DisplayLink, Lumen. Stable across macOS releases, but Apple may break it.
2. **DriverKit display extension** — Apple's public path. No documentation
   covers display extensions; existing DriverKit examples are USB/HID/Audio
   only. Pioneering an undocumented part of a documented framework.
3. **Legacy kext** — requires Reduced Security mode on Apple Silicon. Not
   distributable to normal users. Out.

Among (1) and (2), `CGVirtualDisplay` has well-trodden community precedent and
real shipping software running on it; DriverKit display extensions don't.

A separate concern: TCC and WindowServer behave badly when the same process
that creates the virtual display also calls into screen capture / window
management APIs. Lumen worked around this with a subprocess pattern.

## Decision

We use `CGVirtualDisplay`, accessed via reverse-engineered Objective-C
interface declarations vendored at `virtual-display-helper/include/private/`,
in a **dedicated subprocess** (`fuvr-vdisplay-helper`). The daemon spawns it
via `posix_spawn`, owns its lifetime via stdin pipe-close, and reads the
resulting `CGDirectDisplayID` from a single line of stdout.

Phase: this is **phase 2**. M0/M1/M2 do not depend on it. The runtime renders
into swapchain textures it owns, which the encoder captures directly — no
screen capture is involved.

## Consequences

- TCC dialogs, WindowServer state, and any future Apple sandboxing live in a
  separate process. If Apple breaks the API in macOS 27, only the helper has
  to be rewritten (or stubbed out, with the feature degraded).
- We accept the risk of private API breakage as a known cost. The helper has
  a re-validation checklist in `virtual-display-helper/README.md`.
- This is not on the critical path for shipping a streaming session: the
  primary code path renders directly into FrameSink, not through a virtual
  display.

## Alternatives considered

- **DriverKit.** Cost of pioneering an undocumented Apple framework is
  unbounded. Rejected for now; we may revisit if Apple ever documents
  display extensions.
- **Process the display creation in the daemon directly.** Rejected for the
  TCC/WindowServer isolation reason. Lumen learnt this lesson; we copy it.
