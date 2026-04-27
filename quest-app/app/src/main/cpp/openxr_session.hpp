// SPDX-License-Identifier: Apache-2.0
#pragma once

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1

#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "input_packer.hpp"

struct android_app;

namespace fuvr {

class Compositor;

struct ViewSnapshot {
    XrPosef pose{};
    XrFovf fov{};
};

class OpenXrSession {
public:
    bool create(android_app* app);
    void destroy();

    void poll_events();
    bool is_running() const { return running_; }

    void begin_frame();
    void end_frame(Compositor& compositor);

    // Used by pose forwarder.
    XrInstance instance() const { return instance_; }
    XrSession session() const { return session_; }
    XrSpace stage_space() const { return stage_space_; }
    XrSpace view_space() const { return view_space_; }
    XrSpace hand_space(int hand) const { return hand_spaces_[hand]; }
    XrTime predicted_display_time() const { return predicted_display_time_; }

    // xrSyncActions on the main action set; safe to call before reading
    // per-hand action states for the same frame. Returns true if sync ran.
    bool sync_actions();

    // Read live action state for `hand` (0=left, 1=right) into `out`.
    // Calls xrGetActionState{Boolean,Float,Vector2f}; assumes sync_actions()
    // has run for the current frame already. Returns true if the actions
    // are bound and the state was filled (active flag still reflects the
    // OpenXR-reported "isActive").
    bool read_action_state(int hand, ActionStateBundle& out) const;

    // Stage-relative HMD pose support (Task 2). Captured once when the
    // stage and view spaces are both valid. Returns identity if not yet set.
    void capture_local_origin_if_needed(XrTime t);

    const std::array<ViewSnapshot, 2>& last_views() const { return last_views_; }
    // Eye poses located against view_space (= each eye's IPD offset relative
    // to the head). Used by build_head_locked_quads to place the per-eye quad
    // directly in front of the corresponding eye in head-relative coords.
    const std::array<ViewSnapshot, 2>& last_views_view() const { return last_views_view_; }
    const std::array<XrCompositionLayerProjectionView, 2>& projection_views() const { return projection_views_; }

private:
    bool init_loader(android_app* app);
    bool create_instance(android_app* app);
    bool create_system_and_session(android_app* app);
    bool create_spaces();
    bool create_action_set();

    XrInstance instance_{XR_NULL_HANDLE};
    XrSystemId system_id_{XR_NULL_SYSTEM_ID};
    XrSession session_{XR_NULL_HANDLE};
    XrSpace stage_space_{XR_NULL_HANDLE};
    XrSpace local_space_{XR_NULL_HANDLE};
    XrSpace view_space_{XR_NULL_HANDLE};
    std::array<XrSpace, 2> hand_spaces_{XR_NULL_HANDLE, XR_NULL_HANDLE};
    std::array<XrPath, 2> hand_paths_{XR_NULL_PATH, XR_NULL_PATH};

    XrPosef local_origin_pose_{};
    bool local_origin_captured_{false};

public:
    XrSpace local_space() const { return local_space_; }
    XrPosef local_origin_pose() const { return local_origin_pose_; }
    bool local_origin_captured() const { return local_origin_captured_; }
private:

    XrActionSet action_set_{XR_NULL_HANDLE};
    XrAction pose_action_{XR_NULL_HANDLE};
    XrAction trigger_action_{XR_NULL_HANDLE};
    XrAction trigger_touch_action_{XR_NULL_HANDLE};
    XrAction grip_action_{XR_NULL_HANDLE};
    XrAction thumbstick_action_{XR_NULL_HANDLE};
    XrAction thumbstick_click_action_{XR_NULL_HANDLE};
    XrAction thumbstick_touch_action_{XR_NULL_HANDLE};
    XrAction button_a_action_{XR_NULL_HANDLE};
    XrAction button_a_touch_action_{XR_NULL_HANDLE};
    XrAction button_b_action_{XR_NULL_HANDLE};
    XrAction button_b_touch_action_{XR_NULL_HANDLE};
    XrAction system_click_action_{XR_NULL_HANDLE};
    XrAction thumbrest_action_{XR_NULL_HANDLE};
    XrAction haptic_action_{XR_NULL_HANDLE};

public:
    XrAction haptic_action() const { return haptic_action_; }
    const std::vector<XrViewConfigurationView>& view_configs() const { return view_configs_; }

    // Headset capability snapshot, populated lazily from OpenXR queries the
    // first time this is called. Used by ProtocolRouter to build a
    // helloFromQuest with values reflecting the actual headset (recommended
    // per-eye render dims, supported display refresh rates via
    // XR_FB_display_refresh_rate, system name/vendor, hand-tracking presence
    // via XR_EXT_hand_tracking system properties).
    struct CapabilitiesSnapshot {
        std::string deviceModel;       // XrSystemProperties.systemName
        std::string systemVersion;     // empty for now (no portable source)
        uint32_t perEyeWidth{0};       // recommendedImageRectWidth
        uint32_t perEyeHeight{0};      // recommendedImageRectHeight
        uint32_t maxPerEyeWidth{0};    // maxImageRectWidth
        uint32_t maxPerEyeHeight{0};   // maxImageRectHeight
        std::vector<uint32_t> refreshRatesHz;
        bool hasHandTracking{false};
        bool hasEyeTracking{false};
    };
    CapabilitiesSnapshot query_capabilities();

private:

    XrSessionState session_state_{XR_SESSION_STATE_UNKNOWN};
    bool running_{false};
    bool frame_in_flight_{false};

    XrFrameState frame_state_{XR_TYPE_FRAME_STATE};
    XrTime predicted_display_time_{0};

    std::array<ViewSnapshot, 2> last_views_{};
    std::array<ViewSnapshot, 2> last_views_view_{};
    std::array<XrCompositionLayerProjectionView, 2> projection_views_{};
    std::vector<XrViewConfigurationView> view_configs_;
};

}
