// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include <CoreVideo/CoreVideo.h>
#include <mach/mach.h>

namespace fuvr::daemon {

class IOSurfaceXpcService;

// Resolve a mach send-right naming an IOSurface into a retained CVPixelBufferRef.
// Returns nullptr on failure. Caller owns the returned reference.
// Why: legacy entry point, kept for the existing in-process unit test that
// passes a mach port name directly.
CVPixelBufferRef pixelBufferFromMachPort(mach_port_t port);

// Resolve a SubmitFrameRequest's `surfaceToken` into a retained CVPixelBufferRef.
// Consults `xpcService` (out-of-process path) or the in-process registry when
// FUVR_INPROCESS_HANDOFF is set. Retries up to 16 ms before giving up. Returns
// nullptr on failure; caller owns the reference on success.
CVPixelBufferRef pixelBufferFromToken(IOSurfaceXpcService* xpcService,
                                      uint64_t surfaceToken);

// Returns the count of surface lookups that gave up after the grace window.
uint64_t missingSurfaceCount();

}  // namespace fuvr::daemon
