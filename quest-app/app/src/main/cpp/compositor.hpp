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

    // Acquire/render/release per-eye swapchain images and fill a stereo
    // projection layer in stage_space. Kept as a fallback path / future
    // option; the active end_frame submits head-locked quads instead.
    bool build_projection_layer(OpenXrSession& xr,
                                XrCompositionLayerProjection& out,
                                std::array<XrCompositionLayerProjectionView, 2>& views);

    // Head-locked stereo dual-quad path. Two XrCompositionLayerQuad layers
    // anchored in view_space, one per eye (eyeVisibility LEFT/RIGHT), sized
    // to each eye's asymmetric FOV at 1m depth. Fills `quads[0]` for the
    // left eye and `quads[1]` for the right; both reference the matching
    // per-eye swapchain rendered by render_eye(). Returns false if
    // swapchains aren't ready or no decoded texture is bound yet.
    bool build_head_locked_quads(OpenXrSession& xr,
                                 std::array<XrCompositionLayerQuad, 2>& quads);

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

    // Pose + fov used by the Mac to render the currently-bound texture.
    // Stamped onto the projection layer so the OS compositor knows where
    // the swapchain was rasterized from and can timewarp at scan-out.
    PlainViewState rendered_left_{};
    PlainViewState rendered_right_{};

    EyeBlit blit_;
};

}
