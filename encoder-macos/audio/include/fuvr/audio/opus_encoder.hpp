// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace fuvr::audio {

// Low-latency Opus encoder wrapper. Configured for 20 ms frames, 128 kbps,
// OPUS_APPLICATION_RESTRICTED_LOWDELAY. Stereo only is supported here.
class OpusEncoderWrap {
public:
    static constexpr std::uint32_t kFrameMs       = 20;
    static constexpr std::uint32_t kBitrateBps    = 128000;

    virtual ~OpusEncoderWrap() = default;

    static std::unique_ptr<OpusEncoderWrap> create(std::uint32_t sampleRate,
                                                   std::uint32_t channels);

    // Encode exactly `frames` PCM samples per channel (interleaved if stereo).
    // For 48 kHz / 20 ms that's 960 frames. Returns bytes written into `out`,
    // or 0 on failure.
    virtual std::size_t encode(const std::int16_t* pcm,
                               std::size_t frames,
                               std::span<std::uint8_t> out) = 0;

    virtual std::uint32_t sampleRate() const = 0;
    virtual std::uint32_t channels()   const = 0;
    virtual std::uint32_t framesPerPacket() const = 0;
};

} // namespace fuvr::audio
