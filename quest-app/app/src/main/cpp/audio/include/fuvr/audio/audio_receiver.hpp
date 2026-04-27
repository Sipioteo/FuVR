// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace fuvr::audio {

// Plain-old-data view of a `proto::AudioPacket` after parsing. Avoids
// leaking capnp types out of the codec translation unit.
struct PlainAudioPacket {
    std::uint64_t timestampNs{0};
    std::uint32_t sampleRate{0};
    std::uint8_t  channels{0};
    int           codec{0};   // 0=opus 1=pcm (matches proto::AudioCodec)
    const std::uint8_t* payload{nullptr};
    std::size_t   payloadSize{0};
};

// Parse a packed Cap'n Proto envelope into `out`. The payload pointer in
// `out` aliases the input buffer; copy if you need to outlive the call.
// Returns false on parse failure.
bool decode_audio_packet(const std::uint8_t* data, std::size_t size,
                         PlainAudioPacket& out,
                         std::vector<std::uint8_t>& payloadCopy);

// PCM sink interface, written into by the receiver. Implementations are
// expected to be thread-safe; the receiver invokes from the network thread.
class PcmSink {
public:
    virtual ~PcmSink() = default;
    virtual void onPcm(const std::int16_t* frames,
                       std::size_t numFrames,
                       std::uint32_t channels,
                       std::uint32_t sampleRate,
                       std::uint64_t timestampNs) = 0;
};

// Stateless-ish helper that decodes Opus AudioPackets and hands PCM to a sink.
class OpusAudioReceiver {
public:
    static std::unique_ptr<OpusAudioReceiver> create(PcmSink* sink,
                                                     std::uint32_t sampleRate,
                                                     std::uint32_t channels);
    virtual ~OpusAudioReceiver() = default;

    // Submit one transport-channel datagram (already routed to Audio).
    virtual void onAudioBytes(const std::uint8_t* data, std::size_t size) = 0;

    virtual std::uint64_t framesDelivered() const = 0;
};

} // namespace fuvr::audio
