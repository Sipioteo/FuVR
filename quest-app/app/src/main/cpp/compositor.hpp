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

    EyeBlit blit_;
};

}
