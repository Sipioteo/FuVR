// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>

#include "fragment_reassembler.hpp"
#include "loss_tracker.hpp"

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

    // Periodic Quest->Mac metrics piggy-backed on the ControlMessage.error
    // arm with a stable "q-metrics: " prefix. Pending a wire-schema bump
    // adding a real metrics arm; see quest-app/TODO.md.
    void send_metrics_if_due();

    // Latest negotiated session config from the Mac, if any.
    bool has_session_config() const { std::lock_guard<std::mutex> lk(mu_); return have_cfg_; }

    // Transport-loss percent surfaced by GAMMA's transport stats. The metrics
    // line picks this up next time send_metrics_if_due fires.
    void set_transport_loss_pct(float pct) { transport_loss_pct_ = pct; }

    // Drives bitrate-req / keyframe-req emission. Called every frame that
    // exhibited reassembly loss or post-decode corruption respectively.
    void note_loss_frame();
    void note_decode_failure();
    void poll_adaptive_signals();

private:
    void on_video(const uint8_t* data, size_t size);
    void on_control(const uint8_t* data, size_t size);

    TransportClient& tx_;
    DecoderPipeline& dec_;
    OpenXrSession& xr_;

    FragmentReassembler reassembler_;
    LossTracker loss_tracker_;
    mutable std::mutex mu_;
    bool have_cfg_{false};
    uint64_t last_metrics_ns_{0};
    float transport_loss_pct_{0.0f};
    std::function<void(const uint8_t*, size_t)> audio_handler_;
};

}
