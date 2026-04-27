// SPDX-License-Identifier: Apache-2.0

#include "pose_forwarder.hpp"

#include "openxr_session.hpp"
#include "transport_client.hpp"
#include "proto_codec.hpp"
#include "input_packer.hpp"

#include <android/log.h>
#include <chrono>
#include <thread>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.pose", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "fuvr.pose", __VA_ARGS__)

namespace fuvr {

namespace {
PlainPose to_plain(const XrPosef& p) {
    PlainPose r;
    r.px = p.position.x; r.py = p.position.y; r.pz = p.position.z;
    r.ox = p.orientation.x; r.oy = p.orientation.y;
    r.oz = p.orientation.z; r.ow = p.orientation.w;
    return r;
}
PlainViewState to_plain(const XrPosef& p, const XrFovf& f) {
    PlainViewState v;
    v.pose = to_plain(p);
    v.fov.angleLeft = f.angleLeft;
    v.fov.angleRight = f.angleRight;
    v.fov.angleUp = f.angleUp;
    v.fov.angleDown = f.angleDown;
    return v;
}
uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

void PoseForwarder::start() {
    if (running_.exchange(true)) return;
    thread_ = std::thread(&PoseForwarder::run, this);
}

void PoseForwarder::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void PoseForwarder::run() {
    using namespace std::chrono;
    const auto period = microseconds(1000); // 1 kHz target
    auto next = steady_clock::now();

    LOGI("[DEBUG-POSE] forwarder thread started");
    auto last_heartbeat = steady_clock::now();
    uint64_t ticks_total = 0, ticks_running = 0, sends_ok = 0, sends_fail = 0;

    while (running_.load()) {
        next += period;
        ++ticks_total;

        const bool xr_run = xr_.is_running();
        const bool xr_sess = (xr_.session() != XR_NULL_HANDLE);
        if (xr_run && xr_sess) {
            ++ticks_running;
            const XrTime t = xr_.predicted_display_time();

            // Single sync per tick: shared by pose locate + action read.
            xr_.sync_actions();
            xr_.capture_local_origin_if_needed(t);

            XrSpaceLocation hand_loc[2] = {{XR_TYPE_SPACE_LOCATION}, {XR_TYPE_SPACE_LOCATION}};
            for (int i = 0; i < 2; ++i) {
                if (xr_.hand_space(i) != XR_NULL_HANDLE) {
                    xrLocateSpace(xr_.hand_space(i), xr_.stage_space(), t, &hand_loc[i]);
                }
            }

            const auto& views = xr_.last_views();

            PlainUpstreamFrame f;
            f.correlationFrameId = 0;
            f.hmd.timestampNs = now_ns();
            f.hmd.predictedDisplayTimeNs = (uint64_t)t;
            f.hmd.leftView  = to_plain(views[0].pose, views[0].fov);
            f.hmd.rightView = to_plain(views[1].pose, views[1].fov);

            for (int i = 0; i < 2; ++i) {
                f.controllers[i].hand = i;
                f.controllers[i].isActive =
                    (hand_loc[i].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
                f.controllers[i].pose = to_plain(hand_loc[i].pose);

                ActionStateBundle bundle;
                if (xr_.read_action_state(i, bundle)) {
                    f.inputs[i] = InputPacker::pack(bundle);
                } else {
                    f.inputs[i].hand = i;
                }
            }

            auto bytes = encode_upstream_frame(f);
            if (!bytes.empty()) {
                if (tx_.send(Channel::Pose, bytes.data(), bytes.size())) ++sends_ok;
                else ++sends_fail;
            } else {
                ++sends_fail;
            }
        }

        auto now_hb = steady_clock::now();
        if (now_hb - last_heartbeat >= seconds(1)) {
            LOGI("[DEBUG-POSE] tick: total=%llu running=%llu sends_ok=%llu sends_fail=%llu xr_run=%d xr_sess=%d",
                 (unsigned long long)ticks_total,
                 (unsigned long long)ticks_running,
                 (unsigned long long)sends_ok,
                 (unsigned long long)sends_fail,
                 (int)xr_run, (int)xr_sess);
            last_heartbeat = now_hb;
        }

        std::this_thread::sleep_until(next);
    }
    LOGI("[DEBUG-POSE] forwarder thread exiting");
}

}
