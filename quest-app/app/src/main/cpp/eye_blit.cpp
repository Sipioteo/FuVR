// SPDX-License-Identifier: Apache-2.0

#include "eye_blit.hpp"

#include <GLES2/gl2ext.h>
#include <android/log.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "fuvr.blit", __VA_ARGS__)

namespace fuvr {

namespace {

constexpr const char* kVS = R"(#version 300 es
out vec2 vUv;
void main() {
    // Fullscreen triangle covering NDC [-1,1] with uv [0,1].
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0,
                  (gl_VertexID == 2) ? 3.0 : -1.0);
    vUv = (p + 1.0) * 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";

constexpr const char* kFS = R"(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 vUv;
uniform samplerExternalOES uTex;
uniform vec2 uUvOffset;
uniform vec2 uUvScale;
out vec4 oColor;
void main() {
    vec2 uv = vUv * uUvScale + uUvOffset;
    oColor = texture(uTex, uv);
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
    loc_uv_offset_ = glGetUniformLocation(program_, "uUvOffset");
    loc_uv_scale_  = glGetUniformLocation(program_, "uUvScale");
    loc_sampler_   = glGetUniformLocation(program_, "uTex");
    glGenVertexArrays(1, &vao_);
    return true;
}

void EyeBlit::blit(GLuint external_texture, int eye_index) {
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

    // Left eye samples [0, 0.5] in u; right eye samples [0.5, 1.0].
    const float offset_u = (eye_index == 0) ? 0.0f : 0.5f;
    glUniform2f(loc_uv_offset_, offset_u, 0.0f);
    glUniform2f(loc_uv_scale_, 0.5f, 1.0f);

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
