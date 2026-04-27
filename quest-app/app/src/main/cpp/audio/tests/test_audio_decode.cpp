// SPDX-License-Identifier: Apache-2.0
//
// Host-side smoke test: encode a known sine to Opus, build a packed
// proto::AudioPacket envelope, hand it to OpusAudioReceiver, assert the
// decoded sample count.

#include "fuvr/audio/audio_receiver.hpp"

#include <gtest/gtest.h>
#include <opus.h>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>

#include "fuvr.capnp.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

class CountingSink : public fuvr::audio::PcmSink {
public:
    void onPcm(const std::int16_t*, std::size_t numFrames,
               std::uint32_t, std::uint32_t, std::uint64_t) override {
        frames_.fetch_add(numFrames);
        calls_.fetch_add(1);
    }
    std::uint64_t frames() const { return frames_.load(); }
    std::uint64_t calls()  const { return calls_.load(); }
private:
    std::atomic<std::uint64_t> frames_{0};
    std::atomic<std::uint64_t> calls_{0};
};

std::vector<std::uint8_t> make_packet(const std::uint8_t* opus, std::size_t n,
                                      std::uint32_t sr, std::uint8_t ch,
                                      std::uint64_t ts) {
    ::capnp::MallocMessageBuilder msg;
    auto pkt = msg.initRoot<::fuvr::proto::AudioPacket>();
    pkt.setTimestampNs(ts);
    pkt.setCodec(::fuvr::proto::AudioCodec::OPUS);
    pkt.setSampleRate(sr);
    pkt.setChannels(ch);
    auto p = pkt.initPayload(n);
    std::memcpy(p.begin(), opus, n);
    kj::VectorOutputStream os;
    ::capnp::writePackedMessage(os, msg);
    auto bytes = os.getArray();
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

} // namespace

TEST(QuestAudioReceiver, DecodesKnownOpusPayload) {
    constexpr std::uint32_t sr = 48000;
    constexpr std::uint32_t ch = 2;
    constexpr std::uint32_t fpp = 960; // 20 ms

    int err = OPUS_OK;
    OpusEncoder* enc = opus_encoder_create((opus_int32)sr, (int)ch,
                                           OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    ASSERT_EQ(err, OPUS_OK);

    std::vector<std::int16_t> pcm(fpp * ch);
    const double w = 2.0 * 3.14159265358979 * 440.0 / (double)sr;
    for (std::uint32_t i = 0; i < fpp; ++i) {
        std::int16_t v = (std::int16_t)(0.4 * 32767.0 * std::sin(w * i));
        pcm[i * 2 + 0] = v;
        pcm[i * 2 + 1] = v;
    }
    std::vector<std::uint8_t> opusOut(4000);
    int n = opus_encode(enc, pcm.data(), (int)fpp, opusOut.data(),
                        (opus_int32)opusOut.size());
    ASSERT_GT(n, 0);
    opus_encoder_destroy(enc);

    CountingSink sink;
    auto recv = fuvr::audio::OpusAudioReceiver::create(&sink, sr, ch);
    ASSERT_NE(recv, nullptr);

    for (int i = 0; i < 5; ++i) {
        auto env = make_packet(opusOut.data(), (std::size_t)n, sr, (std::uint8_t)ch,
                               (std::uint64_t)i * 20'000'000ull);
        recv->onAudioBytes(env.data(), env.size());
    }

    EXPECT_EQ(sink.calls(), 5u);
    EXPECT_EQ(sink.frames(), (std::uint64_t)fpp * 5u);
    EXPECT_EQ(recv->framesDelivered(), (std::uint64_t)fpp * 5u);
}

TEST(QuestAudioReceiver, RejectsGarbage) {
    CountingSink sink;
    auto recv = fuvr::audio::OpusAudioReceiver::create(&sink, 48000, 2);
    const std::uint8_t junk[] = {0xff, 0xfe, 0xfd, 0x00, 0x01};
    recv->onAudioBytes(junk, sizeof(junk));
    EXPECT_EQ(sink.calls(), 0u);
}
