// SPDX-License-Identifier: Apache-2.0
#include "fuvr/audio/router_glue.hpp"
#include "fuvr/audio/audio_receiver.hpp"
#include "fuvr/audio/aaudio_output.hpp"

#include <memory>
#include <mutex>

namespace fuvr::audio {

namespace {

struct Holder {
    std::mutex mu;
    std::unique_ptr<AAudioOutput> out;
    std::unique_ptr<OpusAudioReceiver> recv;
};

Holder& holder() {
    static Holder h;
    return h;
}

} // namespace

AudioHandler install_audio_handler(std::uint32_t sampleRate,
                                   std::uint32_t channels) {
    auto& h = holder();
    {
        std::lock_guard<std::mutex> lk(h.mu);
        if (!h.out) {
            h.out = AAudioOutput::create(sampleRate, channels);
            if (h.out) h.out->start();
        }
        if (!h.recv && h.out) {
            h.recv = OpusAudioReceiver::create(h.out.get(), sampleRate, channels);
        }
    }
    return [](const std::uint8_t* data, std::size_t size) {
        auto& hh = holder();
        std::lock_guard<std::mutex> lk(hh.mu);
        if (hh.recv) hh.recv->onAudioBytes(data, size);
    };
}

void shutdown_audio_handler() {
    auto& h = holder();
    std::lock_guard<std::mutex> lk(h.mu);
    h.recv.reset();
    if (h.out) {
        h.out->stop();
        h.out.reset();
    }
}

} // namespace fuvr::audio
