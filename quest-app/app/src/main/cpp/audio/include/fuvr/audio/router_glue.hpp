// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace fuvr {
class ProtocolRouter;
}

namespace fuvr::audio {

// Registration shim used by DELTA's protocol_router.cpp. The router is
// responsible for invoking `audio_handler()` whenever a datagram arrives on
// the Audio channel. We keep the API tiny and decoupled — a function-pointer
// returned at startup — to avoid forcing protocol_router.cpp to know about
// AAudioOutput / OpusAudioReceiver types.
using AudioHandler = std::function<void(const std::uint8_t*, std::size_t)>;

// Initializes the audio receiver + AAudio output (idempotent) and returns a
// callback the protocol router can wire into the Audio channel demux.
// Pass the negotiated sample rate (defaults to 48000) and channel count
// (defaults to 2). On failure returns a no-op handler.
AudioHandler install_audio_handler(std::uint32_t sampleRate = 48000,
                                   std::uint32_t channels   = 2);

// Tear down the audio output. Safe to call multiple times.
void shutdown_audio_handler();

} // namespace fuvr::audio
