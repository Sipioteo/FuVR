// SPDX-License-Identifier: Apache-2.0
//
// MockIVRCompositor — the shim's frame-submission heart. `WaitGetPoses`
// blocks on the daemon's frame-pacing signal and returns the predicted
// HMD/controller poses. `Submit` extracts the eye texture into an
// IOSurface (via the GL or Metal bridge), ships the surface over XPC,
// then posts a `SubmitFrame` RPC referencing the same token.

#include <cmath>
#include <cstring>

#include "openvr.h"

#include "log.hpp"
#include "mock_state.hpp"
#include "pose_math.hpp"
#include "texture_bridge.hpp"

namespace fuvr::openvr_shim {

class MockIVRCompositor final : public vr::IVRCompositor {
 public:
  void SetTrackingSpace(vr::ETrackingUniverseOrigin origin) override {
    space_ = origin;
  }
  vr::ETrackingUniverseOrigin GetTrackingSpace() override { return space_; }

  vr::EVRCompositorError WaitGetPoses(vr::TrackedDevicePose_t* renderPoses,
                                      uint32_t renderCount,
                                      vr::TrackedDevicePose_t* gamePoses,
                                      uint32_t gameCount) override {
    auto& st = MockState::shared();
    if (!st.isInitialized()) return vr::VRCompositorError_DoNotHaveFocus;

    // Block until the daemon hits the next frame-pacing edge.
    WaitFrameInfo wf{};
    if (!st.rpc().waitFrame(wf)) {
      return vr::VRCompositorError_RequestFailed;
    }

    PoseSet ps{};
    if (!st.rpc().queryPoses(0.0f, static_cast<uint32_t>(space_), ps)) {
      return vr::VRCompositorError_RequestFailed;
    }
    st.cachePoses(ps);

    fillPoseArray(ps, renderPoses, renderCount);
    fillPoseArray(ps, gamePoses, gameCount);
    return vr::VRCompositorError_None;
  }

  vr::EVRCompositorError GetLastPoses(vr::TrackedDevicePose_t* renderPoses,
                                      uint32_t renderCount,
                                      vr::TrackedDevicePose_t* gamePoses,
                                      uint32_t gameCount) override {
    PoseSet ps{};
    if (!MockState::shared().latestPoses(ps)) {
      return vr::VRCompositorError_RequestFailed;
    }
    fillPoseArray(ps, renderPoses, renderCount);
    fillPoseArray(ps, gamePoses, gameCount);
    return vr::VRCompositorError_None;
  }

  vr::EVRCompositorError GetLastPoseForTrackedDeviceIndex(
      vr::TrackedDeviceIndex_t idx,
      vr::TrackedDevicePose_t* outRender,
      vr::TrackedDevicePose_t* outGame) override {
    PoseSet ps{};
    if (!MockState::shared().latestPoses(ps)) {
      return vr::VRCompositorError_IndexOutOfRange;
    }
    if (idx > 2) return vr::VRCompositorError_IndexOutOfRange;
    vr::TrackedDevicePose_t arr[3]{};
    fillPoseArray(ps, arr, 3);
    if (outRender) *outRender = arr[idx];
    if (outGame)   *outGame   = arr[idx];
    return vr::VRCompositorError_None;
  }

  vr::EVRCompositorError GetSubmitTexture(vr::Texture_t*, bool*,
                                          vr::EVRCompositorTextureUsage,
                                          const vr::Texture_t*,
                                          const vr::VRTextureBounds_t*,
                                          vr::EVRSubmitFlags) override {
    return vr::VRCompositorError_RequestFailed;
  }

  vr::EVRCompositorError Submit(vr::EVREye eye,
                                const vr::Texture_t* tex,
                                const vr::VRTextureBounds_t* bounds,
                                vr::EVRSubmitFlags flags) override {
    return submitImpl(eye, tex, bounds, flags);
  }

  vr::EVRCompositorError SubmitWithArrayIndex(vr::EVREye eye,
                                              const vr::Texture_t* tex,
                                              uint32_t /*arrayIndex*/,
                                              const vr::VRTextureBounds_t* bounds,
                                              vr::EVRSubmitFlags flags) override {
    // Daemon-side OpenXR session uses one swapchain per eye; texture-
    // arrays collapse to slice-zero on this path. Vivecraft never uses
    // array textures in practice.
    return submitImpl(eye, tex, bounds, flags);
  }

  void ClearLastSubmittedFrame() override {}
  void PostPresentHandoff() override {
    // No-op: our `Submit` already commits the GPU work. The daemon's
    // frame-pacing thread is what blocks the next `WaitGetPoses`.
  }

  bool GetFrameTiming(vr::Compositor_FrameTiming* timing, uint32_t /*framesAgo*/) override {
    if (!timing) return false;
    std::memset(timing, 0, sizeof(*timing));
    timing->m_nSize = sizeof(*timing);
    return true;
  }
  uint32_t GetFrameTimings(vr::Compositor_FrameTiming*, uint32_t) override { return 0; }
  float GetFrameTimeRemaining() override { return 0.0f; }
  void GetCumulativeStats(vr::Compositor_CumulativeStats* stats, uint32_t bytes) override {
    if (stats && bytes >= sizeof(*stats)) std::memset(stats, 0, sizeof(*stats));
  }

  // ---- Visual chrome (no-ops; daemon owns the compositor on the Quest) ----
  void FadeToColor(float, float, float, float, float, bool) override {}
  vr::HmdColor_t GetCurrentFadeColor(bool) override { return {0,0,0,0}; }
  void FadeGrid(float, bool) override {}
  float GetCurrentGridAlpha() override { return 0.0f; }
  vr::EVRCompositorError SetSkyboxOverride(const vr::Texture_t*, uint32_t) override {
    return vr::VRCompositorError_RequestFailed;
  }
  void ClearSkyboxOverride() override {}
  void CompositorBringToFront() override {}
  void CompositorGoToBack() override {}
  void CompositorQuit() override {}
  bool IsFullscreen() override { return true; }
  uint32_t GetCurrentSceneFocusProcess() override { return 0; }
  uint32_t GetLastFrameRenderer() override { return 0; }
  bool CanRenderScene() override { return MockState::shared().isInitialized(); }
  void ShowMirrorWindow() override {}
  void HideMirrorWindow() override {}
  bool IsMirrorWindowVisible() override { return false; }
  void CompositorDumpImages() override {}
  bool ShouldAppRenderWithLowResources() override { return false; }
  void ForceInterleavedReprojectionOn(bool) override {}
  void ForceReconnectProcess() override {}
  void SuspendRendering(bool) override {}

  // ---- Mirror & Vulkan & DX paths — not used on macOS ----
  vr::EVRCompositorError GetMirrorTextureD3D11(vr::EVREye, void*, void**) override {
    return vr::VRCompositorError_RequestFailed;
  }
  void ReleaseMirrorTextureD3D11(void*) override {}
  vr::EVRCompositorError GetMirrorTextureGL(vr::EVREye, vr::glUInt_t*, vr::glSharedTextureHandle_t*) override {
    return vr::VRCompositorError_RequestFailed;
  }
  bool ReleaseSharedGLTexture(vr::glUInt_t, vr::glSharedTextureHandle_t) override { return false; }
  void LockGLSharedTextureForAccess(vr::glSharedTextureHandle_t) override {}
  void UnlockGLSharedTextureForAccess(vr::glSharedTextureHandle_t) override {}
  uint32_t GetVulkanInstanceExtensionsRequired(char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  uint32_t GetVulkanDeviceExtensionsRequired(struct VkPhysicalDevice_T*, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  void SetExplicitTimingMode(vr::EVRCompositorTimingMode) override {}
  vr::EVRCompositorError SubmitExplicitTimingData() override {
    return vr::VRCompositorError_None;
  }
  bool IsMotionSmoothingEnabled() override { return false; }
  bool IsMotionSmoothingSupported() override { return false; }
  bool IsCurrentSceneFocusAppLoading() override { return false; }
  vr::EVRCompositorError SetStageOverride_Async(const char*,
                                                const vr::HmdMatrix34_t*,
                                                const vr::Compositor_StageRenderSettings*,
                                                uint32_t) override {
    return vr::VRCompositorError_None;
  }
  void ClearStageOverride() override {}
  bool GetCompositorBenchmarkResults(vr::Compositor_BenchmarkResults*, uint32_t) override { return false; }
  vr::EVRCompositorError GetLastPosePredictionIDs(uint32_t* render, uint32_t* game) override {
    if (render) *render = 0;
    if (game)   *game = 0;
    return vr::VRCompositorError_None;
  }
  vr::EVRCompositorError GetPosesForFrame(uint32_t,
                                          vr::TrackedDevicePose_t* poses,
                                          uint32_t poseCount) override {
    PoseSet ps{};
    if (!MockState::shared().latestPoses(ps)) return vr::VRCompositorError_RequestFailed;
    fillPoseArray(ps, poses, poseCount);
    return vr::VRCompositorError_None;
  }

 private:
  vr::EVRCompositorError submitImpl(vr::EVREye eye,
                                    const vr::Texture_t* tex,
                                    const vr::VRTextureBounds_t* bounds,
                                    vr::EVRSubmitFlags /*flags*/) {
    static uint64_t s_calls = 0;
    static uint64_t s_failedInit = 0, s_failedBridge = 0, s_failedPrepare = 0;
    static uint64_t s_failedCopy = 0, s_failedRpc = 0, s_ok = 0;
    const bool first = (s_calls == 0);
    ++s_calls;

    auto& st = MockState::shared();
    if (!st.isInitialized() || !tex) {
      ++s_failedInit;
      if (first || (s_calls % 60) == 0) {
        FUVR_LOG("submit[%llu]: failed-init init=%d tex=%p (failedInit=%llu)",
                 (unsigned long long)s_calls, (int)st.isInitialized(),
                 (void*)tex, (unsigned long long)s_failedInit);
      }
      return vr::VRCompositorError_RequestFailed;
    }

    TextureBridge* bridge = st.getOrCreateBridge(static_cast<uint32_t>(tex->eType));
    if (!bridge) {
      ++s_failedBridge;
      FUVR_LOG("submit[%llu]: no bridge for texture type %d (failedBridge=%llu)",
               (unsigned long long)s_calls, (int)tex->eType,
               (unsigned long long)s_failedBridge);
      return vr::VRCompositorError_TextureUsesUnsupportedFormat;
    }
    if (!bridge->prepare(st.caps().perEyeWidth, st.caps().perEyeHeight)) {
      ++s_failedPrepare;
      if (first || (s_calls % 60) == 0) {
        FUVR_LOG("submit[%llu]: bridge prepare failed (%ux%u)",
                 (unsigned long long)s_calls,
                 st.caps().perEyeWidth, st.caps().perEyeHeight);
      }
      return vr::VRCompositorError_RequestFailed;
    }
    BridgeFrame perEye = bridge->copyFromTexture(static_cast<uint32_t>(eye), tex, bounds);
    if (!perEye.surface) {
      ++s_failedCopy;
      if (first || (s_calls % 60) == 0) {
        FUVR_LOG("submit[%llu]: bridge copyFromTexture returned no surface (eye=%d, type=%d)",
                 (unsigned long long)s_calls, (int)eye, (int)tex->eType);
      }
      return vr::VRCompositorError_TextureIsOnWrongDevice;
    }

    if (first) {
      FUVR_LOG("submit[1]: FIRST submit — eye=%d type=%d w=%u h=%u token=%llu (per-eye scratch)",
               (int)eye, (int)tex->eType,
               st.caps().perEyeWidth, st.caps().perEyeHeight,
               (unsigned long long)perEye.token);
    }

    // Stereo composite contract:
    //   • Left eye: copy into the bridge's left ring slot (already done
    //     by copyFromTexture above) and stash the most-recent per-eye
    //     bounds. Do NOT ship anything yet — the daemon expects ONE
    //     side-by-side frame per pair, not two distinct frames.
    //   • Right eye: copy into the right ring slot, then call
    //     finalizeStereoFrame() which composites L|R into a single SBS
    //     IOSurface; ship THAT (xpc + rpc.submitFrame) once.
    if (eye == vr::Eye_Left) {
      if (bounds) lastLeftBounds_ = *bounds;
      else        lastLeftBounds_ = vr::VRTextureBounds_t{0.0f, 0.0f, 1.0f, 1.0f};
      hasLeftBounds_ = true;
      return vr::VRCompositorError_None;
    }

    // Right eye → finalize and ship.
    BridgeFrame frame = bridge->finalizeStereoFrame();
    if (!frame.surface) {
      // Right arrived without a corresponding Left this frame (or vice
      // versa earlier). Drop silently; the next L+R pair recovers.
      if ((s_calls % 60) == 0) {
        FUVR_LOG("submit[%llu]: finalizeStereoFrame produced no surface (orphan eye?)",
                 (unsigned long long)s_calls);
      }
      return vr::VRCompositorError_None;
    }

    // Ship the SBS IOSurface mach send-right to the daemon, then announce
    // it over the RPC socket. The daemon pairs them by token.
    st.xpc().send(frame.token, frame.surface);

    PoseSet latest{};
    st.latestPoses(latest);
    vr::HmdMatrix34_t hmd34 = posemath::matrixFromPose(latest.hmd);
    float renderPose[12];
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c)
        renderPose[r * 4 + c] = hmd34.m[r][c];

    // Compose per-eye render pose: world ← head ← eye. Vivecraft already
    // bakes the eye-from-head offset into its per-eye view matrix on the
    // game side, but the Quest's projection-layer compositor still needs
    // the absolute camera each half of the SBS texture was rendered from
    // — otherwise its scan-out reprojection adds another ±IPD/2 on top
    // and we get unfusable double vision.
    const auto& caps = st.caps();
    vr::HmdMatrix34_t eyeFromHeadL = posemath::matrix34FromFlat(caps.eyeFromHeadLeft);
    vr::HmdMatrix34_t eyeFromHeadR = posemath::matrix34FromFlat(caps.eyeFromHeadRight);
    vr::HmdMatrix34_t leftEye34  = posemath::mul34(hmd34, eyeFromHeadL);
    vr::HmdMatrix34_t rightEye34 = posemath::mul34(hmd34, eyeFromHeadR);
    float renderPoseLeft[7]{}, renderPoseRight[7]{};
    posemath::poseFromMatrix(leftEye34,  renderPoseLeft,  renderPoseLeft  + 3);
    posemath::poseFromMatrix(rightEye34, renderPoseRight, renderPoseRight + 3);

    {
      static bool s_loggedFirstPose = false;
      if (!s_loggedFirstPose) {
        s_loggedFirstPose = true;
        FUVR_LOG("submit: first per-eye render-pose L pos=(%.3f,%.3f,%.3f) "
                 "quat=(%.3f,%.3f,%.3f,%.3f) R pos=(%.3f,%.3f,%.3f) "
                 "quat=(%.3f,%.3f,%.3f,%.3f)",
                 renderPoseLeft[0],  renderPoseLeft[1],  renderPoseLeft[2],
                 renderPoseLeft[3],  renderPoseLeft[4],  renderPoseLeft[5],  renderPoseLeft[6],
                 renderPoseRight[0], renderPoseRight[1], renderPoseRight[2],
                 renderPoseRight[3], renderPoseRight[4], renderPoseRight[5], renderPoseRight[6]);
      }
    }

    // Bounds for the SBS frame: the shim writes the full extent of each
    // eye into its half of the side-by-side surface, so the encoder /
    // Quest-side compositor sees a normalized [0,1] in each half. We
    // forward [0,1] for u and the right eye's v bounds (left+right v
    // bounds match in practice — both eyes render at the same vertical
    // extent through Vivecraft's framebuffers).
    float boundsArr[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    if (bounds) {
      boundsArr[2] = bounds->vMin;
      boundsArr[3] = bounds->vMax;
    }
    // Use a single eye marker so the daemon can route the SBS frame
    // through one path. eye=2 ("both/combined") was previously unused.
    constexpr uint32_t kEyeBoth = 2;
    // Convert OpenVR tangent-half-angles to OpenXR XrFovf radians.
    // OpenVR's GetProjectionRaw and our daemon's HelloOk supply tangents
    // (e.g. -0.94, 0.78, 0.92, -0.92). OpenXR's XrFovf wants angles in
    // radians (atan(tangent)). Passing tangents directly to the Quest
    // makes the runtime project the texture into a wrong-sized virtual
    // screen — visually similar to inverse stereo / unfusable double
    // vision because the L and R virtual screens don't align where the
    // user expects them.
    float leftFovRad[4]{
        std::atan(caps.leftFov[0]), std::atan(caps.leftFov[1]),
        std::atan(caps.leftFov[2]), std::atan(caps.leftFov[3])};
    float rightFovRad[4]{
        std::atan(caps.rightFov[0]), std::atan(caps.rightFov[1]),
        std::atan(caps.rightFov[2]), std::atan(caps.rightFov[3])};
    {
      static bool s_loggedFirstFov = false;
      if (!s_loggedFirstFov) {
        s_loggedFirstFov = true;
        FUVR_LOG("submit: first per-eye fov rad L=(%.3f,%.3f,%.3f,%.3f) "
                 "R=(%.3f,%.3f,%.3f,%.3f)",
                 leftFovRad[0],  leftFovRad[1],  leftFovRad[2],  leftFovRad[3],
                 rightFovRad[0], rightFovRad[1], rightFovRad[2], rightFovRad[3]);
      }
    }
    if (!st.rpc().submitFrame(kEyeBoth,
                              frame.token,
                              /*flags*/ 0,
                              boundsArr,
                              renderPose,
                              renderPoseLeft,
                              renderPoseRight,
                              leftFovRad,
                              rightFovRad)) {
      ++s_failedRpc;
      if (first || (s_failedRpc % 60) == 1) {
        FUVR_LOG("submit[%llu]: rpc.submitFrame failed (failedRpc=%llu, ok=%llu)",
                 (unsigned long long)s_calls,
                 (unsigned long long)s_failedRpc, (unsigned long long)s_ok);
      }
      hasLeftBounds_ = false;
      return vr::VRCompositorError_RequestFailed;
    }
    ++s_ok;
    if ((s_ok % 120) == 1) {
      FUVR_LOG("submit[%llu]: ok=%llu (SBS, last token=%llu)",
               (unsigned long long)s_calls, (unsigned long long)s_ok,
               (unsigned long long)frame.token);
    }
    hasLeftBounds_ = false;
    return vr::VRCompositorError_None;
  }

  static void fillPoseArray(const PoseSet& src,
                            vr::TrackedDevicePose_t* arr,
                            uint32_t count) {
    if (!arr) return;
    auto fill = [](vr::TrackedDevicePose_t& dst, const float p[13], bool valid) {
      dst.bDeviceIsConnected = valid;
      dst.bPoseIsValid = valid;
      dst.eTrackingResult = valid ? vr::TrackingResult_Running_OK
                                  : vr::TrackingResult_Uninitialized;
      dst.mDeviceToAbsoluteTracking = posemath::matrixFromPose(p);
      dst.vVelocity        = { p[7],  p[8],  p[9]  };
      dst.vAngularVelocity = { p[10], p[11], p[12] };
    };
    if (count > 0) fill(arr[0], src.hmd,       (src.validMask & 1) != 0);
    if (count > 1) fill(arr[1], src.leftCtrl,  (src.validMask & 2) != 0);
    if (count > 2) fill(arr[2], src.rightCtrl, (src.validMask & 4) != 0);
    for (uint32_t i = 3; i < count; ++i) arr[i] = vr::TrackedDevicePose_t{};
  }

  vr::ETrackingUniverseOrigin space_ = vr::TrackingUniverseStanding;
  // Most-recent left-eye bounds; held until the right eye arrives and we
  // composite the SBS frame.
  vr::VRTextureBounds_t lastLeftBounds_{0.0f, 0.0f, 1.0f, 1.0f};
  bool hasLeftBounds_{false};
};

vr::IVRCompositor* mockIVRCompositor() {
  static MockIVRCompositor instance;
  return &instance;
}

}  // namespace fuvr::openvr_shim
