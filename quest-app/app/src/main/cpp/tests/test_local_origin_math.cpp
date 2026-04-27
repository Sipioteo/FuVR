// SPDX-License-Identifier: Apache-2.0
//
// Smoke logic test for the stage-offset math used by Task 2 (STAGE
// reference space). The OpenXR runtime calls cannot be exercised on the
// host, so this test verifies the pose-inverse helper against a
// synthesized stage transform: applying the inverse to the original must
// yield identity.

#include <cmath>
#include <cstdio>

namespace {

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };
struct Pose { Vec3 position; Quat orientation; };

Pose pose_inverse(const Pose& p) {
    Pose inv{};
    inv.orientation.x = -p.orientation.x;
    inv.orientation.y = -p.orientation.y;
    inv.orientation.z = -p.orientation.z;
    inv.orientation.w =  p.orientation.w;
    const float qx = inv.orientation.x;
    const float qy = inv.orientation.y;
    const float qz = inv.orientation.z;
    const float qw = inv.orientation.w;
    const float vx = -p.position.x, vy = -p.position.y, vz = -p.position.z;
    const float tx = 2.0f * (qy * vz - qz * vy);
    const float ty = 2.0f * (qz * vx - qx * vz);
    const float tz = 2.0f * (qx * vy - qy * vx);
    inv.position.x = vx + qw * tx + (qy * tz - qz * ty);
    inv.position.y = vy + qw * ty + (qz * tx - qx * tz);
    inv.position.z = vz + qw * tz + (qx * ty - qy * tx);
    return inv;
}

Pose pose_compose(const Pose& a, const Pose& b) {
    // a applied to b's frame: a.rot * b + a.pos (translation), rot = a*b.
    Pose r{};
    r.orientation.w = a.orientation.w * b.orientation.w - a.orientation.x * b.orientation.x
                    - a.orientation.y * b.orientation.y - a.orientation.z * b.orientation.z;
    r.orientation.x = a.orientation.w * b.orientation.x + a.orientation.x * b.orientation.w
                    + a.orientation.y * b.orientation.z - a.orientation.z * b.orientation.y;
    r.orientation.y = a.orientation.w * b.orientation.y - a.orientation.x * b.orientation.z
                    + a.orientation.y * b.orientation.w + a.orientation.z * b.orientation.x;
    r.orientation.z = a.orientation.w * b.orientation.z + a.orientation.x * b.orientation.y
                    - a.orientation.y * b.orientation.x + a.orientation.z * b.orientation.w;
    // a.rot * b.pos
    const float qx = a.orientation.x, qy = a.orientation.y, qz = a.orientation.z, qw = a.orientation.w;
    const float vx = b.position.x, vy = b.position.y, vz = b.position.z;
    const float tx = 2.0f * (qy * vz - qz * vy);
    const float ty = 2.0f * (qz * vx - qx * vz);
    const float tz = 2.0f * (qx * vy - qy * vx);
    r.position.x = vx + qw * tx + (qy * tz - qz * ty) + a.position.x;
    r.position.y = vy + qw * ty + (qz * tx - qx * tz) + a.position.y;
    r.position.z = vz + qw * tz + (qx * ty - qy * tx) + a.position.z;
    return r;
}

int g_failures = 0;
#define CHECK(cond) do {                                                       \
    if (!(cond)) {                                                             \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        ++g_failures;                                                          \
    }                                                                          \
} while (0)

bool near0(float v) { return std::fabs(v) < 1e-4f; }

void test_inverse_yields_identity_when_composed() {
    // 90-deg yaw, 1.6m up.
    const float c = std::cos(0.7853981633f), s = std::sin(0.7853981633f);
    Pose p{};
    p.orientation.x = 0; p.orientation.y = s; p.orientation.z = 0; p.orientation.w = c;
    p.position = {1.0f, 1.6f, -2.0f};
    auto inv = pose_inverse(p);
    auto id = pose_compose(p, inv);
    CHECK(near0(id.position.x));
    CHECK(near0(id.position.y));
    CHECK(near0(id.position.z));
    CHECK(near0(id.orientation.x));
    CHECK(near0(id.orientation.y));
    CHECK(near0(id.orientation.z));
    CHECK(std::fabs(id.orientation.w - 1.0f) < 1e-4f
          || std::fabs(id.orientation.w + 1.0f) < 1e-4f);
}

}

int main() {
    test_inverse_yields_identity_when_composed();
    if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
    std::printf("test_local_origin_math: OK\n");
    return 0;
}
