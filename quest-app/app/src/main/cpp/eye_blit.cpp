// SPDX-License-Identifier: Apache-2.0

#include "eye_blit.hpp"

#include <GLES2/gl2ext.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.blit", __VA_ARGS__)

namespace fuvr {

namespace {

// Vertex shader: emits a fullscreen triangle whose NDC also doubles as the
// "destination clip-space" we later unproject through the present-time fov.
constexpr const char* kVS = R"(#version 300 es
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

// Fragment shader: rotational ATW.
//
//   p_d           = vNdc                          (dest NDC, [-1,1])
//   x_now         = mix(tan(L_n), tan(R_n), (p_d.x+1)/2)
//   y_now         = mix(tan(D_n), tan(U_n), (p_d.y+1)/2)
//   dir_now       = (x_now, y_now, -1)             (camera looks down -Z)
//   dir_render    = R_delta_inv * dir_now          (rotate ray into render-frame)
//   x_r/-z_r,...  = perspective divide
//   p_r           = remap from [tan(L_r),tan(R_r)] -> [-1,1]; same for y
//   uv_src        = ((p_r + 1)/2) * uUvScale + uUvOffset
//
// The "wedge" outside the rendered fov clamps to edge so head-turns past the
// rendered fov degrade to a frozen border rather than wrapping or showing
// noise.
constexpr const char* kFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision highp float;
in vec2 vNdc;
uniform samplerExternalOES uTex;
uniform vec2 uUvOffset;
uniform vec2 uUvScale;
uniform vec4 uFovNow;     // (angleLeft, angleRight, angleUp, angleDown)
uniform vec4 uFovRender;  // same convention
uniform mat3 uRDeltaInv;  // now-eye-space -> render-eye-space
out vec4 oColor;
void main() {
    float tanL_n = tan(uFovNow.x);
    float tanR_n = tan(uFovNow.y);
    float tanU_n = tan(uFovNow.z);
    float tanD_n = tan(uFovNow.w);
    float tanL_r = tan(uFovRender.x);
    float tanR_r = tan(uFovRender.y);
    float tanU_r = tan(uFovRender.z);
    float tanD_r = tan(uFovRender.w);

    float u = (vNdc.x + 1.0) * 0.5;
    float v = (vNdc.y + 1.0) * 0.5;
    vec3 dir_now = vec3(mix(tanL_n, tanR_n, u),
                        mix(tanD_n, tanU_n, v),
                        -1.0);
    vec3 dir_r = uRDeltaInv * dir_now;

    // Behind the camera in the render frame -> nothing valid to sample.
    if (dir_r.z >= 0.0) { oColor = vec4(0.0,0.0,0.0,1.0); return; }

    float xr = dir_r.x / -dir_r.z;
    float yr = dir_r.y / -dir_r.z;

    // Map back to [0,1] in the render fov. When the user rotates faster
    // than the pose-prediction lookahead + overscan can compensate
    // (Δq peak > rendered-fov margin), the reprojected ray exits the
    // rendered viewport. Previously the shader emitted explicit black for
    // those samples — the visual result was a "shrinking polygon" of valid
    // texture wrapped in a growing black wedge during head motion (user
    // perception: "the plane stays still then realigns to my eyes after a
    // moment"). We now CLAMP the UV to [0,1] so the sampler reads the
    // nearest texture edge instead of emitting black. The edge content
    // stretches a bit during fast turns, but the FOV stays fully covered
    // and the apparent "shrinking screen" is gone. This is the standard
    // ATW+overscan fallback for remote-rendered VR streaming.
    float ur = clamp((xr - tanL_r) / (tanR_r - tanL_r), 0.0, 1.0);
    float vr = clamp((yr - tanD_r) / (tanU_r - tanD_r), 0.0, 1.0);

    // Why: the source texture is a GL_TEXTURE_EXTERNAL_OES bound to an
    // AHardwareBuffer that MediaCodec wrote in display-natural orientation,
    // i.e. row 0 is the *top* of the image (texture v=0 -> top). The rest of
    // the warp math is in OpenXR camera space (-Z forward, +Y up), so vr=1
    // corresponds to the top of the destination NDC. Sampling that with v=vr
    // would read the bottom of the source -> the whole image appears upside
    // down AND vertical head rotation appears inverted (pitch-up moves the
    // warp toward vr=1, which fetches the floor pixels). Flip V here so the
    // top of NDC samples row 0 of the source. Horizontal axis already
    // matches: u=0 -> tanL_n (left of view) -> left half of the SBS source.
    vec2 src = vec2(ur, 1.0 - vr) * uUvScale + uUvOffset;
    oColor = texture(uTex, src);
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

}  // namespace

bool EyeBlit::init() {
    GLuint vs = compile(GL_VERTEX_SHADER, kVS);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    loc_uv_offset_   = glGetUniformLocation(program_, "uUvOffset");
    loc_uv_scale_    = glGetUniformLocation(program_, "uUvScale");
    loc_sampler_     = glGetUniformLocation(program_, "uTex");
    loc_fov_now_     = glGetUniformLocation(program_, "uFovNow");
    loc_fov_render_  = glGetUniformLocation(program_, "uFovRender");
    loc_r_delta_inv_ = glGetUniformLocation(program_, "uRDeltaInv");
    glGenVertexArrays(1, &vao_);
    return true;
}

void EyeBlit::blit(GLuint external_texture, int eye_index) {
    WarpParams identity{};
    blit(external_texture, eye_index, identity);
}

void EyeBlit::blit(GLuint external_texture, int eye_index, const WarpParams& warp) {
    if (!program_) return;

    GLint prev_program = 0, prev_vao = 0, prev_active_texture = 0;
    GLint prev_tex_ext = 0;
    GLboolean prev_blend = glIsEnabled(GL_BLEND);
    GLboolean prev_scissor = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prev_cull = glIsEnabled(GL_CULL_FACE);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_texture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &prev_tex_ext);

    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, external_texture);
    glUniform1i(loc_sampler_, 0);

    // SBS layout — STEREO-SPLIT marker, see eye_blit.hpp.
    float uv_off_u = (eye_index == 0) ? 0.0f : 0.5f;
    float uv_off_v = 0.0f;
    float uv_sc_u = 0.5f, uv_sc_v = 1.0f;
    if (warp.override_uv) {
        uv_off_u = warp.uv_offset[0]; uv_off_v = warp.uv_offset[1];
        uv_sc_u  = warp.uv_scale[0];  uv_sc_v  = warp.uv_scale[1];
    }
    glUniform2f(loc_uv_offset_, uv_off_u, uv_off_v);
    glUniform2f(loc_uv_scale_,  uv_sc_u,  uv_sc_v);

    glUniform4f(loc_fov_now_,
                warp.fov_now.angleLeft,  warp.fov_now.angleRight,
                warp.fov_now.angleUp,    warp.fov_now.angleDown);
    glUniform4f(loc_fov_render_,
                warp.fov_render.angleLeft,  warp.fov_render.angleRight,
                warp.fov_render.angleUp,    warp.fov_render.angleDown);

    // GLES expects column-major mat3 with transpose=GL_FALSE; we author
    // r_delta_inv as row-major in C++ for readability and pass transpose=true.
    glUniformMatrix3fv(loc_r_delta_inv_, 1, GL_TRUE, warp.r_delta_inv);

    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Restore.
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, (GLuint)prev_tex_ext);
    glActiveTexture((GLenum)prev_active_texture);
    glBindVertexArray((GLuint)prev_vao);
    glUseProgram((GLuint)prev_program);
    if (prev_blend) glEnable(GL_BLEND);
    if (prev_scissor) glEnable(GL_SCISSOR_TEST);
    if (prev_depth) glEnable(GL_DEPTH_TEST);
    if (prev_cull) glEnable(GL_CULL_FACE);
}

void EyeBlit::shutdown() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
}

}
