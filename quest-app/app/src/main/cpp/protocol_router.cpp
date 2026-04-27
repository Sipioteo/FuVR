// SPDX-License-Identifier: Apache-2.0

#include "protocol_router.hpp"

#include "transport_client.hpp"
#include "decoder_pipeline.hpp"
#include "openxr_session.hpp"
#include "proto_codec.hpp"

#include <android/log.h>
#include <openxr/openxr.h>
#include <chrono>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.proto", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.proto", __VA_ARGS__)

namespace fuvr {

namespace {
uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void ProtocolRouter::install() {
    tx_.set_handler([this](Channel ch, const uint8_t* data, size_t size) {
        switch (ch) {
            case Channel::Video:   on_video(data, size); break;
            case Channel::Control: on_control(data, size); break;
            default: break;
        }
    });
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
                        payload, payload_size);

    while (reassembler_.has_completed()) {
        auto au = reassembler_.take_completed();
        dec_.push_encoded(au.payload.data(), au.payload.size(),
                          au.targetDisplayTimeNs, au.isKeyframe);
    }
}

void ProtocolRouter::on_control(const uint8_t* data, size_t size) {
    PlainControlMessage msg;
    if (!decode_control_message(data, size, msg)) return;
    switch (msg.kind) {
        case ControlKind::HelloFromMac: {
            std::lock_guard<std::mutex> lk(mu_);
            have_cfg_ = true;
            LOGI("session config: %ux%u @%uHz codec=%d",
                 msg.sessionConfig.perEyeWidth, msg.sessionConfig.perEyeHeight,
                 msg.sessionConfig.refreshRateHz, msg.sessionConfig.videoCodec);
            break;
        }
        case ControlKind::ClockSync: {
            if (msg.clockSync.isPing) {
                // Echo back as pong; we don't have a separate Mac-receive timestamp,
                // so reuse t0 for both t1 and t2 (Quest can derive RTT from the round trip).
                auto bytes = encode_clock_sync_pong(msg.clockSync.t0, now_ns(), now_ns());
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
