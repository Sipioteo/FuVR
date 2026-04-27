// SPDX-License-Identifier: Apache-2.0
//
// Single integration point for the daemon's `Session`. The implementation is
// delivered as a template in `daemon/vdisplay/INTEGRATION_session_hook.cpp.txt`
// and must be added to `fuvr_daemon_core`'s sources by BETA at integration
// time (it depends on the full Session header, so it cannot live in the
// `fuvr_daemon_vdisplay` static lib without creating a cyclic dependency).
// See `INTEGRATION.md`.
#pragma once

#include <memory>

#include "fuvr/vdisplay/virtual_display_session.hpp"

namespace fuvr::daemon { class Session; }

namespace fuvr::daemon::vdisplay {

// Spawns the helper, starts ScreenCaptureKit against its CGVirtualDisplay, and
// pumps each captured CVPixelBuffer into `session.submitFrame(...)` on the
// CoreMedia callback queue.
//
// Returns nullptr on failure (helper / capture creation). Cleans up the helper
// internally on destruction.
//
// NOTE: This function reads from `session` only via existing public accessors.
// It does NOT take ownership; the caller (Session) owns the returned pointer
// and must destroy it before destroying `session`.
std::unique_ptr<VirtualDisplaySession> startForSession(fuvr::daemon::Session& session);

}  // namespace fuvr::daemon::vdisplay
