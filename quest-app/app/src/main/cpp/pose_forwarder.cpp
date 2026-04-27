// SPDX-License-Identifier: Apache-2.0

#include "pose_forwarder.hpp"

#include "openxr_session.hpp"
#include "transport_client.hpp"
#include "proto_codec.hpp"
#include "input_packer.hpp"
#include "round_trip.hpp"

#include <android/log.h>
#include <chrono>
#include <ctime>
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
    XrTime last_t_predicted = 0;
    XrTime last_t_target = 0;

    while (running_.load()) {
        next += period;
        ++ticks_total;

        const bool xr_run = xr_.is_running();
        const bool xr_sess = (xr_.session() != XR_NULL_HANDLE);
        if (xr_run && xr_sess) {
            ++ticks_running;
            const XrTime t_predicted = xr_.predicted_display_time();
            // Why: predict on the Quest, not the Mac. We sample at
            // t_target = OpenXR's predicted display time + an EWMA estimate
            // of the Mac round-trip, so the pose we ship upstream is already
            // extrapolated to the moment Blender's frame will land back on
            // the Quest swapchain. The Mac-side predictor then degenerates
            // to identity for forward-stamped samples (its dt = displayTime
            // − sample.timestampNs ≈ 0), and the OS compositor's scan-out
            // timewarp absorbs the residual error against real-time IMU at
            // photons. This avoids the Mac/Quest clock-sync drift the old
            // CLOCK_MONOTONIC sample-time path coupled into Δq, and gives
            // the Quest — which owns the IMU and the display clock —
            // ownership of the lookahead term end-to-end.
            // Quest-side lookahead: sample at t_predicted + RTT so the
            // pose Blender renders with corresponds to the time the
            // frame will actually be displayed on the Quest after the
            // pipeline round-trip. Without this the image visibly
            // trails the head by the full pipeline latency (no margin =
            // no OS scan-out reprojection headroom).
            //
            // Deadband: when the user holds still, the IMU still emits
            // residual gyro noise; extrapolating that 30 ms forward
            // turns it into micro-shake. Pre-locate at t_predicted to
            // read |ω|; if below ~5 °/s, drop the lookahead and use
            // t_predicted directly. (Mac no longer re-extrapolates —
            // session.cpp uses predictor.latest() — so this is the
            // only extrapolation in the chain and we can govern it
            // tightly.)
            xr_.sync_actions();
            xr_.capture_local_origin_if_needed(t_predicted);

            XrSpaceVelocity head_vel{XR_TYPE_SPACE_VELOCITY};
            XrSpaceLocation head_loc{XR_TYPE_SPACE_LOCATION, &head_vel};
            if (xr_.view_space() != XR_NULL_HANDLE && xr_.stage_space() != XR_NULL_HANDLE) {
                xrLocateSpace(xr_.view_space(), xr_.stage_space(), t_predicted,
                              &head_loc);
            }
            const bool head_ang_valid_pre =
                (head_vel.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;
            constexpr float kStationaryAngSqr = 0.0076f;  // (5 °/s)² rad²/s²
            const float ang_sqr = head_ang_valid_pre
                ? (head_vel.angularVelocity.x * head_vel.angularVelocity.x +
                   head_vel.angularVelocity.y * head_vel.angularVelocity.y +
                   head_vel.angularVelocity.z * head_vel.angularVelocity.z)
                : 0.0f;
            const bool stationary = head_ang_valid_pre && ang_sqr < kStationaryAngSqr;
            const int64_t rtt_ns = RoundTripEstimator::current_ns();
            const XrTime t_target = stationary
                ? t_predicted
                : t_predicted + static_cast<XrTime>(rtt_ns);
            const XrTime t = t_target;
            last_t_predicted = t_predicted;
            last_t_target = t_target;

            if (!stationary) {
                head_vel = {XR_TYPE_SPACE_VELOCITY};
                head_loc = {XR_TYPE_SPACE_LOCATION, &head_vel};
                if (xr_.view_space() != XR_NULL_HANDLE &&
                    xr_.stage_space() != XR_NULL_HANDLE) {
                    xrLocateSpace(xr_.view_space(), xr_.stage_space(), t,
                                  &head_loc);
                }
            }

            // Hand poses with velocity. ALVR / Carmack: the headset runtime
            // owns the IMU integrator; its velocity is dramatically cleaner
            // than any finite-difference computed from sparse view samples.
            // Chain XrSpaceVelocity to xrLocateSpace so the runtime fills
            // linearVelocity / angularVelocity straight from the IMU Kalman.
            XrSpaceVelocity hand_vel[2] = {{XR_TYPE_SPACE_VELOCITY}, {XR_TYPE_SPACE_VELOCITY}};
            XrSpaceLocation hand_loc[2] = {{XR_TYPE_SPACE_LOCATION, &hand_vel[0]},
                                           {XR_TYPE_SPACE_LOCATION, &hand_vel[1]}};
            for (int i = 0; i < 2; ++i) {
                if (xr_.hand_space(i) != XR_NULL_HANDLE) {
                    xrLocateSpace(xr_.hand_space(i), xr_.stage_space(), t, &hand_loc[i]);
                }
            }

            const bool head_lin_valid =
                (head_vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
            const bool head_ang_valid =
                (head_vel.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;

            // Fresh per-eye locate at t_target (= predictedDisplayTime +
            // RTT). FOV is an intrinsic of the headset and doesn't change
            // frame-to-frame, so we still take it from the cached views —
            // saves a structure copy.
            const auto& cached_views = xr_.last_views();
            XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
            vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            vli.displayTime = t;
            vli.space = xr_.stage_space();
            XrViewState vstate{XR_TYPE_VIEW_STATE};
            uint32_t out_count = 0;
            XrView fresh_views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
            XrResult lvr = xrLocateViews(xr_.session(), &vli, &vstate, 2,
                                          &out_count, fresh_views);
            const bool fresh_ok =
                (lvr == XR_SUCCESS && out_count == 2 &&
                 (vstate.viewStateFlags &
                  XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0);
            XrPosef pose0 = fresh_ok ? fresh_views[0].pose : cached_views[0].pose;
            XrPosef pose1 = fresh_ok ? fresh_views[1].pose : cached_views[1].pose;

            PlainUpstreamFrame f;
            f.correlationFrameId = 0;
            // Stamp the sample with t_target: the Mac-side predictor reads
            // this as "the pose is already valid at this future time", so
            // its extrapolation dt = displayTime − timestampNs is ≈ 0 and
            // predict() returns identity (no Mac-side lookahead).
            f.hmd.timestampNs = (uint64_t)t_target;
            f.hmd.predictedDisplayTimeNs = (uint64_t)t_predicted;
            f.hmd.leftView  = to_plain(pose0, cached_views[0].fov);
            f.hmd.rightView = to_plain(pose1, cached_views[1].fov);
            if (head_lin_valid) {
                f.hmd.linVelX = head_vel.linearVelocity.x;
                f.hmd.linVelY = head_vel.linearVelocity.y;
                f.hmd.linVelZ = head_vel.linearVelocity.z;
            }
            if (head_ang_valid) {
                f.hmd.angVelX = head_vel.angularVelocity.x;
                f.hmd.angVelY = head_vel.angularVelocity.y;
                f.hmd.angVelZ = head_vel.angularVelocity.z;
            }

            for (int i = 0; i < 2; ++i) {
                f.controllers[i].hand = i;
                f.controllers[i].isActive =
                    (hand_loc[i].locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
                f.controllers[i].pose = to_plain(hand_loc[i].pose);
                if (hand_vel[i].velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) {
                    f.controllers[i].linVelX = hand_vel[i].linearVelocity.x;
                    f.controllers[i].linVelY = hand_vel[i].linearVelocity.y;
                    f.controllers[i].linVelZ = hand_vel[i].linearVelocity.z;
                }
                if (hand_vel[i].velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) {
                    f.controllers[i].angVelX = hand_vel[i].angularVelocity.x;
                    f.controllers[i].angVelY = hand_vel[i].angularVelocity.y;
                    f.controllers[i].angVelZ = hand_vel[i].angularVelocity.z;
                }

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
            const int64_t rtt_ns_hb = RoundTripEstimator::current_ns();
            const long long offset_ns =
                (long long)last_t_target - (long long)last_t_predicted;
            LOGI("[RTT] mean_ms=%.2f t_target_offset_from_predicted_ms=%.2f",
                 (double)rtt_ns_hb / 1.0e6, (double)offset_ns / 1.0e6);
            last_heartbeat = now_hb;
        }

        std::this_thread::sleep_until(next);
    }
    LOGI("[DEBUG-POSE] forwarder thread exiting");
}

}
