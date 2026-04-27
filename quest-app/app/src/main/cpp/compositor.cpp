// SPDX-License-Identifier: Apache-2.0

#include "compositor.hpp"

#include "decoder_pipeline.hpp"
#include "openxr_session.hpp"
#include "quat_math.hpp"

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <chrono>
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

// Quaternion helpers live in quat_math.hpp so the host-side ATW math test can
// reuse the same Quat / quat_mul / quat_conjugate / quat_to_mat3_rowmajor /
// qdot / qneg implementations the runtime uses. Do not redefine them here.

uint64_t now_ns_steady() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
    const char* dbg = std::getenv("FUVR_QUEST_DEBUG");
    debug_atw_ = (dbg && dbg[0] && dbg[0] != '0');
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

    for (int eye = 0; eye < 2; ++eye) {
        auto& e = eyes_[eye];
        e.width  = (int32_t)vc[eye].recommendedImageRectWidth;
        e.height = (int32_t)vc[eye].recommendedImageRectHeight;
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

    // --- Build per-eye warp params. ---------------------------------------
    EyeBlit::WarpParams warp{};
    const auto& views_now = xr_.last_views();
    const PlainViewState& rv = (eye_index == 0) ? rendered_left_ : rendered_right_;
    const auto& nv = views_now[eye_index];

    // Now-fov (xrLocateViews this frame). Always trusted.
    warp.fov_now.angleLeft  = nv.fov.angleLeft;
    warp.fov_now.angleRight = nv.fov.angleRight;
    warp.fov_now.angleUp    = nv.fov.angleUp;
    warp.fov_now.angleDown  = nv.fov.angleDown;

    // Render-fov: the daemon currently doesn't ship a real fov in the wire
    // header (TODO: plumb through SubmitFrameRequest). Until it does, the
    // safest assumption is that the Mac rendered with the same per-eye fov
    // the headset reported on its last upstream pose sample, which is the
    // current OpenXR fov. The shader is robust to drift here — using the
    // now-fov gives a slightly under-corrected warp, never an over-correction.
    bool have_render_fov = (rv.fov.angleLeft != 0.0f || rv.fov.angleRight != 0.0f);
    warp.fov_render = have_render_fov
        ? EyeBlit::Fov{rv.fov.angleLeft, rv.fov.angleRight, rv.fov.angleUp, rv.fov.angleDown}
        : warp.fov_now;

    // The shader needs R_delta_inv: a rotation that maps a ray direction
    // expressed in the present-time eye frame into the render-time eye frame.
    // Vectors transform between frames by their *inverse* world rotations:
    //   v_world  = R(q_eye) * v_eye
    //   v_render = R(q_render⁻¹) * R(q_now) * v_now
    //            = R(q_render⁻¹ · q_now) * v_now
    // so R_delta_inv corresponds to the quaternion (q_render⁻¹ · q_now).
    // If either pose is invalid (still defaulted), fall back to identity so
    // the blit still produces a sensible image.
    Quat q_now { nv.pose.orientation.x, nv.pose.orientation.y,
                 nv.pose.orientation.z, nv.pose.orientation.w };
    Quat q_ren { rv.pose.ox, rv.pose.oy, rv.pose.oz, rv.pose.ow };
    bool render_pose_valid = (q_ren.x != 0.0f || q_ren.y != 0.0f ||
                              q_ren.z != 0.0f || q_ren.w != 1.0f);

    // QUAT-FIX: q_now arrives raw from xrLocateViews (Meta makes no continuity
    // guarantee across the q vs -q double cover) while q_ren came over the
    // wire from the Mac predictor (which canonicalizes against its own
    // history). The two sign threads can drift, and a single antipodal frame
    // produces a Δq through the long way ~360° → instant ATW snap. Pin both
    // onto the same sheet as the previous frame's canonical values, then
    // recompute Δq.
    // qdot / qneg now live in quat_math.hpp.
    float* prev_now = prev_q_now_[eye_index];
    float* prev_ren = prev_q_ren_[eye_index];
    if (qdot(prev_now, q_now) < 0.0f) qneg(q_now);
    if (render_pose_valid && qdot(prev_ren, q_ren) < 0.0f) qneg(q_ren);
    // Additionally make q_ren live on the same sheet as q_now so the
    // conjugate-product never crosses the cover.
    if (render_pose_valid) {
        const float d = q_ren.x*q_now.x + q_ren.y*q_now.y +
                        q_ren.z*q_now.z + q_ren.w*q_now.w;
        if (d < 0.0f) qneg(q_ren);
    }

    // Compute frame-to-frame and pair(ren,now) BEFORE updating prev_*, then
    // store extremes (min dot = max angular delta) over the 1Hz window so the
    // log captures motion peaks instead of whatever sample the throttle picks.
    const float d_now_frame = qdot(prev_q_now_[eye_index], q_now);
    const float d_ren_frame = qdot(prev_q_ren_[eye_index], q_ren);
    const float d_pair_frame = q_ren.x*q_now.x + q_ren.y*q_now.y +
                                q_ren.z*q_now.z + q_ren.w*q_now.w;

    prev_q_now_[eye_index][0] = q_now.x; prev_q_now_[eye_index][1] = q_now.y;
    prev_q_now_[eye_index][2] = q_now.z; prev_q_now_[eye_index][3] = q_now.w;
    if (render_pose_valid) {
        prev_q_ren_[eye_index][0] = q_ren.x; prev_q_ren_[eye_index][1] = q_ren.y;
        prev_q_ren_[eye_index][2] = q_ren.z; prev_q_ren_[eye_index][3] = q_ren.w;
    }

    if (eye_index == 0) {
        // Track minimum dot (= maximum angular delta) within the 1Hz window
        // for each metric so we don't miss motion peaks. Reset on log emit.
        if (d_now_frame  < min_d_now_window_)  min_d_now_window_  = d_now_frame;
        if (d_ren_frame  < min_d_ren_window_)  min_d_ren_window_  = d_ren_frame;
        if (d_pair_frame < min_d_pair_window_) min_d_pair_window_ = d_pair_frame;
        const uint64_t t = now_ns_steady();
        if (t - last_quat_dbg_ns_ > 1'000'000'000ull) {
            last_quat_dbg_ns_ = t;
            // Convert min-dot to peak angle (degrees) so sub-degree motion is
            // still readable. angle = 2 * acos(|dot|).
            auto dot_to_deg = [](float d) {
                if (d > 1.0f) d = 1.0f; if (d < -1.0f) d = -1.0f;
                return 2.0f * std::acos(std::fabs(d)) * 57.2957795f;
            };
            LOGI("[QUAT-DEBUG] window peak: dq_now=%.3f° dq_ren=%.3f° "
                 "Δq_pair=%.3f° (min_dot pair=%.6f) valid=%d",
                 dot_to_deg(min_d_now_window_),
                 dot_to_deg(min_d_ren_window_),
                 dot_to_deg(min_d_pair_window_),
                 min_d_pair_window_,
                 (int)render_pose_valid);
            min_d_now_window_  = 1.0f;
            min_d_ren_window_  = 1.0f;
            min_d_pair_window_ = 1.0f;
        }
    }

    if (render_pose_valid) {
        Quat dq = quat_mul(quat_conjugate(quat_normalize(q_ren)),
                           quat_normalize(q_now));
        quat_to_mat3_rowmajor(dq, warp.r_delta_inv);
    } else {
        // Identity row-major.
        warp.r_delta_inv[0] = warp.r_delta_inv[4] = warp.r_delta_inv[8] = 1.0f;
        warp.r_delta_inv[1] = warp.r_delta_inv[2] = warp.r_delta_inv[3] = 0.0f;
        warp.r_delta_inv[5] = warp.r_delta_inv[6] = warp.r_delta_inv[7] = 0.0f;
    }

    if (debug_atw_ && eye_index == 0) {
        const uint64_t t = now_ns_steady();
        if (t - last_debug_log_ns_ > 1'000'000'000ull) {
            last_debug_log_ns_ = t;
            // Δq norm relative to identity == |sin(theta/2)| ≈ theta/2 (rad)
            // for small angles. Useful sanity number to confirm the warp is
            // actually responsive to head motion.
            Quat dq_dbg = quat_mul(quat_conjugate(quat_normalize(q_ren)),
                                   quat_normalize(q_now));
            const float vec_norm = std::sqrt(dq_dbg.x*dq_dbg.x +
                                             dq_dbg.y*dq_dbg.y +
                                             dq_dbg.z*dq_dbg.z);
            LOGI("ATW eye0 fov_now(L=%.3f R=%.3f U=%.3f D=%.3f) "
                 "fov_render_present=%d |sin(theta/2)|=%.4f render_pose_valid=%d",
                 warp.fov_now.angleLeft, warp.fov_now.angleRight,
                 warp.fov_now.angleUp, warp.fov_now.angleDown,
                 (int)have_render_fov, vec_norm, (int)render_pose_valid);
        }
    }

    blit_.blit(current_texture_, eye_index, warp);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

    XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    return xrReleaseSwapchainImage(e.handle, &ri) == XR_SUCCESS;
}

bool Compositor::build_projection_layer(OpenXrSession& xr,
                                        XrCompositionLayerProjection& out,
                                        std::array<XrCompositionLayerProjectionView, 2>& views) {
    if (!has_frame_ || eyes_[0].handle == XR_NULL_HANDLE || eyes_[1].handle == XR_NULL_HANDLE)
        return false;

    if (!render_eye(0) || !render_eye(1)) return false;

    const auto& snap = xr.last_views();
    for (int i = 0; i < 2; ++i) {
        views[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        views[i].pose = snap[i].pose;
        views[i].fov = snap[i].fov;
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
