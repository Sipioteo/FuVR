// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <mutex>

#include "fragment_reassembler.hpp"

namespace fuvr {

class TransportClient;
class DecoderPipeline;
class OpenXrSession;
struct PlainSessionConfig;

// Owns the channel-aware demux of inbound traffic. Calls into the decoder
// pipeline for completed video access units, replies to clock-sync pings
// over the same transport, and forwards haptic pulses to OpenXR.
class ProtocolRouter {
public:
    ProtocolRouter(TransportClient& tx, DecoderPipeline& dec, OpenXrSession& xr)
        : tx_(tx), dec_(dec), xr_(xr) {}

    void install();
    void send_hello_from_quest();

    // Latest negotiated session config from the Mac, if any.
    bool has_session_config() const { std::lock_guard<std::mutex> lk(mu_); return have_cfg_; }

private:
    void on_video(const uint8_t* data, size_t size);
    void on_control(const uint8_t* data, size_t size);

    TransportClient& tx_;
    DecoderPipeline& dec_;
    OpenXrSession& xr_;

    FragmentReassembler reassembler_;
    mutable std::mutex mu_;
    bool have_cfg_{false};
};

}
