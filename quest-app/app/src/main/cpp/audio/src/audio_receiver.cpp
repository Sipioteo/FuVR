// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/audio_receiver.hpp"

#include <opus.h>

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/io.h>
#include <kj/array.h>

#include "fuvr.capnp.h"

#include <atomic>
#include <cstring>
#include <vector>

namespace fuvr::audio {

bool decode_audio_packet(const std::uint8_t* data, std::size_t size,
                         PlainAudioPacket& out,
                         std::vector<std::uint8_t>& payloadCopy) {
    try {
        kj::ArrayPtr<const kj::byte> bytes(
            reinterpret_cast<const kj::byte*>(data), size);
        kj::ArrayInputStream is(bytes);
        ::capnp::ReaderOptions opts;
        opts.traversalLimitInWords = 64ull * 1024ull * 1024ull;
        ::capnp::PackedMessageReader reader(is, opts);
        auto pkt = reader.getRoot<::fuvr::proto::AudioPacket>();
        out.timestampNs = pkt.getTimestampNs();
        out.sampleRate  = pkt.getSampleRate();
        out.channels    = pkt.getChannels();
        out.codec       = (int)pkt.getCodec();
        auto pl = pkt.getPayload();
        payloadCopy.assign(pl.begin(), pl.end());
        out.payload     = payloadCopy.data();
        out.payloadSize = payloadCopy.size();
        return true;
    } catch (...) {
        return false;
    }
}

namespace {

class OpusAudioReceiverImpl final : public OpusAudioReceiver {
public:
    OpusAudioReceiverImpl(PcmSink* sink, std::uint32_t sr, std::uint32_t ch)
        : sink_(sink), sr_(sr), ch_(ch) {
        int err = OPUS_OK;
        decoder_ = opus_decoder_create((opus_int32)sr, (int)ch, &err);
        if (err != OPUS_OK) decoder_ = nullptr;
        // Worst-case 120 ms frame at 48 kHz.
        scratch_.resize(48000 * 120 / 1000 * ch_);
    }
    ~OpusAudioReceiverImpl() override {
        if (decoder_) opus_decoder_destroy(decoder_);
    }

    void onAudioBytes(const std::uint8_t* data, std::size_t size) override {
        if (!decoder_) return;
        PlainAudioPacket pkt{};
        std::vector<std::uint8_t> payload;
        if (!decode_audio_packet(data, size, pkt, payload)) return;
        if (pkt.codec != 0 /* opus */) return;
        if (pkt.payloadSize == 0) return;

        const int maxFrames = (int)(scratch_.size() / ch_);
        int got = opus_decode(decoder_,
                              pkt.payload, (opus_int32)pkt.payloadSize,
                              scratch_.data(), maxFrames, 0);
        if (got <= 0) return;
        framesDelivered_.fetch_add((std::uint64_t)got);
        if (sink_) {
            sink_->onPcm(scratch_.data(), (std::size_t)got, ch_,
                         pkt.sampleRate ? pkt.sampleRate : sr_,
                         pkt.timestampNs);
        }
    }

    std::uint64_t framesDelivered() const override { return framesDelivered_.load(); }

private:
    PcmSink* sink_;
    std::uint32_t sr_;
    std::uint32_t ch_;
    OpusDecoder* decoder_{nullptr};
    std::vector<std::int16_t> scratch_;
    std::atomic<std::uint64_t> framesDelivered_{0};
};

} // namespace

std::unique_ptr<OpusAudioReceiver> OpusAudioReceiver::create(PcmSink* sink,
                                                             std::uint32_t sr,
                                                             std::uint32_t ch) {
    return std::make_unique<OpusAudioReceiverImpl>(sink, sr, ch);
}

} // namespace fuvr::audio
