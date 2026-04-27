// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <CoreVideo/CoreVideo.h>

namespace fuvr::vdisplay {

// SckCapture wraps a ScreenCaptureKit `SCStream` against a single CGDirectDisplayID.
//
// Lifetime / threading
// --------------------
//   - `create()` builds the capture object synchronously but does NOT start the
//     stream. Call `start()` to begin delivery; `stop()` to halt it. Destroying
//     the object implicitly stops the stream.
//   - The frame sink is invoked from a CoreMedia callback dispatch queue (the
//     same queue SCStream's output handler runs on). It is NOT safe to assume
//     the main thread.
//   - The CVPixelBufferRef passed to the sink is owned by ScreenCaptureKit for
//     the duration of the callback. The sink MUST `CFRetain` it (or copy out
//     of the IOSurface) before returning if it intends to use it asynchronously.
//
// Hardware caveats
// ----------------
//   - ScreenCaptureKit cannot capture an unconfigured CGVirtualDisplay; the
//     display must be visible to WindowServer (i.e. the helper has finished
//     `applySettings:` and a SCShareableContent refresh sees it).
//   - Capture against a virtual display ALWAYS requires a logged-in GUI session
//     and Screen Recording TCC permission for the host process.

class SckCapture {
public:
    using FrameSink = std::function<void(CVPixelBufferRef, uint64_t hostTimeNs)>;

    // Returns nullptr on early failure (display not enumerable, ScreenCaptureKit
    // unavailable). A late failure (TCC denial, stream error after `start()`)
    // surfaces as the stream simply not delivering frames; check liveness via
    // metrics on the FrameSink.
    static std::unique_ptr<SckCapture> create(uint32_t displayId,
                                              uint32_t fps,
                                              FrameSink sink);

    virtual ~SckCapture() = default;

    virtual void start() = 0;
    virtual void stop()  = 0;
};

}  // namespace fuvr::vdisplay
