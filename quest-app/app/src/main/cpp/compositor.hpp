// SPDX-License-Identifier: Apache-2.0
#pragma once

#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_OPENGL_ES 1

#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <array>
#include <cstdint>
#include <vector>

#include "eye_blit.hpp"
#include "proto_codec.hpp"

namespace fuvr {

class OpenXrSession;
struct DecodedFrame;

struct EyeSwapchain {
    XrSwapchain handle{XR_NULL_HANDLE};
    int32_t width{0};
    int32_t height{0};
    int64_t format{0};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> framebuffers;
};

class Compositor {
public:
    explicit Compositor(OpenXrSession& xr) : xr_(xr) {}

    bool init();
    void shutdown();

    // Bind decoded AHardwareBuffer as GL texture for the next frame.
    void submit_frame(const DecodedFrame& frame);

    // Acquire/render/release per-eye swapchain images and fill projection
    // views referencing them. Returns false if no decoded frame is ready.
    bool build_projection_layer(OpenXrSession& xr,
                                XrCompositionLayerProjection& out,
                                std::array<XrCompositionLayerProjectionView, 2>& views);

    // Black "Connecting..." quad shown until first frame arrives.
    bool build_placeholder_layer(OpenXrSession& xr, XrCompositionLayerQuad& out);

private:
    bool create_egl_context();
    bool create_swapchains();
    bool render_eye(int eye_index);
    GLuint upload_hardware_buffer(AHardwareBuffer* buf);

    OpenXrSession& xr_;

    EGLDisplay egl_display_{EGL_NO_DISPLAY};
    EGLContext egl_context_{EGL_NO_CONTEXT};
    EGLSurface egl_surface_{EGL_NO_SURFACE};
    EGLConfig egl_config_{nullptr};

    std::array<EyeSwapchain, 2> eyes_{};
    XrSwapchain placeholder_swapchain_{XR_NULL_HANDLE};

    GLuint current_texture_{0};
    bool has_frame_{false};

    // Pose used by the Mac to render the currently-bound texture. Compositor
    // pairs this with xrLocateViews now-pose to compute the ATW Δq.
    PlainViewState rendered_left_{};
    PlainViewState rendered_right_{};

    // Throttled debug logging (FUVR_QUEST_DEBUG).
    bool debug_atw_{false};
    uint64_t last_debug_log_ns_{0};

    // QUAT-FIX: previous-frame canonical orientations, kept per eye, used to
    // pin q_now and q_render onto the same sign sheet across the antipodal
    // double cover. Initialized to identity (w=1) so first-frame dot >= 0.
    float prev_q_now_[2][4]{{0,0,0,1},{0,0,0,1}};
    float prev_q_ren_[2][4]{{0,0,0,1},{0,0,0,1}};
    uint64_t last_quat_dbg_ns_{0};
    // Per-1Hz-window minimum dot products (= maximum angular deltas). Reset
    // each time the QUAT-DEBUG line fires so we capture motion peaks instead
    // of whatever single frame the throttle happens to hit.
    float min_d_now_window_{1.0f};
    float min_d_ren_window_{1.0f};
    float min_d_pair_window_{1.0f};

    EyeBlit blit_;
};

}
