// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/opus_encoder.hpp"

#include <opus.h>

#include <cstdint>
#include <cstdlib>

namespace fuvr::audio {

namespace {

class OpusEncoderImpl final : public OpusEncoderWrap {
public:
    OpusEncoderImpl(std::uint32_t sr, std::uint32_t ch, ::OpusEncoder* enc)
        : sr_(sr), ch_(ch), enc_(enc) {
        framesPerPacket_ = (sr_ * kFrameMs) / 1000u;
    }
    ~OpusEncoderImpl() override {
        if (enc_) opus_encoder_destroy(enc_);
    }

    std::size_t encode(const std::int16_t* pcm,
                       std::size_t frames,
                       std::span<std::uint8_t> out) override {
        if (!enc_ || frames != framesPerPacket_) return 0;
        if (out.empty()) return 0;
        int n = opus_encode(enc_, pcm, (int)frames,
                            out.data(), (opus_int32)out.size());
        if (n < 0) return 0;
        return (std::size_t)n;
    }

    std::uint32_t sampleRate() const override { return sr_; }
    std::uint32_t channels()   const override { return ch_; }
    std::uint32_t framesPerPacket() const override { return framesPerPacket_; }

private:
    std::uint32_t sr_;
    std::uint32_t ch_;
    std::uint32_t framesPerPacket_{0};
    ::OpusEncoder* enc_{nullptr};
};

} // namespace

std::unique_ptr<OpusEncoderWrap> OpusEncoderWrap::create(std::uint32_t sampleRate,
                                                         std::uint32_t channels) {
    if (channels != 1 && channels != 2) return nullptr;
    if (sampleRate != 8000 && sampleRate != 12000 &&
        sampleRate != 16000 && sampleRate != 24000 &&
        sampleRate != 48000) {
        return nullptr;
    }
    int err = OPUS_OK;
    ::OpusEncoder* enc = opus_encoder_create((opus_int32)sampleRate,
                                             (int)channels,
                                             OPUS_APPLICATION_RESTRICTED_LOWDELAY,
                                             &err);
    if (err != OPUS_OK || !enc) return nullptr;

    opus_encoder_ctl(enc, OPUS_SET_BITRATE((opus_int32)kBitrateBps));
    opus_encoder_ctl(enc, OPUS_SET_VBR(0));
    opus_encoder_ctl(enc, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    opus_encoder_ctl(enc, OPUS_SET_PACKET_LOSS_PERC(5));
    opus_encoder_ctl(enc, OPUS_SET_DTX(0));
    opus_encoder_ctl(enc, OPUS_SET_INBAND_FEC(0));

    return std::make_unique<OpusEncoderImpl>(sampleRate, channels, enc);
}

} // namespace fuvr::audio
