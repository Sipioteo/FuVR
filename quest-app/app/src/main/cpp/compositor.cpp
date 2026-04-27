// SPDX-License-Identifier: Apache-2.0

#include "compositor.hpp"

#include "decoder_pipeline.hpp"
#include "openxr_session.hpp"

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "fuvr.comp", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.comp", __VA_ARGS__)

namespace fuvr {

namespace {
PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID_ = nullptr;
PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_ = nullptr;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_ = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_ = nullptr;

void load_gl_extensions() {
    if (eglCreateImageKHR_) return;
    eglGetNativeClientBufferANDROID_ = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC)
        eglGetProcAddress("eglGetNativeClientBufferANDROID");
    eglCreateImageKHR_ = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR_ = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    glEGLImageTargetTexture2DOES_ = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
        eglGetProcAddress("glEGLImageTargetTexture2DOES");
}
}

bool Compositor::init() {
    if (!create_egl_context()) return false;
    load_gl_extensions();
    if (!blit_.init()) {
        LOGE("EyeBlit init failed");
        return false;
    }
    return create_swapchains();
}

bool Compositor::create_egl_context() {
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(egl_display_, nullptr, nullptr)) return false;

    EGLint cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0,
        EGL_NONE
    };
    EGLint num = 0;
    eglChooseConfig(egl_display_, cfg_attrs, &egl_config_, 1, &num);
    if (num < 1) return false;

    EGLint ctx_attrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, ctx_attrs);
    if (egl_context_ == EGL_NO_CONTEXT) return false;

    EGLint pbuf_attrs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    egl_surface_ = eglCreatePbufferSurface(egl_display_, egl_config_, pbuf_attrs);
    eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_);
    return true;
}

bool Compositor::create_swapchains() {
    XrSession session = xr_.session();
    if (session == XR_NULL_HANDLE) return false;

    const auto& vc = xr_.view_configs();
    if (vc.size() < 2) {
        LOGE("expected 2 view configs, got %zu", vc.size());
        return false;
    }

    uint32_t fmt_count = 0;
    xrEnumerateSwapchainFormats(session, 0, &fmt_count, nullptr);
    std::vector<int64_t> formats(fmt_count);
    xrEnumerateSwapchainFormats(session, fmt_count, &fmt_count, formats.data());

    auto pick_format = [&]() -> int64_t {
        for (int64_t want : {(int64_t)GL_SRGB8_ALPHA8, (int64_t)GL_RGBA8}) {
            if (std::find(formats.begin(), formats.end(), want) != formats.end()) return want;
        }
        return formats.empty() ? (int64_t)GL_RGBA8 : formats.front();
    };
    const int64_t color_format = pick_format();

    // Why: the Mac runtime renders into a per-eye buffer of fixed dimensions
    // (currently 2064x2208 — see runtime-macos/src/session.cpp where
    // StartSessionParams.perEyeWidth/Height are pinned). The decoded SBS
    // texture's per-eye half therefore has aspect 2064:2208 (≈0.935). If the
    // Quest swapchain dims here use the OS-recommended values (≈1680x1760
    // on Quest 3, aspect 0.955), the blit_ fragment shader stretches a
    // frustum-correct image non-uniformly into the swapchain, and the OS
    // compositor (which only sees `views[i].fov` + the swapchain) interprets
    // the stretched content as if it had been rendered at that fov — which
    // is exactly the geometric warping that looks like "FOV mismatch" during
    // head motion.
    //
    // Fix: size the swapchain to match the Mac-side per-eye dims. The dims
    // are negotiated via the SessionConfig handshake (see protocol_router
    // ControlKind::HelloFromMac) but that arrives *after* compositor init,
    // so we pin to the same default the Mac currently hardcodes. If the
    // OS-recommended dims diverge by more than a small margin, log a
    // warning so a future Mac runtime change is caught.
    constexpr int32_t kMacPerEyeWidth  = 2064;
    constexpr int32_t kMacPerEyeHeight = 2208;
    for (int eye = 0; eye < 2; ++eye) {
        auto& e = eyes_[eye];
        const int32_t recW = (int32_t)vc[eye].recommendedImageRectWidth;
        const int32_t recH = (int32_t)vc[eye].recommendedImageRectHeight;
        const float recAspect = (recH > 0) ? (float)recW / (float)recH : 0.0f;
        const float macAspect = (float)kMacPerEyeWidth / (float)kMacPerEyeHeight;
        if (std::abs(recAspect - macAspect) > 0.01f) {
            LOGI("eye %d: OS-recommended swapchain %dx%d (aspect %.3f) "
                 "differs from Mac per-eye %dx%d (aspect %.3f); using Mac "
                 "dims to keep blit identity-stretch-free",
                 eye, recW, recH, recAspect,
                 kMacPerEyeWidth, kMacPerEyeHeight, macAspect);
        }
        e.width  = kMacPerEyeWidth;
        e.height = kMacPerEyeHeight;
        e.format = color_format;

        XrSwapchainCreateInfo sci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        sci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format = color_format;
        sci.sampleCount = 1;
        sci.width = e.width;
        sci.height = e.height;
        sci.faceCount = 1;
        sci.arraySize = 1;
        sci.mipCount = 1;
        if (xrCreateSwapchain(session, &sci, &e.handle) != XR_SUCCESS) {
            LOGE("xrCreateSwapchain eye=%d failed", eye);
            return false;
        }

        uint32_t img_count = 0;
        xrEnumerateSwapchainImages(e.handle, 0, &img_count, nullptr);
        e.images.assign(img_count, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(e.handle, img_count, &img_count,
            (XrSwapchainImageBaseHeader*)e.images.data());

        e.framebuffers.assign(img_count, 0);
        glGenFramebuffers((GLsizei)img_count, e.framebuffers.data());
        for (uint32_t i = 0; i < img_count; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, e.framebuffers[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, e.images[i].image, 0);
            GLenum st = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            if (st != GL_FRAMEBUFFER_COMPLETE) {
                LOGE("eye %d FBO %u incomplete: 0x%x", eye, i, st);
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        LOGI("eye %d swapchain %dx%d images=%u format=0x%x",
             eye, e.width, e.height, img_count, (uint32_t)color_format);
    }
    return true;
}

GLuint Compositor::upload_hardware_buffer(AHardwareBuffer* buf) {
    if (!buf || !eglGetNativeClientBufferANDROID_) return 0;
    EGLClientBuffer client = eglGetNativeClientBufferANDROID_(buf);
    if (!client) return 0;
    const EGLint img_attrs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE };
    EGLImageKHR image = eglCreateImageKHR_(egl_display_, EGL_NO_CONTEXT,
                                           EGL_NATIVE_BUFFER_ANDROID, client, img_attrs);
    if (image == EGL_NO_IMAGE_KHR) return 0;

    if (current_texture_ == 0) glGenTextures(1, &current_texture_);

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, current_texture_);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)image);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Why: glEGLImageTargetTexture2DOES makes the GL texture reference the
    // underlying gralloc buffer directly, so the EGLImage handle itself can
    // be destroyed immediately — the binding (and thus the AHardwareBuffer)
    // stays alive as long as the texture exists.
    eglDestroyImageKHR_(egl_display_, image);
    return current_texture_;
}

void Compositor::submit_frame(const DecodedFrame& frame) {
    // Why: the main loop ticks at compositor vsync (90 Hz) and pops the
    // decoder every iteration; if the decoder hasn't published a fresh frame
    // since the previous pop the buffer is null. Clearing has_frame_ here
    // produced the alternating black-frame flicker. Keep the previously
    // bound texture on the GL side and just skip — the decoder's drop-old
    // replacement still runs, so we never starve nor lag behind by more
    // than one decode interval.
    if (!frame.buffer) return;
    upload_hardware_buffer(frame.buffer);
    // Why: the texture/EGLImage path keeps its own reference to the gralloc
    // pages, so we drop the ref the decoder handed us as soon as the bind
    // completes; otherwise we'd hold every frame buffer indefinitely and
    // exhaust the AImageReader's max-images pool within seconds.
    AHardwareBuffer_release(frame.buffer);
    rendered_left_ = frame.rendered_left;
    rendered_right_ = frame.rendered_right;
    has_frame_ = true;
}

bool Compositor::render_eye(int eye_index) {
    auto& e = eyes_[eye_index];
    if (e.handle == XR_NULL_HANDLE) return false;

    uint32_t image_index = 0;
    XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (xrAcquireSwapchainImage(e.handle, &ai, &image_index) != XR_SUCCESS) return false;

    XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wi.timeout = XR_INFINITE_DURATION;
    if (xrWaitSwapchainImage(e.handle, &wi) != XR_SUCCESS) return false;

    GLint prev_fbo = 0, prev_viewport[4] = {0};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, e.framebuffers[image_index]);
    glViewport(0, 0, e.width, e.height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Plain texture copy. The OS compositor performs scan-out timewarp
    // against (rendered_pose, rendered_fov) submitted on the projection
    // layer, so the shader must NOT apply any rotation here — doing so
    // would double-warp. The blit just splits the SBS source 50/50 into
    // each eye's swapchain. EyeBlit's identity-warp path handles this:
    // r_delta_inv = I, fov_now == fov_render, ur=u and vr=v.
    EyeBlit::WarpParams warp{};
    warp.r_delta_inv[0] = warp.r_delta_inv[4] = warp.r_delta_inv[8] = 1.0f;
    warp.r_delta_inv[1] = warp.r_delta_inv[2] = warp.r_delta_inv[3] = 0.0f;
    warp.r_delta_inv[5] = warp.r_delta_inv[6] = warp.r_delta_inv[7] = 0.0f;
    // Pick any consistent fov — with R_delta_inv = I and fov_now == fov_render
    // the math reduces to ur = (vNdc.x+1)/2, vr = (vNdc.y+1)/2 regardless of
    // the actual angle values. Use a neutral non-zero value to avoid
    // tan(0)=0 division degeneracies at the shader edges.
    const EyeBlit::Fov neutral{ -1.0f, 1.0f, 1.0f, -1.0f };
    warp.fov_now = neutral;
    warp.fov_render = neutral;

    blit_.blit(current_texture_, eye_index, warp);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return xrReleaseSwapchainImage(e.handle, &ri) == XR_SUCCESS;
}

bool Compositor::build_projection_layer(OpenXrSession& xr,
                                        XrCompositionLayerProjection& out,
                                        std::array<XrCompositionLayerProjectionView, 2>& views) {
    // Why: stereo XrCompositionLayerProjection in stage_space, with each
    // eye's pose+fov set to the render-time values shipped by the Mac in
    // VideoFragmentHeader. This is what tells Meta's compositor "this
    // swapchain was rasterized FROM this eye-pose with this fov" — it then
    // performs per-pixel scan-out timewarp against the present-time head
    // pose. Mac renders with a +12° margin on every side (see
    // runtime-macos/src/session.cpp); that margin is the OS's reprojection
    // headroom for fast head rotation between render and display.
    //
    // We deliberately do NOT gate on has_frame_: render_eye reuses the last
    // bound texture so the layer keeps presenting smoothly between Mac
    // frames. Submitting consistently each vsync also gives the OS a stable
    // history to time-warp against.
    if (eyes_[0].handle == XR_NULL_HANDLE || eyes_[1].handle == XR_NULL_HANDLE)
        return false;
    if (current_texture_ == 0) return false;

    if (!render_eye(0) || !render_eye(1)) return false;

    const auto& snap = xr.last_views();

    for (int i = 0; i < 2; ++i) {
        const PlainViewState& rv = (i == 0) ? rendered_left_ : rendered_right_;
        // Use the render-time pose if it's valid; otherwise fall back to the
        // current-frame xrLocateViews pose so the very first frames before
        // a Mac frame arrives still place the layer somewhere reasonable.
        const bool render_pose_valid = (rv.pose.ow != 1.0f) || (rv.pose.ox != 0.0f) ||
                                       (rv.pose.oy != 0.0f) || (rv.pose.oz != 0.0f);
        const bool render_fov_valid = (rv.fov.angleLeft != 0.0f) || (rv.fov.angleRight != 0.0f);

        views[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        if (render_pose_valid) {
            views[i].pose.position = { rv.pose.px, rv.pose.py, rv.pose.pz };
            views[i].pose.orientation = { rv.pose.ox, rv.pose.oy, rv.pose.oz, rv.pose.ow };
        } else {
            views[i].pose = snap[i].pose;
        }
        if (render_fov_valid) {
            views[i].fov.angleLeft  = rv.fov.angleLeft;
            views[i].fov.angleRight = rv.fov.angleRight;
            views[i].fov.angleUp    = rv.fov.angleUp;
            views[i].fov.angleDown  = rv.fov.angleDown;
        } else {
            views[i].fov = snap[i].fov;
        }
        views[i].subImage.swapchain = eyes_[i].handle;
        views[i].subImage.imageRect.offset = {0, 0};
        views[i].subImage.imageRect.extent.width = eyes_[i].width;
        views[i].subImage.imageRect.extent.height = eyes_[i].height;
        views[i].subImage.imageArrayIndex = 0;
    }
    out = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    out.space = xr.stage_space();
    out.viewCount = 2;
    out.views = views.data();
    return true;
}

bool Compositor::build_head_locked_quads(OpenXrSession& xr,
                                         std::array<XrCompositionLayerQuad, 2>& quads) {
    // Why: anchoring the per-eye quads in view_space delegates head-locking
    // to the OpenXR runtime — the layer pose is evaluated against view_space
    // (the live head-tracked frame) at scan-out, so the rectangle stays
    // rigidly glued to the lenses regardless of whether new video frames
    // are arriving from the Mac. The texture is blitted 1:1 by render_eye
    // (identity warp); the in-rectangle scene rotates with the head with
    // ~pipeline-latency lag, which the user accepts as the lesser evil
    // versus rectangle drift.
    //
    // Per-eye geometry (matches the asymmetric Quest FOV exactly):
    //   eye_pos_in_view = (±IPD/2, 0, 0)             (xrLocateViews(view_space))
    //   quad_center     = eye_pos + (cx, cy, -depth)
    //   cx              = depth * (tan(R) + tan(L)) / 2
    //   cy              = depth * (tan(U) + tan(D)) / 2
    //   quad_w          = depth * (tan(R) - tan(L))
    //   quad_h          = depth * (tan(U) - tan(D))
    if (eyes_[0].handle == XR_NULL_HANDLE || eyes_[1].handle == XR_NULL_HANDLE)
        return false;
    if (current_texture_ == 0) return false;

    if (!render_eye(0) || !render_eye(1)) return false;

    constexpr float kDepth = 1.0f;
    const auto& snap = xr.last_views();
    const auto& snap_view = xr.last_views_view();

    for (int i = 0; i < 2; ++i) {
        const XrFovf& fov = snap[i].fov;
        const float tanL = std::tan(fov.angleLeft);
        const float tanR = std::tan(fov.angleRight);
        const float tanU = std::tan(fov.angleUp);
        const float tanD = std::tan(fov.angleDown);
        const float w = kDepth * (tanR - tanL);
        const float h = kDepth * (tanU - tanD);
        const float cx = kDepth * (tanR + tanL) * 0.5f;
        const float cy = kDepth * (tanU + tanD) * 0.5f;

        const float eye_x = snap_view[i].pose.position.x;
        const float eye_y = snap_view[i].pose.position.y;
        const float eye_z = snap_view[i].pose.position.z;

        quads[i] = {XR_TYPE_COMPOSITION_LAYER_QUAD};
        quads[i].space = xr.view_space();
        quads[i].eyeVisibility = (i == 0) ? XR_EYE_VISIBILITY_LEFT
                                          : XR_EYE_VISIBILITY_RIGHT;
        quads[i].pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
        quads[i].pose.position = { eye_x + cx, eye_y + cy, eye_z - kDepth };
        quads[i].size = { std::fabs(w), std::fabs(h) };
        quads[i].subImage.swapchain = eyes_[i].handle;
        quads[i].subImage.imageRect.offset = {0, 0};
        quads[i].subImage.imageRect.extent.width  = eyes_[i].width;
        quads[i].subImage.imageRect.extent.height = eyes_[i].height;
        quads[i].subImage.imageArrayIndex = 0;
    }
    return true;
}

bool Compositor::build_placeholder_layer(OpenXrSession& xr, XrCompositionLayerQuad& out) {
    if (placeholder_swapchain_ == XR_NULL_HANDLE) return false;
    out = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    out.space = xr.stage_space();
    out.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    out.subImage.swapchain = placeholder_swapchain_;
    out.pose.orientation.w = 1.0f;
    out.pose.position = {0.0f, 0.0f, -2.0f};
    out.size = {1.6f, 0.9f};
    return true;
}

void Compositor::shutdown() {
    blit_.shutdown();
    for (auto& e : eyes_) {
        if (!e.framebuffers.empty()) {
            glDeleteFramebuffers((GLsizei)e.framebuffers.size(), e.framebuffers.data());
            e.framebuffers.clear();
        }
        if (e.handle) { xrDestroySwapchain(e.handle); e.handle = XR_NULL_HANDLE; }
    }
    if (current_texture_) glDeleteTextures(1, &current_texture_);
    if (egl_context_ != EGL_NO_CONTEXT) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_display_, egl_context_);
    }
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_display_, egl_surface_);
    if (egl_display_ != EGL_NO_DISPLAY) eglTerminate(egl_display_);
}

}
