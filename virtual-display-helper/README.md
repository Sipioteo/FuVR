# virtual-display-helper

Subprocess helper that creates a `CGVirtualDisplay` for FuVR's optional
"extended display VR mode" (SPEC §3.1.1, phase 2).

## Why a subprocess

`CGVirtualDisplay` is a **private** Apple API. It's used in production by
BetterDisplay, Lumen, and the DisplayLink driver, but Apple does not commit
to its stability. Two reasons we isolate it in its own binary:

1. **TCC / WindowServer isolation.** Creating virtual displays from the main
   FuVR runtime would entangle the runtime's TCC profile with display-creation
   privileges. Lumen's pattern (subprocess helper) keeps the surface narrow.
2. **Crash containment.** If a future macOS release breaks the private API,
   only the helper crashes — the runtime can detect the failure on the pipe
   and fall back to mirror-mode capture.

## Private-API risk

The headers under `include/private/CGVirtualDisplay.h` are reverse-engineered
from public sources (notably https://github.com/w0lfschild/macOS_headers).
**They are not guaranteed to compile or link on any specific macOS version.**
Re-validate after every major macOS update.

### Re-validation checklist (run after each macOS major)

- [ ] `fuvr-vdisplay-helper --width 1920 --height 1080 --refresh 60` prints a `display_id=` line within 2 s.
- [ ] The reported display appears in `System Settings → Displays`.
- [ ] Closing stdin terminates the helper and the display disappears.
- [ ] On M4/M5: a 4128×2208 request produces the split-mode warning on stderr.
- [ ] No new symbols stripped from `CoreGraphics` (check with `nm`).

## Standalone debugging

```
./fuvr-vdisplay-helper --width 4128 --height 2208 --refresh 90 --name "FuVR debug"
# prints: display_id=<int>
# blocks until stdin EOF — Ctrl-D to terminate.
```

## C ABI

```c
#include "fuvr_vdisplay_control.h"
fuvr_vdisplay_handle* h = fuvr_vdisplay_spawn(4128, 2208, 90);
uint32_t did = fuvr_vdisplay_id(h);
fuvr_vdisplay_kill(h);
```

The control library `posix_spawn`s the helper, parses `display_id=` from
stdout, and keeps the stdin pipe open. `fuvr_vdisplay_kill` closes stdin and
waits 2 s before SIGTERM.

## Tests

`fuvr_vdisplay_tests` runs `clamp_dimensions` unit tests by default.
The end-to-end spawn test is `DISABLED_` because it requires a logged-in GUI
session (no CI). Run manually:

```
ctest --gtest_also_run_disabled_tests -R vdisplay
```
