// SPDX-License-Identifier: Apache-2.0

#include "openxr_session.hpp"
#include "compositor.hpp"

#include <android/log.h>
#include <android_native_app_glue.h>
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
    if (!create_instance()) return false;
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

bool OpenXrSession::create_instance() {
    const char* exts[] = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
        XR_FB_COLOR_SPACE_EXTENSION_NAME,
        XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,
    };

    XrInstanceCreateInfoAndroidKHR android_info{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};

    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    info.next = &android_info;
    info.enabledExtensionCount = sizeof(exts) / sizeof(exts[0]);
    info.enabledExtensionNames = exts;
    std::strcpy(info.applicationInfo.applicationName, "FuVR Quest");
    info.applicationInfo.applicationVersion = 1;
    std::strcpy(info.applicationInfo.engineName, "FuVR");
    info.applicationInfo.engineVersion = 1;
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;

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

    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
    if (!xr_check(xrCreateReferenceSpace(session_, &ref, &stage_space_), "stage")) {
        ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        if (!xr_check(xrCreateReferenceSpace(session_, &ref, &stage_space_), "local")) return false;
    }
    ref.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    return xr_check(xrCreateReferenceSpace(session_, &ref, &view_space_), "view");
}

bool OpenXrSession::create_action_set() {
    XrActionSetCreateInfo as{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strcpy(as.actionSetName, "fuvr_main");
    std::strcpy(as.localizedActionSetName, "FuVR Main");
    as.priority = 0;
    if (!xr_check(xrCreateActionSet(instance_, &as, &action_set_), "xrCreateActionSet")) return false;

    XrPath hand_paths[2];
    xrStringToPath(instance_, "/user/hand/left", &hand_paths[0]);
    xrStringToPath(instance_, "/user/hand/right", &hand_paths[1]);

    auto make_action = [&](const char* name, const char* loc, XrActionType type, XrAction* out) {
        XrActionCreateInfo ai{XR_TYPE_ACTION_CREATE_INFO};
        std::strcpy(ai.actionName, name);
        std::strcpy(ai.localizedActionName, loc);
        ai.actionType = type;
        ai.countSubactionPaths = 2;
        ai.subactionPaths = hand_paths;
        return xr_check(xrCreateAction(action_set_, &ai, out), name);
    };

    make_action("hand_pose", "Hand Pose", XR_ACTION_TYPE_POSE_INPUT, &pose_action_);
    make_action("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, &trigger_action_);
    make_action("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, &grip_action_);
    make_action("thumbstick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, &thumbstick_action_);
    make_action("button_a", "Button A/X", XR_ACTION_TYPE_BOOLEAN_INPUT, &button_a_action_);
    make_action("button_b", "Button B/Y", XR_ACTION_TYPE_BOOLEAN_INPUT, &button_b_action_);
    make_action("haptic", "Haptic", XR_ACTION_TYPE_VIBRATION_OUTPUT, &haptic_action_);

    XrPath profile_path;
    xrStringToPath(instance_, "/interaction_profiles/oculus/touch_plus_controller", &profile_path);

    auto p = [&](const char* s) { XrPath o; xrStringToPath(instance_, s, &o); return o; };
    XrActionSuggestedBinding bindings[] = {
        {pose_action_,       p("/user/hand/left/input/grip/pose")},
        {pose_action_,       p("/user/hand/right/input/grip/pose")},
        {trigger_action_,    p("/user/hand/left/input/trigger/value")},
        {trigger_action_,    p("/user/hand/right/input/trigger/value")},
        {grip_action_,       p("/user/hand/left/input/squeeze/value")},
        {grip_action_,       p("/user/hand/right/input/squeeze/value")},
        {thumbstick_action_, p("/user/hand/left/input/thumbstick")},
        {thumbstick_action_, p("/user/hand/right/input/thumbstick")},
        {button_a_action_,   p("/user/hand/left/input/x/click")},
        {button_a_action_,   p("/user/hand/right/input/a/click")},
        {button_b_action_,   p("/user/hand/left/input/y/click")},
        {button_b_action_,   p("/user/hand/right/input/b/click")},
        {haptic_action_,     p("/user/hand/left/output/haptic")},
        {haptic_action_,     p("/user/hand/right/output/haptic")},
    };
    XrInteractionProfileSuggestedBinding suggest{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggest.interactionProfile = profile_path;
    suggest.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);
    suggest.suggestedBindings = bindings;
    xr_check(xrSuggestInteractionProfileBindings(instance_, &suggest), "xrSuggestInteractionProfileBindings");

    for (int hand = 0; hand < 2; ++hand) {
        XrActionSpaceCreateInfo sci{XR_TYPE_ACTION_SPACE_CREATE_INFO};
        sci.action = pose_action_;
        sci.subactionPath = hand_paths[hand];
        sci.poseInActionSpace.orientation.w = 1.0f;
        xr_check(xrCreateActionSpace(session_, &sci, &hand_spaces_[hand]), "xrCreateActionSpace");
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &action_set_;
    return xr_check(xrAttachSessionActionSets(session_, &attach), "xrAttachSessionActionSets");
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

    if (running_) {
        const XrActiveActionSet active{action_set_, XR_NULL_PATH};
        XrActionsSyncInfo si{XR_TYPE_ACTIONS_SYNC_INFO};
        si.countActiveActionSets = 1;
        si.activeActionSets = &active;
        xrSyncActions(session_, &si);
    }
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
}

void OpenXrSession::end_frame(Compositor& compositor) {
    if (!frame_in_flight_) return;
    frame_in_flight_ = false;

    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad placeholder{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const XrCompositionLayerBaseHeader* layers[1] = {nullptr};
    uint32_t layer_count = 0;

    if (frame_state_.shouldRender == XR_TRUE) {
        if (compositor.build_projection_layer(*this, projection, projection_views_)) {
            layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader*>(&projection);
            layer_count = 1;
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
    if (stage_space_) xrDestroySpace(stage_space_);
    if (session_) xrDestroySession(session_);
    if (instance_) xrDestroyInstance(instance_);
    instance_ = XR_NULL_HANDLE;
    session_ = XR_NULL_HANDLE;
}

}
