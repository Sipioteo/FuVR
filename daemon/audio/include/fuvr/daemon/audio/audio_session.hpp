// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <memory>

struct FuvrTransport;

namespace fuvr::daemon { class Session; }

namespace fuvr::daemon::audio {

// Configuration for an AudioSession bound to a Session. Sample rate / channel
// count are fixed (48 kHz stereo) to match what ScreenCaptureKit produces and
// what the Quest receiver expects. Future extension points stay struct-based.
struct AudioConfig {
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels   = 2;
};

// Owns capture + opus encoder pair, builds proto::AudioPacket envelopes,
// and ships them on the transport's Audio channel. Lifetime is tied to the
// owning Session; AudioSession does not retain the transport beyond stop().
class AudioSession {
public:
    static std::unique_ptr<AudioSession> create(FuvrTransport* transport,
                                                AudioConfig cfg);
    virtual ~AudioSession() = default;

    virtual bool start() = 0;
    virtual void stop()  = 0;

    // Test hook: synthesize a buffer of PCM frames into the encode+ship path
    // without going through ScreenCaptureKit. Used by daemon audio tests.
    virtual void injectPcmForTest(const std::int16_t* frames,
                                  std::size_t numFrames,
                                  std::uint64_t hostTimeNs) = 0;

    // Number of AudioPacket envelopes successfully handed to the transport.
    virtual std::uint64_t packetsSent() const = 0;
};

// Integration entry points BETA's session.cpp calls.
//
// `startAudioFor(session)` looks at the negotiated wire SessionConfig stored on
// the Session (audioEnabled flag) and, if true, allocates an AudioSession and
// owns it via a per-Session map keyed by `session.id()`. `stopAudioFor` tears
// it down. Both are idempotent.
void startAudioFor(Session& session, FuvrTransport* transport);
void stopAudioFor(Session& session);

} // namespace fuvr::daemon::audio
