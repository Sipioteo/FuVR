// SPDX-License-Identifier: Apache-2.0
//
// Pure-CPU replica of the rotational ATW fragment shader in
// quest-app/app/src/main/cpp/eye_blit.cpp. The shader maps a destination
// (u, v) ∈ [0,1]² (per-eye, after the vNdc → [0,1] remap) to a source
// (ur, vr) ∈ [0,1]² using:
//
//     R_delta_inv = mat3( quat_conjugate(q_render) · q_now )
//     dir_now     = (mix(tanL_n,tanR_n,u), mix(tanD_n,tanU_n,v), -1)
//     dir_r       = R_delta_inv · dir_now
//     xr/yr       = dir_r.{x,y} / -dir_r.z
//     ur          = (xr - tanL_r) / (tanR_r - tanL_r)
//     vr          = (yr - tanD_r) / (tanU_r - tanD_r)
//
// We exercise this with hand-computed expected values so a regression in
// quat_math.hpp or the shader semantics is caught on the host long before
// it shows up as a wrong-feeling head-turn on the headset.

#include "../quat_math.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

struct Fov {
    float angleLeft;
    float angleRight;
    float angleUp;
    float angleDown;
};

struct UV { float u; float v; };

// In-shader reprojection. Writes ur/vr to `out` and returns true if the
// sample lands inside [0,1]² (i.e. the shader's clamp-to-black branch would
// NOT fire).
bool reproject(const fuvr::Quat& q_render,
               const fuvr::Quat& q_now,
               const Fov& fov_now,
               const Fov& fov_render,
               float u, float v,
               UV& out) {
    // R_delta_inv = R(q_render⁻¹ · q_now). Identical to compositor.cpp's
    //   dq = quat_mul(quat_conjugate(quat_normalize(q_ren)), quat_normalize(q_now));
    //   quat_to_mat3_rowmajor(dq, warp.r_delta_inv);
    fuvr::Quat dq = fuvr::quat_mul(
        fuvr::quat_conjugate(fuvr::quat_normalize(q_render)),
        fuvr::quat_normalize(q_now));
    float r[9];
    fuvr::quat_to_mat3_rowmajor(dq, r);

    const float tanL_n = std::tan(fov_now.angleLeft);
    const float tanR_n = std::tan(fov_now.angleRight);
    const float tanU_n = std::tan(fov_now.angleUp);
    const float tanD_n = std::tan(fov_now.angleDown);
    const float tanL_r = std::tan(fov_render.angleLeft);
    const float tanR_r = std::tan(fov_render.angleRight);
    const float tanU_r = std::tan(fov_render.angleUp);
    const float tanD_r = std::tan(fov_render.angleDown);

    auto mix = [](float a, float b, float t) { return a + t * (b - a); };
    const float dnx = mix(tanL_n, tanR_n, u);
    const float dny = mix(tanD_n, tanU_n, v);
    const float dnz = -1.0f;

    // Row-major mat3·vec.
    const float drx = r[0]*dnx + r[1]*dny + r[2]*dnz;
    const float dry = r[3]*dnx + r[4]*dny + r[5]*dnz;
    const float drz = r[6]*dnx + r[7]*dny + r[8]*dnz;

    if (drz >= 0.0f) {
        // Behind the camera in the render frame — the shader would draw
        // black. Report as out-of-FOV for symmetry.
        out = UV{-1.0f, -1.0f};
        return false;
    }

    const float xr = drx / -drz;
    const float yr = dry / -drz;
    out.u = (xr - tanL_r) / (tanR_r - tanL_r);
    out.v = (yr - tanD_r) / (tanU_r - tanD_r);
    return out.u >= 0.0f && out.u <= 1.0f && out.v >= 0.0f && out.v <= 1.0f;
}

constexpr float kTol = 1e-4f;

#define CHECK_NEAR(actual, expected, tol)                                    \
    do {                                                                     \
        float __a = (actual), __e = (expected), __t = (tol);                 \
        if (std::fabs(__a - __e) > __t) {                                    \
            std::fprintf(stderr,                                             \
                         "FAIL %s:%d  %s = %.6f  expected %.6f  (|d|=%.6f > %.6f)\n", \
                         __FILE__, __LINE__, #actual, __a, __e,              \
                         std::fabs(__a - __e), __t);                         \
            std::abort();                                                    \
        }                                                                    \
    } while (0)

#define CHECK_TRUE(expr)                                                     \
    do {                                                                     \
        if (!(expr)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d  %s\n",                         \
                         __FILE__, __LINE__, #expr);                         \
            std::abort();                                                    \
        }                                                                    \
    } while (0)

// Build a unit quaternion for rotation by `angle_rad` about a unit axis.
fuvr::Quat axisAngle(float ax, float ay, float az, float angle_rad) {
    const float h = 0.5f * angle_rad;
    const float s = std::sin(h);
    return fuvr::Quat{ax * s, ay * s, az * s, std::cos(h)};
}

void test_identity() {
    fuvr::Quat q{0, 0, 0, 1};
    Fov fov{-0.95f, 0.95f, 0.95f, -0.95f};
    UV out{};
    bool inside = reproject(q, q, fov, fov, 0.5f, 0.5f, out);
    CHECK_TRUE(inside);
    CHECK_NEAR(out.u, 0.5f, kTol);
    CHECK_NEAR(out.v, 0.5f, kTol);
    std::printf("[ OK ] identity (u,v)=(0.5,0.5) -> (%.6f, %.6f)\n", out.u, out.v);
}

void test_yaw_30_right() {
    // q_render = identity, q_now = -30° about Y (head turned right).
    // Expected horizontal sample shift: tan(30°) / (tanR_n - tanL_n).
    fuvr::Quat q_render{0, 0, 0, 1};
    const float yaw = -30.0f * static_cast<float>(M_PI) / 180.0f;
    fuvr::Quat q_now = axisAngle(0, 1, 0, yaw);
    Fov fov{-0.95f, 0.95f, 0.95f, -0.95f};
    UV out{};
    bool inside = reproject(q_render, q_now, fov, fov, 0.5f, 0.5f, out);
    CHECK_TRUE(inside);
    const float tanL_n = std::tan(fov.angleLeft);
    const float tanR_n = std::tan(fov.angleRight);
    const float expected_shift =
        std::tan(30.0f * static_cast<float>(M_PI) / 180.0f)
        / (tanR_n - tanL_n);
    CHECK_NEAR(out.u, 0.5f + expected_shift, kTol);
    CHECK_NEAR(out.v, 0.5f, kTol);
    std::printf("[ OK ] yaw 30deg right -> u=%.6f (shift=%.6f)\n",
                out.u, expected_shift);
}

void test_pitch_20_up() {
    // q_render = identity, q_now = +20° about X (head pitched up).
    fuvr::Quat q_render{0, 0, 0, 1};
    const float pitch = 20.0f * static_cast<float>(M_PI) / 180.0f;
    fuvr::Quat q_now = axisAngle(1, 0, 0, pitch);
    Fov fov{-0.95f, 0.95f, 0.95f, -0.95f};
    UV out{};
    bool inside = reproject(q_render, q_now, fov, fov, 0.5f, 0.5f, out);
    CHECK_TRUE(inside);
    const float tanD_n = std::tan(fov.angleDown);
    const float tanU_n = std::tan(fov.angleUp);
    const float expected_shift =
        std::tan(20.0f * static_cast<float>(M_PI) / 180.0f)
        / (tanU_n - tanD_n);
    CHECK_NEAR(out.u, 0.5f, kTol);
    CHECK_NEAR(out.v, 0.5f + expected_shift, kTol);
    CHECK_TRUE(out.v > 0.5f);  // sampling from above
    std::printf("[ OK ] pitch 20deg up -> v=%.6f (shift=%.6f)\n",
                out.v, expected_shift);
}

void test_overscan_corners_inside() {
    // fov_render = fov_now × 1.25 — render with a wider FOV than the now-fov.
    // q_render == q_now → R_delta_inv = identity. Every (u,v)∈[0,1]² should
    // map inside [0,1]² in the source.
    fuvr::Quat q{0, 0, 0, 1};
    Fov fov_now{-0.95f, 0.95f, 0.95f, -0.95f};
    Fov fov_render{fov_now.angleLeft * 1.25f, fov_now.angleRight * 1.25f,
                   fov_now.angleUp * 1.25f, fov_now.angleDown * 1.25f};
    const UV corners[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}};
    for (const UV& c : corners) {
        UV out{};
        bool inside = reproject(q, q, fov_now, fov_render, c.u, c.v, out);
        if (!inside) {
            std::fprintf(stderr,
                         "FAIL overscan corner (%.1f,%.1f) -> (%.6f,%.6f) outside [0,1]\n",
                         c.u, c.v, out.u, out.v);
            std::abort();
        }
    }
    // Center should land at exactly 0.5, 0.5.
    UV center{};
    reproject(q, q, fov_now, fov_render, 0.5f, 0.5f, center);
    CHECK_NEAR(center.u, 0.5f, kTol);
    CHECK_NEAR(center.v, 0.5f, kTol);
    std::printf("[ OK ] overscan: all 4 corners inside source [0,1]\n");
}

void test_out_of_fov_yaw_60() {
    // q_now is 60° yaw beyond q_render → tan(60°) far exceeds tanR_n, so the
    // dest center maps to ur > 1 (or < 0). The shader's clamp-to-black branch
    // would fire.
    fuvr::Quat q_render{0, 0, 0, 1};
    const float yaw = -60.0f * static_cast<float>(M_PI) / 180.0f;
    fuvr::Quat q_now = axisAngle(0, 1, 0, yaw);
    Fov fov{-0.95f, 0.95f, 0.95f, -0.95f};
    UV out{};
    bool inside = reproject(q_render, q_now, fov, fov, 0.5f, 0.5f, out);
    CHECK_TRUE(!inside);
    CHECK_TRUE(out.u < 0.0f || out.u > 1.0f);
    std::printf("[ OK ] out-of-fov yaw 60deg -> u=%.6f (clamp branch fires)\n",
                out.u);
}

}  // namespace

int main() {
    test_identity();
    test_yaw_30_right();
    test_pitch_20_up();
    test_overscan_corners_inside();
    test_out_of_fov_yaw_60();
    std::printf("test_atw_math: 5/5 passed\n");
    return 0;
}
