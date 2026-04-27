// SPDX-License-Identifier: Apache-2.0
//
// Unit tests for VirtualDisplaySession using fakes for the helper and the
// SckCapture factory. No GUI session required.
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <thread>

#include "fuvr/vdisplay/virtual_display_session.hpp"

namespace {

// Fake SckCapture that immediately fires N synthetic frames on `start()`.
class FakeCapture final : public fuvr::vdisplay::SckCapture {
public:
    FakeCapture(FrameSink sink, int frames)
        : sink_(std::move(sink)), frames_(frames) {}
    ~FakeCapture() override { stop(); }

    void start() override {
        if (started_) return;
        started_ = true;
        // Synchronous delivery is fine; the production callback is on a
        // CoreMedia queue but the contract is "call sink with a borrowed pb".
        for (int i = 0; i < frames_; ++i) {
            // We pass a non-null sentinel because the test sink is asked only
            // to count, not to dereference. Callers that dereference the
            // pointer would need a real pixel buffer.
            CVPixelBufferRef sentinel =
                reinterpret_cast<CVPixelBufferRef>(0xC0FFEEULL + i);
            sink_(sentinel, /*hostNs*/ 1000000ULL * (i + 1));
        }
    }
    void stop() override { started_ = false; }

private:
    FrameSink sink_;
    int       frames_  = 0;
    bool      started_ = false;
};

}  // namespace

TEST(VirtualDisplaySession, StartFailsWithBadParams) {
    fuvr::daemon::vdisplay::StartParams p;
    p.frameSink = [](CVPixelBufferRef, uint64_t, uint64_t) {};
    EXPECT_EQ(fuvr::daemon::vdisplay::start(p), nullptr);
}

TEST(VirtualDisplaySession, FakeHelperPlusFakeCaptureDeliversFrames) {
    std::atomic<int> frames{0};
    bool helperKilled = false;

    fuvr::daemon::vdisplay::StartParams p;
    p.width = 1920;
    p.height = 1080;
    p.refreshHz = 60;
    p.frameSink = [&](CVPixelBufferRef pb, uint64_t fid, uint64_t hostNs) {
        (void)pb; (void)hostNs;
        // Frame ids must be monotonic from 0.
        EXPECT_EQ(fid, static_cast<uint64_t>(frames.load()));
        frames.fetch_add(1);
    };
    p.helperSpawn = [](uint32_t w, uint32_t h, uint32_t hz,
                       uint32_t* outId, void** outOp) {
        EXPECT_EQ(w, 1920u);
        EXPECT_EQ(h, 1080u);
        EXPECT_EQ(hz, 60u);
        *outId = 0xDEADBEEF;
        *outOp = reinterpret_cast<void*>(0x1);
        return true;
    };
    p.helperKill = [&](void* op) {
        EXPECT_EQ(op, reinterpret_cast<void*>(0x1));
        helperKilled = true;
    };
    p.captureFactory = [](uint32_t did, uint32_t fps,
                          fuvr::vdisplay::SckCapture::FrameSink s) {
        EXPECT_EQ(did, 0xDEADBEEFu);
        EXPECT_EQ(fps, 60u);
        return std::unique_ptr<fuvr::vdisplay::SckCapture>(
            new FakeCapture(std::move(s), /*frames*/ 5));
    };

    auto session = fuvr::daemon::vdisplay::start(p);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->displayId(), 0xDEADBEEFu);
    EXPECT_EQ(frames.load(), 5);

    session->stop();
    EXPECT_TRUE(helperKilled);
}

TEST(VirtualDisplaySession, HelperSpawnFailureReturnsNull) {
    fuvr::daemon::vdisplay::StartParams p;
    p.width = 1280; p.height = 720; p.refreshHz = 60;
    p.frameSink = [](CVPixelBufferRef, uint64_t, uint64_t) {};
    p.helperSpawn = [](uint32_t, uint32_t, uint32_t, uint32_t*, void**) {
        return false;
    };
    EXPECT_EQ(fuvr::daemon::vdisplay::start(p), nullptr);
}

TEST(VirtualDisplaySession, CaptureFactoryFailureKillsHelper) {
    bool helperKilled = false;
    fuvr::daemon::vdisplay::StartParams p;
    p.width = 1280; p.height = 720; p.refreshHz = 60;
    p.frameSink = [](CVPixelBufferRef, uint64_t, uint64_t) {};
    p.helperSpawn = [](uint32_t, uint32_t, uint32_t, uint32_t* outId, void** outOp) {
        *outId = 7; *outOp = reinterpret_cast<void*>(0x42); return true;
    };
    p.helperKill = [&](void*) { helperKilled = true; };
    p.captureFactory = [](uint32_t, uint32_t,
                          fuvr::vdisplay::SckCapture::FrameSink) {
        return std::unique_ptr<fuvr::vdisplay::SckCapture>{};
    };
    EXPECT_EQ(fuvr::daemon::vdisplay::start(p), nullptr);
    EXPECT_TRUE(helperKilled);
}
