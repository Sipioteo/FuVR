// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl32.h>

namespace fuvr {

// Blits half of a side-by-side stereo external texture (GL_TEXTURE_EXTERNAL_OES)
// into the currently bound framebuffer using a fullscreen triangle.
//
// `eye_index` 0 = left  -> samples u in [0.0, 0.5)
// `eye_index` 1 = right -> samples u in [0.5, 1.0]
class EyeBlit {
public:
    bool init();
    void shutdown();

    // Caller is responsible for binding the destination FBO and setting
    // viewport before calling. blit() saves and restores GL state it changes
    // (program, scissor, blend, depth, vao binding, active texture).
    void blit(GLuint external_texture, int eye_index);

    bool ready() const { return program_ != 0; }

private:
    GLuint program_{0};
    GLint  loc_uv_offset_{-1};
    GLint  loc_uv_scale_{-1};
    GLint  loc_sampler_{-1};
    GLuint vao_{0};
};

}
