// SPDX-License-Identifier: Apache-2.0

#include "openxr_session.hpp"
#include "compositor.hpp"

#include <android/log.h>
#include <android_native_app_glue.h>
#include <chrono>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.xr", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.xr", __VA_ARGS__)

namespace fuvr {

namespace {
constexpr XrViewConfigurationType kViewConfig = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

bool xr_check(XrResult r, const char* where) {
    if (XR_SUCCEEDED(r)) return true;
    LOGE("OpenXR call failed at %s: %d", where, r);
    return false;
}
}

bool OpenXrSession::create(android_app* app) {
    if (!init_loader(app)) return false;
    if (!create_instance(app)) return false;
    if (!create_system_and_session(app)) return false;
    if (!create_spaces()) return false;
    if (!create_action_set()) return false;
    return true;
}

bool OpenXrSession::init_loader(android_app* app) {
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    if (!XR_SUCCEEDED(xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            (PFN_xrVoidFunction*)&xrInitializeLoaderKHR))) {
        LOGE("xrInitializeLoaderKHR not available");
        return false;
    }
    XrLoaderInitInfoAndroidKHR init{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    init.applicationVM = app->activity->vm;
    init.applicationContext = app->activity->clazz;
    return xr_check(xrInitializeLoaderKHR((XrLoaderInitInfoBaseHeaderKHR*)&init), "xrInitializeLoaderKHR");
}

bool OpenXrSession::create_instance(android_app* app) {
    const char* exts[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_FB_COLOR_SPACE_EXTENSION_NAME,
        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
        XR_EXT_HAND_TRACKING_EXTENSION_NAME,
        // Why: pose_forwarder needs to query head pose at "now" (not the
        // future predictedDisplayTime) so Mac receives the sample-time pose,
        // and ATW Quest-side has a non-trivial Δq to correct the pipeline
        // latency. The macro is gated on XR_USE_TIMESPEC, so we hard-code the
        // string to keep this file independent of the platform define order.
        "XR_KHR_convert_timespec_time",
    };

    XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    android_info.applicationVM = app->activity->vm;
    android_info.applicationActivity = app->activity->clazz;

    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &android_info;
    info.enabledExtensionCount = sizeof(exts) / sizeof(exts[0]);
    info.enabledExtensionNames = exts;
    std::strcpy(info.applicationInfo.applicationName, "FuVR Quest");
    info.applicationInfo.applicationVersion = 1;
    std::strcpy(info.applicationInfo.engineName, "FuVR");
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);

    return xr_check(xrCreateInstance(&info, &instance_), "xrCreateInstance");
}

bool OpenXrSession::create_system_and_session(android_app* /*app*/) {
    XrSystemGetInfo sys_info{XR_TYPE_SYSTEM_GET_INFO};
    sys_info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xr_check(xrGetSystem(instance_, &sys_info, &system_id_), "xrGetSystem")) return false;

    uint32_t view_count = 0;
    xrEnumerateViewConfigurationViews(instance_, system_id_, kViewConfig, 0, &view_count, nullptr);
    view_configs_.assign(view_count, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(instance_, system_id_, kViewConfig, view_count, &view_count, view_configs_.data());

    // Why: OpenXR spec requires xrGet*GraphicsRequirementsKHR be called
    // before xrCreateSession when the corresponding graphics extension is
    // enabled. The Quest runtime enforces this strictly.
    PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetReqs = nullptr;
    if (xrGetInstanceProcAddr(instance_, "xrGetOpenGLESGraphicsRequirementsKHR",
                              reinterpret_cast<PFN_xrVoidFunction*>(&pfnGetReqs)) == XR_SUCCESS && pfnGetReqs) {
        XrGraphicsRequirementsOpenGLESKHR reqs{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
        pfnGetReqs(instance_, system_id_, &reqs);
    }

    // Compositor owns the EGL/GLES context; session creation needs the
    // graphics binding which Compositor populates after init().
    XrGraphicsBindingOpenGLESAndroidKHR gl_binding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gl_binding.display = eglGetCurrentDisplay();
    gl_binding.config = EGL_NO_CONFIG_KHR;
    gl_binding.context = eglGetCurrentContext();

    XrSessionCreateInfo s_info{XR_TYPE_SESSION_CREATE_INFO};
    s_info.next = &gl_binding;
    s_info.systemId = system_id_;
    return xr_check(xrCreateSession(instance_, &s_info, &session_), "xrCreateSession");
}

bool OpenXrSession::create_spaces() {
    XrReferenceSpaceCreateInfo ref{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ref.poseInReferenceSpace.orientation.w = 1.0f;

    // Why: pose_forwarder reports both local-relative and stage-relative
    // poses. STAGE may be unavailable (guardian not configured); we fall
    // back to LOCAL but still keep a separate local_space_ handle so the
    // ATW path always has a working reference.
    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    if (!xr_check(xrCreateReferenceSpace(session_, &ref, &local_space_), "local")) return false;

    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (!xr_check(xrCreateReferenceSpace(session_, &ref, &stage_space_), "stage")) {
        // Stage unavailable — alias stage_space_ to local_space_ so callers
        // that locate against stage_space_ still get a valid result.
        stage_space_ = local_space_;
    }
    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    return xr_check(xrCreateReferenceSpace(session_, &ref, &view_space_), "view");
}

namespace {
// stageOffset = stagePose⁻¹ × hmdPose, captured once and applied each frame.
// Quaternion inverse for unit q is conjugate.
XrPosef pose_inverse(const XrPosef& p) {
    XrPosef inv{};
    inv.orientation.x = -p.orientation.x;
    inv.orientation.y = -p.orientation.y;
    inv.orientation.z = -p.orientation.z;
    inv.orientation.w =  p.orientation.w;
    // -inv.orientation * p.position
    const float qx = inv.orientation.x;
    const float qy = inv.orientation.y;
    const float qz = inv.orientation.z;
    const float qw = inv.orientation.w;
    const float vx = -p.position.x, vy = -p.position.y, vz = -p.position.z;
    const float tx = 2.0f * (qy * vz - qz * vy);
    const float ty = 2.0f * (qz * vx - qx * vz);
    const float tz = 2.0f * (qx * vy - qy * vx);
    inv.position.x = vx + qw * tx + (qy * tz - qz * ty);
    inv.position.y = vy + qw * ty + (qz * tx - qx * tz);
    inv.position.z = vz + qw * tz + (qx * ty - qy * tx);
    return inv;
}
}

void OpenXrSession::capture_local_origin_if_needed(XrTime t) {
    if (local_origin_captured_) return;
    if (stage_space_ == XR_NULL_HANDLE || stage_space_ == local_space_) return;
    if (local_space_ == XR_NULL_HANDLE) return;
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (xrLocateSpace(local_space_, stage_space_, t, &loc) != XR_SUCCESS) return;
    constexpr XrSpaceLocationFlags kValid =
        XR_SPACE_LOCATION_POSITION_VALID_BIT |
        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    if ((loc.locationFlags & kValid) != kValid) return;
    local_origin_pose_ = pose_inverse(loc.pose);
    local_origin_captured_ = true;
}

bool OpenXrSession::create_action_set() {
    XrActionSetCreateInfo as{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(as.actionSetName, "fuvr_main");
    std::strcpy(as.localizedActionSetName, "FuVR Main");
    as.priority = 0;
    if (!xr_check(xrCreateActionSet(instance_, &as, &action_set_), "xrCreateActionSet")) return false;

    xrStringToPath(instance_, "/user/hand/left", &hand_paths_[0]);
    xrStringToPath(instance_, "/user/hand/right", &hand_paths_[1]);

    auto make_action = [&](const char* name, const char* loc, XrActionType type, XrAction* out) {
        XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
        std::strcpy(ai.actionName, name);
        std::strcpy(ai.localizedActionName, loc);
        ai.actionType = type;
        ai.countSubactionPaths = 2;
        ai.subactionPaths = hand_paths_.data();
        return xr_check(xrCreateAction(action_set_, &ai, out), name);
    };

    make_action("hand_pose", "Hand Pose", XR_ACTION_TYPE_POSE_INPUT, &pose_action_);
    make_action("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, &trigger_action_);
    make_action("trigger_touch", "Trigger Touch", XR_ACTION_TYPE_BOOLEAN_INPUT, &trigger_touch_action_);
    make_action("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, &grip_action_);
    make_action("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, &thumbstick_action_);
    make_action("thumbstick_click", "Thumbstick Click", XR_ACTION_TYPE_BOOLEAN_INPUT, &thumbstick_click_action_);
    make_action("thumbstick_touch", "Thumbstick Touch", XR_ACTION_TYPE_BOOLEAN_INPUT, &thumbstick_touch_action_);
    make_action("button_a", "Button A/X", XR_ACTION_TYPE_BOOLEAN_INPUT, &button_a_action_);
    make_action("button_a_touch", "Button A/X Touch", XR_ACTION_TYPE_BOOLEAN_INPUT, &button_a_touch_action_);
    make_action("button_b", "Button B/Y", XR_ACTION_TYPE_BOOLEAN_INPUT, &button_b_action_);
    make_action("button_b_touch", "Button B/Y Touch", XR_ACTION_TYPE_BOOLEAN_INPUT, &button_b_touch_action_);
    make_action("system_click", "System Click", XR_ACTION_TYPE_BOOLEAN_INPUT, &system_click_action_);
    make_action("thumbrest", "Thumbrest", XR_ACTION_TYPE_FLOAT_INPUT, &thumbrest_action_);
    make_action("haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, &haptic_action_);

    XrPath profile_path;
    xrStringToPath(instance_, "/interaction_profiles/oculus/touch_plus_controller", &profile_path);

    auto p = [&](const char* s) { XrPath o; xrStringToPath(instance_, s, &o); return o; };
    XrActionSuggestedBinding bindings[] = {
        {pose_action_,             p("/user/hand/left/input/grip/pose")},
        {pose_action_,             p("/user/hand/right/input/grip/pose")},
        {trigger_action_,          p("/user/hand/left/input/trigger/value")},
        {trigger_action_,          p("/user/hand/right/input/trigger/value")},
        {trigger_touch_action_,    p("/user/hand/left/input/trigger/touch")},
        {trigger_touch_action_,    p("/user/hand/right/input/trigger/touch")},
        {grip_action_,             p("/user/hand/left/input/squeeze/value")},
        {grip_action_,             p("/user/hand/right/input/squeeze/value")},
        {thumbstick_action_,       p("/user/hand/left/input/thumbstick")},
        {thumbstick_action_,       p("/user/hand/right/input/thumbstick")},
        {thumbstick_click_action_, p("/user/hand/left/input/thumbstick/click")},
        {thumbstick_click_action_, p("/user/hand/right/input/thumbstick/click")},
        {thumbstick_touch_action_, p("/user/hand/left/input/thumbstick/touch")},
        {thumbstick_touch_action_, p("/user/hand/right/input/thumbstick/touch")},
        {button_a_action_,         p("/user/hand/left/input/x/click")},
        {button_a_action_,         p("/user/hand/right/input/a/click")},
        {button_a_touch_action_,   p("/user/hand/left/input/x/touch")},
        {button_a_touch_action_,   p("/user/hand/right/input/a/touch")},
        {button_b_action_,         p("/user/hand/left/input/y/click")},
        {button_b_action_,         p("/user/hand/right/input/b/click")},
        {button_b_touch_action_,   p("/user/hand/left/input/y/touch")},
        {button_b_touch_action_,   p("/user/hand/right/input/b/touch")},
        {system_click_action_,     p("/user/hand/left/input/system/click")},
        {thumbrest_action_,        p("/user/hand/left/input/thumbrest/force")},
        {thumbrest_action_,        p("/user/hand/right/input/thumbrest/force")},
        {haptic_action_,           p("/user/hand/left/output/haptic")},
        {haptic_action_,           p("/user/hand/right/output/haptic")},
    };
    XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggest.interactionProfile = profile_path;
    suggest.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);
    suggest.suggestedBindings = bindings;
    xr_check(xrSuggestInteractionProfileBindings(instance_, &suggest), "xrSuggestInteractionProfileBindings");

    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = pose_action_;
        sci.subactionPath = hand_paths_[hand];
        sci.poseInActionSpace.orientation.w = 1.0f;
        xr_check(xrCreateActionSpace(session_, &sci, &hand_spaces_[hand]), "xrCreateActionSpace");
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &action_set_;
    return xr_check(xrAttachSessionActionSets(session_, &attach), "xrAttachSessionActionSets");
}

OpenXrSession::CapabilitiesSnapshot OpenXrSession::query_capabilities() {
    CapabilitiesSnapshot caps;
    if (instance_ == XR_NULL_HANDLE || system_id_ == XR_NULL_SYSTEM_ID) {
        return caps;
    }

    // ---- Recommended/max per-eye render dims from OpenXR ------------------
    if (!view_configs_.empty()) {
        // PRIMARY_STEREO has identical L/R configs on Quest; pick eye 0.
        const auto& v = view_configs_[0];
        caps.perEyeWidth   = v.recommendedImageRectWidth;
        caps.perEyeHeight  = v.recommendedImageRectHeight;
        caps.maxPerEyeWidth  = v.maxImageRectWidth;
        caps.maxPerEyeHeight = v.maxImageRectHeight;
    }

    // ---- System properties (name + hand/eye tracking) ---------------------
    XrSystemHandTrackingPropertiesEXT handProps{
        XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
    sysProps.next = &handProps;
    if (XR_SUCCEEDED(xrGetSystemProperties(instance_, system_id_, &sysProps))) {
        caps.deviceModel = sysProps.systemName;
        caps.hasHandTracking = handProps.supportsHandTracking == XR_TRUE;
    }

    // ---- Supported display refresh rates via XR_FB_display_refresh_rate ----
    // Why: Quest 3 supports {72, 80, 90, 120}; Quest 2 supports {72, 80, 90};
    // negotiating from the actual list lets the daemon pick a valid rate per
    // device generation rather than hard-coding {72, 90, 120}.
    PFN_xrEnumerateDisplayRefreshRatesFB pfnEnum = nullptr;
    if (xrGetInstanceProcAddr(instance_, "xrEnumerateDisplayRefreshRatesFB",
                              reinterpret_cast<PFN_xrVoidFunction*>(&pfnEnum)) == XR_SUCCESS &&
        pfnEnum != nullptr && session_ != XR_NULL_HANDLE) {
        uint32_t count = 0;
        if (XR_SUCCEEDED(pfnEnum(session_, 0, &count, nullptr)) && count > 0) {
            std::vector<float> rates(count, 0.0f);
            if (XR_SUCCEEDED(pfnEnum(session_, count, &count, rates.data()))) {
                caps.refreshRatesHz.reserve(count);
                for (float r : rates) {
                    // Round to nearest Hz; the wire schema uses uint32.
                    if (r > 0.0f) caps.refreshRatesHz.push_back(static_cast<uint32_t>(r + 0.5f));
                }
            }
        }
    }
    if (caps.refreshRatesHz.empty()) {
        // Sensible fallback if the extension isn't available (host build, etc.).
        caps.refreshRatesHz = {72, 90};
    }

    // Eye tracking: not currently using XR_FB_eye_tracking_social or the EXT
    // social extension. Leave false until eye-driven foveation is wired.
    caps.hasEyeTracking = false;
    return caps;
}

void OpenXrSession::poll_events() {
    XrEventDataBuffer ev{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &ev) == XR_SUCCESS) {
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* sc = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
            session_state_ = sc->state;
            if (sc->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{XR_TYPE_SESSION_BEGIN_INFO};
                bi.primaryViewConfigurationType = kViewConfig;
                xrBeginSession(session_, &bi);
                running_ = true;
            } else if (sc->state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(session_);
                running_ = false;
            }
        }
        ev = {XR_TYPE_EVENT_DATA_BUFFER};
    }

    // Why: pose_forwarder runs at 1 kHz on its own thread and must call
    // sync_actions() itself before reading per-hand state for that tick.
    // We no longer call xrSyncActions in poll_events, to avoid double-sync
    // racing the forwarder's read.
}

bool OpenXrSession::sync_actions() {
    if (!running_ || session_ == XR_NULL_HANDLE || action_set_ == XR_NULL_HANDLE) return false;
    const XrActiveActionSet active{action_set_, XR_NULL_PATH};
    XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
    si.countActiveActionSets = 1;
    si.activeActionSets = &active;
    return xrSyncActions(session_, &si) == XR_SUCCESS;
}

bool OpenXrSession::read_action_state(int hand, ActionStateBundle& out) const {
    if (hand < 0 || hand > 1) return false;
    if (session_ == XR_NULL_HANDLE) return false;
    const XrPath sub = hand_paths_[hand];

    auto get_bool = [&](XrAction a, bool& dst, bool& any_active) {
        if (a == XR_NULL_HANDLE) return;
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a; gi.subactionPath = sub;
        XrActionStateBoolean s{XR_TYPE_ACTION_STATE_BOOLEAN};
        if (xrGetActionStateBoolean(session_, &gi, &s) == XR_SUCCESS) {
            dst = s.currentState;
            any_active = any_active || s.isActive;
        }
    };
    auto get_float = [&](XrAction a, float& dst, bool& any_active) {
        if (a == XR_NULL_HANDLE) return;
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = a; gi.subactionPath = sub;
        XrActionStateFloat s{XR_TYPE_ACTION_STATE_FLOAT};
        if (xrGetActionStateFloat(session_, &gi, &s) == XR_SUCCESS) {
            dst = s.currentState;
            any_active = any_active || s.isActive;
        }
    };

    out = ActionStateBundle{};
    out.hand = hand;
    bool active = false;

    get_float(trigger_action_,        out.trigger,         active);
    get_bool (trigger_touch_action_,  out.triggerTouch,    active);
    get_float(grip_action_,           out.squeeze,         active);
    get_bool (thumbstick_click_action_, out.thumbstickClick, active);
    get_bool (thumbstick_touch_action_, out.thumbstickTouch, active);
    get_bool (button_a_action_,       out.buttonAClick,    active);
    get_bool (button_a_touch_action_, out.buttonATouch,    active);
    get_bool (button_b_action_,       out.buttonBClick,    active);
    get_bool (button_b_touch_action_, out.buttonBTouch,    active);
    get_float(thumbrest_action_,      out.thumbrest,       active);
    if (hand == 0) get_bool(system_click_action_, out.systemClick, active);

    if (thumbstick_action_ != XR_NULL_HANDLE) {
        XrActionStateGetInfo gi{XR_TYPE_ACTION_STATE_GET_INFO};
        gi.action = thumbstick_action_; gi.subactionPath = sub;
        XrActionStateVector2f s{XR_TYPE_ACTION_STATE_VECTOR2F};
        if (xrGetActionStateVector2f(session_, &gi, &s) == XR_SUCCESS) {
            out.thumbstickX = s.currentState.x;
            out.thumbstickY = s.currentState.y;
            active = active || s.isActive;
        }
    }
    out.active = active;
    return true;
}

void OpenXrSession::begin_frame() {
    if (!running_) return;
    XrFrameWaitInfo wi{XR_TYPE_FRAME_WAIT_INFO};
    frame_state_ = {XR_TYPE_FRAME_STATE};
    xrWaitFrame(session_, &wi, &frame_state_);
    predicted_display_time_ = frame_state_.predictedDisplayTime;

    XrFrameBeginInfo bi{XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(session_, &bi);
    frame_in_flight_ = true;

    // Locate views for this frame so the compositor knows where to render.
    XrViewLocateInfo vli{XR_TYPE_VIEW_LOCATE_INFO};
    vli.viewConfigurationType = kViewConfig;
    vli.displayTime = predicted_display_time_;
    vli.space = stage_space_;
    XrViewState vs{XR_TYPE_VIEW_STATE};
    uint32_t out_count = 0;
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    xrLocateViews(session_, &vli, &vs, 2, &out_count, views);
    for (int i = 0; i < 2; ++i) {
        last_views_[i].pose = views[i].pose;
        last_views_[i].fov = views[i].fov;
    }

    // Why: we also locate views against view_space so the compositor can
    // submit a head-locked projection layer (out.space = view_space).
    // In view space the per-eye pose collapses to the IPD offset (and any
    // eye-convergence rotation), with no head rotation/translation — the
    // runtime then composites the layer rigidly attached to the head, so
    // fast rotations never reveal the empty world while the remote frame
    // is still in flight.
    XrViewLocateInfo vli_view{XR_TYPE_VIEW_LOCATE_INFO};
    vli_view.viewConfigurationType = kViewConfig;
    vli_view.displayTime = predicted_display_time_;
    vli_view.space = view_space_;
    XrViewState vs_view{XR_TYPE_VIEW_STATE};
    uint32_t out_count_view = 0;
    XrView views_view[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    xrLocateViews(session_, &vli_view, &vs_view, 2, &out_count_view, views_view);
    for (int i = 0; i < 2; ++i) {
        last_views_view_[i].pose = views_view[i].pose;
        last_views_view_[i].fov = views_view[i].fov;
    }

    // [DRIFT-DIAG #1] How far ahead does the runtime predict, and is the
    // predicted view pose marked valid? A runaway predictedDisplayTime
    // (clock drift / sync issue) makes xrLocateViews extrapolate IMU forward
    // by huge dt, so the projection-layer pose we submit ends up at a stale
    // world-space position even though the head is still — exactly the
    // symptom of "the plane drifts in stage_space, then snaps back".
    {
        static uint64_t last_log_ns = 0;
        const uint64_t t_now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        if (t_now_ns - last_log_ns > 1'000'000'000ull) {
            last_log_ns = t_now_ns;
            const int64_t lookahead_ns =
                (int64_t)predicted_display_time_ - (int64_t)t_now_ns;
            __android_log_print(ANDROID_LOG_INFO, "fuvr.drift",
                "[DRIFT #1] predictedDisplayTime lookahead=%.2fms "
                "viewStateFlags=0x%x posValid=%d ornValid=%d "
                "eye0.pos=(%.3f,%.3f,%.3f)",
                lookahead_ns * 1e-6,
                (unsigned)vs.viewStateFlags,
                (vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) ? 1 : 0,
                (vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) ? 1 : 0,
                last_views_[0].pose.position.x,
                last_views_[0].pose.position.y,
                last_views_[0].pose.position.z);
        }
    }
}

void OpenXrSession::end_frame(Compositor& compositor) {
    if (!frame_in_flight_) return;
    frame_in_flight_ = false;

    // Why head-locked quads, not a projection layer:
    // Meta Quest's compositor ignores `space = view_space` on
    // XrCompositionLayerProjection — the layer ends up world-locked, producing
    // the "plane lags behind the gaze, then snaps back into place" symptom
    // during head rotation. Quad layers DO honor view_space, so we submit two
    // XrCompositionLayerQuads (eyeVisibility LEFT/RIGHT) anchored to the
    // headset's view space. The OS compositor then re-anchors them to the
    // latest head pose at scan-out, driven by the headset's native tracking,
    // fully decoupled from network/decoder latency. See
    // Compositor::build_head_locked_quads() in compositor.cpp for the
    // per-eye geometry derivation.
    std::array<XrCompositionLayerQuad, 2> head_quads{};
    XrCompositionLayerQuad placeholder{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[2] = {nullptr, nullptr};
    uint32_t layer_count = 0;

    if (frame_state_.shouldRender == XR_TRUE) {
        if (compositor.build_head_locked_quads(*this, head_quads)) {
            layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&head_quads[0]);
            layers[1] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&head_quads[1]);
            layer_count = 2;
        } else if (compositor.build_placeholder_layer(*this, placeholder)) {
            layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&placeholder);
            layer_count = 1;
        }
    }

    XrFrameEndInfo ei{XR_TYPE_FRAME_END_INFO};
    ei.displayTime = predicted_display_time_;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = layer_count;
    ei.layers = layers;
    xrEndFrame(session_, &ei);
}

void OpenXrSession::destroy() {
    if (action_set_) xrDestroyActionSet(action_set_);
    for (auto& s : hand_spaces_) if (s) xrDestroySpace(s);
    if (view_space_) xrDestroySpace(view_space_);
    if (stage_space_ && stage_space_ != local_space_) xrDestroySpace(stage_space_);
    if (local_space_) xrDestroySpace(local_space_);
    if (session_) xrDestroySession(session_);
    if (instance_) xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
}

}
