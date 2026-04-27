// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <EGL/egl.h>
#include <GLES3/gl32.h>

namespace fuvr {

// Reprojects (Asynchronous Time Warp) a side-by-side stereo external texture
// into the currently bound framebuffer using a fullscreen triangle.
//
// The fragment shader unprojects the destination NDC through the present-time
// eye fov, rotates the resulting view ray by the delta between the pose used
// to render the frame and the pose now (R_delta_inv = q_now⁻¹ · q_render),
// then projects through the render-time fov and maps that into the
// side-by-side source texture using uv_offset / uv_scale.
//
// `eye_index` 0 = left -> samples u in [0.0, 0.5)
// `eye_index` 1 = right -> samples u in [0.5, 1.0]
//
// STEREO-SPLIT marker — the stereo pipeline agent owns the source-side SBS
// 2W×H layout. uv_offset / uv_scale are the only knobs the warp shader uses
// to address its half of that layout, so swapping in an over/under or full-
// frame layout is a one-call change here.
class EyeBlit {
public:
    bool init();
    void shutdown();

    struct Fov {
        float angleLeft{0}, angleRight{0}, angleUp{0}, angleDown{0};
    };

    struct WarpParams {
        Fov fov_now{};      // half-angles, radians (xrLocateViews this frame)
        Fov fov_render{};   // half-angles used by the Mac to render this frame
        // Row-major 3x3 from now-eye-space → render-eye-space (i.e. inverse
        // of the now-rotation composed with render-rotation).
        float r_delta_inv[9]{1,0,0, 0,1,0, 0,0,1};
        // Source UV remap for SBS layout. Defaults are the SBS half for the
        // requested eye_index (set by blit() if WarpParams is left default).
        float uv_offset[2]{0.0f, 0.0f};
        float uv_scale[2]{1.0f, 1.0f};
        bool override_uv{false};
    };

    // Caller is responsible for binding the destination FBO and setting
    // viewport before calling. blit() saves and restores GL state it changes
    // (program, scissor, blend, depth, vao binding, active texture).
    void blit(GLuint external_texture, int eye_index, const WarpParams& warp);

    // Convenience: identity-warp blit (no ATW). Used as a fallback before any
    // rendered pose is known.
    void blit(GLuint external_texture, int eye_index);

    bool ready() const { return program_ != 0; }

private:
    GLuint program_{0};
    GLint  loc_uv_offset_{-1};
    GLint  loc_uv_scale_{-1};
    GLint  loc_sampler_{-1};
    GLint  loc_fov_now_{-1};
    GLint  loc_fov_render_{-1};
    GLint  loc_r_delta_inv_{-1};
    GLuint vao_{0};
};

}
