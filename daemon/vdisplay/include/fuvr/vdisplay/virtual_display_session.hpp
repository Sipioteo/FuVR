// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <CoreVideo/CoreVideo.h>

#include "fuvr/vdisplay/sck_capture.hpp"

namespace fuvr::daemon::vdisplay {

// Pluggable encoder sink: a single CVPixelBuffer per captured frame goes here.
// In production this is a thin adapter around `Session::submitFrame`. In tests
// it is a counter / mock.
//
// The implementation invokes the sink synchronously from the SckCapture
// callback queue. The sink MUST treat `pb` as borrowed and CFRetain if it
// needs to use the buffer past the callback.
using EncoderFrameSink =
    std::function<void(CVPixelBufferRef pb, uint64_t frameId, uint64_t hostTimeNs)>;

// Pluggable helper-spawn / capture-create hooks. Real production uses the C
// ABI in `virtual-display-helper/` and the real `SckCapture::create`. Tests
// plug fakes in via the factories below.
struct StartParams {
    uint32_t width        = 0;
    uint32_t height       = 0;
    uint32_t refreshHz    = 90;
    EncoderFrameSink frameSink;

    // Test seam: when set, called instead of `fuvr_vdisplay_spawn` and must
    // return a non-zero CGDirectDisplayID. Default uses the real helper.
    using HelperSpawn = std::function<bool(uint32_t /*w*/, uint32_t /*h*/,
                                           uint32_t /*hz*/,
                                           uint32_t* /*out_display_id*/,
                                           void**   /*out_opaque*/)>;
    using HelperKill  = std::function<void(void* /*opaque*/)>;
    HelperSpawn helperSpawn;
    HelperKill  helperKill;

    // Test seam: SckCapture factory. Default uses `SckCapture::create`.
    using CaptureFactory =
        std::function<std::unique_ptr<fuvr::vdisplay::SckCapture>(
            uint32_t /*displayId*/, uint32_t /*fps*/,
            fuvr::vdisplay::SckCapture::FrameSink)>;
    CaptureFactory captureFactory;
};

class VirtualDisplaySession {
public:
    virtual ~VirtualDisplaySession() = default;
    [[nodiscard]] virtual uint32_t displayId() const = 0;
    virtual void stop() = 0;
};

// Returns nullptr on failure (helper spawn failed, capture create failed).
std::unique_ptr<VirtualDisplaySession> start(const StartParams& p);

}  // namespace fuvr::daemon::vdisplay
