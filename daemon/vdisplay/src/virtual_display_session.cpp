// SPDX-License-Identifier: Apache-2.0
#include "fuvr/vdisplay/virtual_display_session.hpp"

#include <atomic>
#include <mutex>
#include <utility>

#include "fuvr_vdisplay_control.h"

namespace fuvr::daemon::vdisplay {

namespace {

bool defaultHelperSpawn(uint32_t w, uint32_t h, uint32_t hz,
                        uint32_t* outDisplayId, void** outOpaque) {
    fuvr_vdisplay_handle* hnd = fuvr_vdisplay_spawn(w, h, hz);
    if (!hnd) return false;
    uint32_t did = fuvr_vdisplay_id(hnd);
    if (did == 0) {
        fuvr_vdisplay_kill(hnd);
        return false;
    }
    if (outDisplayId) *outDisplayId = did;
    if (outOpaque)    *outOpaque    = static_cast<void*>(hnd);
    return true;
}

void defaultHelperKill(void* opaque) {
    if (!opaque) return;
    fuvr_vdisplay_kill(static_cast<fuvr_vdisplay_handle*>(opaque));
}

std::unique_ptr<fuvr::vdisplay::SckCapture> defaultCaptureFactory(
    uint32_t displayId, uint32_t fps,
    fuvr::vdisplay::SckCapture::FrameSink sink) {
    return fuvr::vdisplay::SckCapture::create(displayId, fps, std::move(sink));
}

class VirtualDisplaySessionImpl final : public VirtualDisplaySession {
public:
    VirtualDisplaySessionImpl(uint32_t displayId,
                              void* helperOpaque,
                              StartParams::HelperKill killFn,
                              std::unique_ptr<fuvr::vdisplay::SckCapture> capture)
        : displayId_(displayId),
          helperOpaque_(helperOpaque),
          killFn_(std::move(killFn)),
          capture_(std::move(capture)) {}

    ~VirtualDisplaySessionImpl() override { stop(); }

    uint32_t displayId() const override { return displayId_; }

    void stop() override {
        std::lock_guard<std::mutex> lock(mtx_);
        if (stopped_) return;
        stopped_ = true;
        if (capture_) {
            capture_->stop();
            capture_.reset();
        }
        if (helperOpaque_ && killFn_) {
            killFn_(helperOpaque_);
        }
        helperOpaque_ = nullptr;
    }

private:
    uint32_t                                       displayId_ = 0;
    void*                                          helperOpaque_ = nullptr;
    StartParams::HelperKill                        killFn_;
    std::unique_ptr<fuvr::vdisplay::SckCapture>    capture_;
    std::mutex                                     mtx_;
    bool                                           stopped_ = false;
};

}  // namespace

std::unique_ptr<VirtualDisplaySession> start(const StartParams& p) {
    if (p.width == 0 || p.height == 0 || p.refreshHz == 0 || !p.frameSink) {
        return nullptr;
    }

    auto spawnFn   = p.helperSpawn    ? p.helperSpawn    : StartParams::HelperSpawn(defaultHelperSpawn);
    auto killFn    = p.helperKill     ? p.helperKill     : StartParams::HelperKill(defaultHelperKill);
    auto captureFn = p.captureFactory ? p.captureFactory : StartParams::CaptureFactory(defaultCaptureFactory);

    uint32_t displayId = 0;
    void*    opaque    = nullptr;
    if (!spawnFn(p.width, p.height, p.refreshHz, &displayId, &opaque) || displayId == 0) {
        return nullptr;
    }

    // Frame counter shared by the SckCapture callback closure. Atomic because
    // the callback may run on a different thread than caller-visible state.
    auto frameCounter = std::make_shared<std::atomic<uint64_t>>(0);
    auto sink         = p.frameSink;

    auto captureSink = [frameCounter, sink](CVPixelBufferRef pb, uint64_t hostNs) {
        const uint64_t fid = frameCounter->fetch_add(1, std::memory_order_relaxed);
        sink(pb, fid, hostNs);
    };

    auto capture = captureFn(displayId, p.refreshHz, std::move(captureSink));
    if (!capture) {
        killFn(opaque);
        return nullptr;
    }

    capture->start();

    return std::make_unique<VirtualDisplaySessionImpl>(
        displayId, opaque, std::move(killFn), std::move(capture));
}

}  // namespace fuvr::daemon::vdisplay
