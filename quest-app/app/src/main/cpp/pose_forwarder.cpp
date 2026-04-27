// SPDX-License-Identifier: Apache-2.0

#include "pose_forwarder.hpp"

#include "openxr_session.hpp"
#include "transport_client.hpp"

#include <android/log.h>
#include <chrono>
#include <thread>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.pose", __VA_ARGS__)

namespace fuvr {

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

    while (running_.load()) {
        next += period;

        if (xr_.is_running() && xr_.session() != XR_NULL_HANDLE) {
            const XrTime t = xr_.predicted_display_time();
            XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
            xrLocateSpace(xr_.view_space(), xr_.stage_space(), t, &loc);

            XrSpaceLocation hand_loc[2] = {{XR_TYPE_SPACE_LOCATION}, {XR_TYPE_SPACE_LOCATION}};
            for (int i = 0; i < 2; ++i) {
                if (xr_.hand_space(i) != XR_NULL_HANDLE) {
                    xrLocateSpace(xr_.hand_space(i), xr_.stage_space(), t, &hand_loc[i]);
                }
            }

            // TODO: serialize via Cap'n Proto UpstreamFrame using proto_gen/fuvr.capnp.h
            // and call tx_.send(Channel::Pose, msg.data(), msg.size()).
            (void)tx_;
        }

        std::this_thread::sleep_until(next);
    }
}

}
