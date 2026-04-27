// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include <CoreVideo/CoreVideo.h>
#include <mach/mach.h>

namespace fuvr::daemon {

// Resolve a mach send-right naming an IOSurface into a retained CVPixelBufferRef.
// Returns nullptr on failure. Caller owns the returned reference.
CVPixelBufferRef pixelBufferFromMachPort(mach_port_t port);

} // namespace fuvr::daemon
