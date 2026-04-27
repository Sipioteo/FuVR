// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fuvr/audio/audio_receiver.hpp"

#include <cstdint>
#include <memory>

namespace fuvr::audio {

// AAudio low-latency output, configured for stereo S16 with a callback-driven
// stream. Owns a ring buffer sized for ~40 ms of audio; on underrun the
// callback returns silence. Thread-safe; the network thread enqueues PCM,
// the AAudio thread drains it.
class AAudioOutput : public PcmSink {
public:
    static std::unique_ptr<AAudioOutput> create(std::uint32_t sampleRate,
                                                std::uint32_t channels);
    ~AAudioOutput() override = default;

    virtual bool start() = 0;
    virtual void stop()  = 0;

    using PcmSink::onPcm;
};

} // namespace fuvr::audio
