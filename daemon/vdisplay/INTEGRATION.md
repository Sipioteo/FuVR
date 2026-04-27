# daemon/vdisplay — integration patch for BETA

THETA owns `daemon/vdisplay/**` and `virtual-display-helper/**`. This document
specifies the **only** code change BETA (daemon lead) needs to apply to wire
the virtual-display flow into a `Session`. The change is intentionally small
and local; THETA has already prepared everything else.

## CMake (already appended by THETA)

`daemon/CMakeLists.txt` ends with:

```cmake
add_subdirectory(vdisplay)
```

BETA must additionally link `fuvr_daemon_core` against the new static lib.
Add the single line `fuvr_daemon_vdisplay` to the existing
`target_link_libraries(fuvr_daemon_core PUBLIC ...)` block in
`daemon/CMakeLists.txt`. (This is the only daemon CMake edit BETA owns.)

## Source patch (3 lines)

Apply inside `daemon/src/session.cpp`, in the `Session::Session` constructor,
**replacing** the existing `if (cfg_.enableVirtualDisplay) { ... }` block with
a call to the new orchestrator. Diff:

```diff
-    if (cfg_.enableVirtualDisplay) {
-        vdisplay_ = fuvr_vdisplay_spawn(cfg_.perEyeWidth * 2, cfg_.perEyeHeight,
-                                        cfg_.refreshRateHz);
-        if (vdisplay_) virtualDisplayId_ = fuvr_vdisplay_id(vdisplay_);
-    }
+    if (cfg_.enableVirtualDisplay) {
+        vdisplaySession_ = fuvr::daemon::vdisplay::startForSession(*this);
+        if (vdisplaySession_) virtualDisplayId_ = vdisplaySession_->displayId();
+    }
```

The corresponding header change in `daemon/include/fuvr/session.hpp` is also
3 lines:

```diff
-struct fuvr_vdisplay_handle;
+namespace fuvr::daemon::vdisplay { class VirtualDisplaySession; }
 // ...
-    fuvr_vdisplay_handle* vdisplay_ = nullptr;
+    std::unique_ptr<fuvr::daemon::vdisplay::VirtualDisplaySession> vdisplaySession_;
```

And the destructor body in `session.cpp` loses its `fuvr_vdisplay_kill` line
(the unique_ptr handles it).

## The hook function THETA exports

THETA exposes a single helper symbol declared in
`fuvr/vdisplay/session_hook.hpp` and implemented in `session_hook.cpp`. It
takes a `fuvr::daemon::Session&`, reads `cfg_.perEyeWidth`/`perEyeHeight`/
`refreshRateHz` via getters BETA already exposes (or via friend if needed),
spawns the helper, starts the SckCapture, and on each captured frame calls
`session.submitFrame(...)` with a synthetic frame id and the host time. BETA
must not call into anything else from `daemon/vdisplay/`; the hook is the
single integration point.

## Expected 3-line summary BETA needs to apply

1. `daemon/CMakeLists.txt`: add `fuvr_daemon_vdisplay` to `fuvr_daemon_core`'s
   `target_link_libraries(... PUBLIC ...)` list.
2. `daemon/include/fuvr/session.hpp`: replace the `fuvr_vdisplay_handle*`
   member with `std::unique_ptr<fuvr::daemon::vdisplay::VirtualDisplaySession>`.
3. `daemon/src/session.cpp`: replace the `fuvr_vdisplay_spawn` call inside
   `Session::Session` with `fuvr::daemon::vdisplay::startForSession(*this)`,
   and drop the `fuvr_vdisplay_kill` from the destructor.
