// SPDX-License-Identifier: Apache-2.0

#include "pose_forwarder.hpp"

#include "openxr_session.hpp"
#include "transport_client.hpp"
#include "proto_codec.hpp"
#include "input_packer.hpp"

#include <android/log.h>
#include <chrono>
#include <ctime>
#include <thread>

// XR_KHR_convert_timespec_time prototype + PFN. Declared here directly to
// avoid pulling in openxr_platform.h, which on Android additionally needs
// jni + EGL headers and balloons the include surface for one extension.
extern "C" {
typedef XrResult (XRAPI_PTR *PFN_xrConvertTimespecTimeToTimeKHR)(
    XrInstance instance,
    const struct timespec* timespecTime,
    XrTime* time);
}

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

    // Resolve xrConvertTimespecTimeToTimeKHR once. It maps clock_gettime(MONO)
    // into the headset's XrTime, which we then feed to xrLocateSpace to get
    // the *current* head pose (not the future-predicted one in last_views).
    PFN_xrConvertTimespecTimeToTimeKHR pfnConvertTimespec = nullptr;
    if (xr_.instance() != XR_NULL_HANDLE) {
        xrGetInstanceProcAddr(
            xr_.instance(), "xrConvertTimespecTimeToTimeKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&pfnConvertTimespec));
    }

    while (running_.load()) {
        next += period;
        ++ticks_total;

        const bool xr_run = xr_.is_running();
        const bool xr_sess = (xr_.session() != XR_NULL_HANDLE);
        if (xr_run && xr_sess) {
            ++ticks_running;
            const XrTime t_predicted = xr_.predicted_display_time();
            // Why: send the head pose at "now" (CLOCK_MONOTONIC) rather than at
            // predictedDisplayTime. The compositor's q_now is itself derived
            // from xrLocateViews(predictedDisplayTime), so if we send that
            // same future-pose to Mac, Mac echoes it back as q_render and
            // pair(ren,now) ≡ 1.0 ⇒ ATW Δq = identity ⇒ no warp ⇒ visible
            // lag during head motion. With t_now = CLOCK_MONOTONIC, q_render
            // is sample-time and ATW gets a real Δq covering the full pipeline
            // latency to correct.
            XrTime t_now = t_predicted;
            if (pfnConvertTimespec != nullptr && xr_.instance() != XR_NULL_HANDLE) {
                struct timespec ts{};
                if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
                    XrTime converted = 0;
                    if (pfnConvertTimespec(xr_.instance(), &ts, &converted) ==
                        XR_SUCCESS) {
                        t_now = converted;
                    }
                }
            }

            // Single sync per tick: shared by pose locate + action read.
            // Use predicted display time for actions / capture (those want the
            // future-aligned data). t_now is only for the sample we send.
            xr_.sync_actions();
            xr_.capture_local_origin_if_needed(t_predicted);
            const XrTime t = t_now;

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

            // Head velocity: locate view_space (HMD center) against stage to
            // get the canonical Meta-IMU linear/angular velocity. Note we use
            // the **same** velocity for both eyes since rotational velocity is
            // shared at the head and IPD-driven linear velocity contribution
            // is below sensor noise. This is exactly how OVR / VrApi and ALVR
            // derive head velocity for the streaming case.
            XrSpaceVelocity head_vel{XR_TYPE_SPACE_VELOCITY};
            XrSpaceLocation head_loc{XR_TYPE_SPACE_LOCATION, &head_vel};
            if (xr_.view_space() != XR_NULL_HANDLE && xr_.stage_space() != XR_NULL_HANDLE) {
                xrLocateSpace(xr_.view_space(), xr_.stage_space(), t, &head_loc);
            }
            const bool head_lin_valid =
                (head_vel.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0;
            const bool head_ang_valid =
                (head_vel.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0;

            // Fresh per-eye locate at t_now (instead of cached last_views,
            // which is at predictedDisplayTime). FOV is an intrinsic of the
            // headset and doesn't change frame-to-frame, so we still take it
            // from the cached views — saves a structure copy.
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
            f.hmd.timestampNs = now_ns();
            f.hmd.predictedDisplayTimeNs = (uint64_t)t;
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
            last_heartbeat = now_hb;
        }

        std::this_thread::sleep_until(next);
    }
    LOGI("[DEBUG-POSE] forwarder thread exiting");
}

}
