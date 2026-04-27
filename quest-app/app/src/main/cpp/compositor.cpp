// SPDX-License-Identifier: Apache-2.0

#include "compositor.hpp"

#include "decoder_pipeline.hpp"
#include "openxr_session.hpp"

#include <android/hardware_buffer.h>
#include <android/log.h>
#include <GLES2/gl2ext.h>

#include <algorithm>

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

    if (current_image_ != EGL_NO_IMAGE_KHR) eglDestroyImageKHR_(egl_display_, current_image_);
    if (current_texture_ == 0) glGenTextures(1, &current_texture_);

    glBindTexture(GL_TEXTURE_EXTERNAL_OES, current_texture_);
    glEGLImageTargetTexture2DOES_(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)image);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    current_image_ = image;
    return current_texture_;
}

void Compositor::submit_frame(const DecodedFrame& frame) {
    if (!frame.buffer) { has_frame_ = false; return; }
    if (current_buffer_) AHardwareBuffer_release(current_buffer_);
    current_buffer_ = frame.buffer;
    AHardwareBuffer_acquire(current_buffer_);
    upload_hardware_buffer(current_buffer_);
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
    blit_.blit(current_texture_, eye_index);

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
    if (current_image_ != EGL_NO_IMAGE_KHR && eglDestroyImageKHR_)
        eglDestroyImageKHR_(egl_display_, current_image_);
    if (current_texture_) glDeleteTextures(1, &current_texture_);
    if (current_buffer_) AHardwareBuffer_release(current_buffer_);
    if (egl_context_ != EGL_NO_CONTEXT) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(egl_display_, egl_context_);
    }
    if (egl_surface_ != EGL_NO_SURFACE) eglDestroySurface(egl_display_, egl_surface_);
    if (egl_display_ != EGL_NO_DISPLAY) eglTerminate(egl_display_);
}

}
