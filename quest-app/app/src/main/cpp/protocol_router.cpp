// SPDX-License-Identifier: Apache-2.0

#include "protocol_router.hpp"

#include "transport_client.hpp"
#include "decoder_pipeline.hpp"
#include "openxr_session.hpp"
#include "proto_codec.hpp"
#include "clock_sync.hpp"
#include "metrics_format.hpp"
#ifdef __ANDROID__
#include "fuvr/audio/router_glue.hpp"
#endif

#include <android/log.h>
#include <openxr/openxr.h>
#include <cstdio>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.proto", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.proto", __VA_ARGS__)

namespace fuvr {

void ProtocolRouter::install() {
#ifdef __ANDROID__
    // Why: install_audio_handler() touches AAudio + libopus, neither of which
    // exist on the host build. Host smoke compiles see audio_handler_ as null
    // and skip the Audio case below.
    audio_handler_ = fuvr::audio::install_audio_handler();
#endif
    tx_.set_handler([this](Channel ch, const uint8_t* data, size_t size) {
        switch (ch) {
            case Channel::Video:   on_video(data, size); break;
            case Channel::Control: on_control(data, size); break;
            case Channel::Audio:
                if (audio_handler_) audio_handler_(data, size);
                break;
            default: break;
        }
    });
}

void ProtocolRouter::send_metrics_if_due() {
    constexpr uint64_t kPeriodNs = 100ULL * 1000ULL * 1000ULL;
    const uint64_t now = ClockSyncResponder::now_ns();
    if (now - last_metrics_ns_ < kPeriodNs) return;
    last_metrics_ns_ = now;

    auto m = dec_.snapshot_metrics();
    MetricsSample s;
    s.fps = m.fps;
    s.decode_p95_ms = m.decode_ms_p95;
    s.decode_avg_ms = m.decode_ms_avg;
    s.frames_delivered = m.frames_delivered;
    s.dropped_frames = m.dropped_frames;
    s.transport_loss_pct = transport_loss_pct_;

    auto line = MetricsFormatter::format(s);
    if (line.empty()) return;
    auto bytes = encode_error_message(line);
    if (!bytes.empty()) tx_.send(Channel::Control, bytes.data(), bytes.size());
}

void ProtocolRouter::note_loss_frame() {
    loss_tracker_.note_loss(ClockSyncResponder::now_ns());
}

void ProtocolRouter::note_decode_failure() {
    loss_tracker_.note_decode_failure(ClockSyncResponder::now_ns());
}

void ProtocolRouter::poll_adaptive_signals() {
    const uint64_t now = ClockSyncResponder::now_ns();
    if (auto br = loss_tracker_.poll_bitrate_request(now)) {
        auto bytes = encode_error_message(*br);
        if (!bytes.empty()) tx_.send(Channel::Control, bytes.data(), bytes.size());
    }
    if (auto kf = loss_tracker_.poll_keyframe_request(now)) {
        auto bytes = encode_error_message(*kf);
        if (!bytes.empty()) tx_.send(Channel::Control, bytes.data(), bytes.size());
    }
}

void ProtocolRouter::send_hello_from_quest() {
    PlainDeviceCapabilities caps;
    caps.deviceModel = "Quest 3";
    caps.systemVersion = "";
    caps.perEyeWidth = 2064;
    caps.perEyeHeight = 2208;
    caps.refreshRatesHz = {72, 90, 120};
    caps.supportedCodecs = {0, 1};
    caps.hasHandTracking = true;
    caps.hasEyeTracking = false;
    auto bytes = encode_hello_from_quest(caps);
    if (bytes.empty()) { LOGE("encode_hello_from_quest failed"); return; }
    tx_.send(Channel::Control, bytes.data(), bytes.size());
}

void ProtocolRouter::on_video(const uint8_t* data, size_t size) {
    PlainVideoHeader hdr{};
    auto consumed = decode_video_header(data, size, hdr);
    if (!consumed || *consumed > size) {
        LOGE("video header parse failed (size=%zu)", size);
        return;
    }
    const uint8_t* payload = data + *consumed;
    const size_t payload_size = size - *consumed;

    reassembler_.submit(hdr.frameId, hdr.fragmentIndex, hdr.fragmentCount,
                        hdr.flags, hdr.codec, hdr.targetDisplayTimeNs,
                        hdr.renderedLeft, hdr.renderedRight,
                        payload, payload_size);

    while (reassembler_.has_completed()) {
        auto au = reassembler_.take_completed();
        dec_.push_encoded(au.payload.data(), au.payload.size(),
                          au.targetDisplayTimeNs, au.isKeyframe,
                          au.renderedLeft, au.renderedRight);
    }
}

void ProtocolRouter::on_control(const uint8_t* data, size_t size) {
    PlainControlMessage msg;
    if (!decode_control_message(data, size, msg)) return;
    switch (msg.kind) {
        case ControlKind::HelloFromMac: {
            {
                std::lock_guard<std::mutex> lk(mu_);
                have_cfg_ = true;
            }
            LOGI("session config: %ux%u @%uHz codec=%d",
                 msg.sessionConfig.perEyeWidth, msg.sessionConfig.perEyeHeight,
                 msg.sessionConfig.refreshRateHz, msg.sessionConfig.videoCodec);
            // Why: width is per-eye; the side-by-side stereo frame the Mac
            // encodes is twice as wide. Stored only as a hint until the
            // decoder is restarted with the negotiated dimensions.
            dec_.set_output_size((int32_t)msg.sessionConfig.perEyeWidth * 2,
                                 (int32_t)msg.sessionConfig.perEyeHeight);
            break;
        }
        case ControlKind::ClockSync: {
            if (msg.clockSync.isPing) {
                const uint64_t t1 = ClockSyncResponder::now_ns();
                const uint64_t t2 = ClockSyncResponder::now_ns();
                auto pong = ClockSyncResponder::build_pong(msg.clockSync.t0, t1, t2);
                auto bytes = encode_clock_sync_pong(pong.t0, pong.t1, pong.t2);
                if (!bytes.empty()) tx_.send(Channel::Control, bytes.data(), bytes.size());
            }
            break;
        }
        case ControlKind::Haptic: {
            XrSession session = xr_.session();
            XrAction action = xr_.haptic_action();
            if (session == XR_NULL_HANDLE || action == XR_NULL_HANDLE) break;
            XrHapticVibration vib{XR_TYPE_HAPTIC_VIBRATION};
            vib.duration = (XrDuration)msg.haptic.durationNs;
            vib.frequency = msg.haptic.frequencyHz;
            vib.amplitude = msg.haptic.amplitude;
            XrHapticActionInfo hi{XR_TYPE_HAPTIC_ACTION_INFO};
            hi.action = action;
            // subactionPath left null -> all bound paths fire; works for the
            // skeleton. Future: route to specific hand via msg.haptic.hand.
            xrApplyHapticFeedback(session, &hi, (XrHapticBaseHeader*)&vib);
            break;
        }
        default: break;
    }
}

}
