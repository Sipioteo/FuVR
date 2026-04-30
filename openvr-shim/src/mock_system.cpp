// SPDX-License-Identifier: Apache-2.0
//
// MockIVRSystem — implements the subset of OpenVR's IVRSystem that legacy
// Mac SteamVR titles (Vivecraft, Unity 5.x, UE 4.10-4.18) actually call.
// Every pure virtual is overridden so the class is concrete; the methods
// not exercised by typical games are stubbed with success-shaped defaults.

#include <atomic>
#include <cstring>
#include <cstdio>

#include "openvr.h"

#include "log.hpp"
#include "mock_state.hpp"
#include "pose_math.hpp"

namespace fuvr::openvr_shim {

class MockIVRSystem final : public vr::IVRSystem {
 public:
  // ---- Display / projection ----

  void GetRecommendedRenderTargetSize(uint32_t* w, uint32_t* h) override {
    const auto& c = MockState::shared().caps();
    if (w) *w = c.perEyeWidth;
    if (h) *h = c.perEyeHeight;
  }

  vr::HmdMatrix44_t GetProjectionMatrix(vr::EVREye eye, float zNear, float zFar) override {
    const auto& c = MockState::shared().caps();
    const float* fov = (eye == vr::Eye_Left) ? c.leftFov : c.rightFov;
    return posemath::projectionFromFovTangents(fov, zNear, zFar);
  }

  void GetProjectionRaw(vr::EVREye eye, float* l, float* r, float* t, float* b) override {
    const auto& c = MockState::shared().caps();
    const float* fov = (eye == vr::Eye_Left) ? c.leftFov : c.rightFov;
    if (l) *l = fov[0];
    if (r) *r = fov[1];
    if (t) *t = fov[2];
    if (b) *b = fov[3];
  }

  bool ComputeDistortion(vr::EVREye, float u, float v, vr::DistortionCoordinates_t* out) override {
    if (!out) return false;
    out->rfRed[0] = u;   out->rfRed[1] = v;
    out->rfGreen[0] = u; out->rfGreen[1] = v;
    out->rfBlue[0] = u;  out->rfBlue[1] = v;
    return true;
  }

  bool ComputeDistortionSet(vr::EVREye, vr::EVRDistortionChannel, bool,
                            uint32_t, const vr::DistortionCoordinate_t*,
                            vr::DistortionCoordinate_t*) override {
    // Daemon-side compositor handles distortion via OpenXR.
    return false;
  }

  vr::HmdMatrix34_t GetEyeToHeadTransform(vr::EVREye eye) override {
    const auto& c = MockState::shared().caps();
    const float* m = (eye == vr::Eye_Left) ? c.eyeFromHeadLeft : c.eyeFromHeadRight;
    vr::HmdMatrix34_t out{};
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 4; ++col)
        out.m[row][col] = m[row * 4 + col];
    static std::atomic<uint32_t> seenE2H[2]{0, 0};
    uint32_t n = seenE2H[eye & 1].fetch_add(1, std::memory_order_relaxed);
    if (n < 2) {
      FUVR_LOG("mock: GetEyeToHeadTransform eye=%d call=%u | "
               "row0=[%.3f %.3f %.3f %.3f] row1=[%.3f %.3f %.3f %.3f] row2=[%.3f %.3f %.3f %.3f]",
               (int)eye, n,
               out.m[0][0], out.m[0][1], out.m[0][2], out.m[0][3],
               out.m[1][0], out.m[1][1], out.m[1][2], out.m[1][3],
               out.m[2][0], out.m[2][1], out.m[2][2], out.m[2][3]);
    }
    return out;
  }

  bool GetTimeSinceLastVsync(float* secondsSinceLastVsync, uint64_t* frameCounter) override {
    // Best-effort: ask the daemon for its last frame index. If not yet
    // connected, fake values prevent divide-by-zero in framerate-tracking
    // code paths in some games.
    if (secondsSinceLastVsync) *secondsSinceLastVsync = 0.0f;
    if (frameCounter) *frameCounter = 0;
    return true;
  }

  int32_t GetD3D9AdapterIndex() override { return -1; }  // No D3D on macOS.
  void GetDXGIOutputInfo(int32_t* idx) override { if (idx) *idx = -1; }
  void GetOutputDevice(uint64_t* dev, vr::ETextureType, struct VkInstance_T*) override {
    if (dev) *dev = 0;
  }

  bool IsDisplayOnDesktop() override { return false; }
  bool SetDisplayVisibility(bool) override { return true; }

  // ---- Tracking ----

  void GetDeviceToAbsoluteTrackingPose(vr::ETrackingUniverseOrigin origin,
                                       float predictedSecondsToPhotonsFromNow,
                                       vr::TrackedDevicePose_t* poseArr,
                                       uint32_t poseArrCount) override {
    if (!poseArr || poseArrCount == 0) return;
    PoseSet ps{};
    if (!MockState::shared().rpc().queryPoses(
          predictedSecondsToPhotonsFromNow,
          static_cast<uint32_t>(origin), ps)) {
      // Initialize to invalid so the game knows tracking is paused.
      for (uint32_t i = 0; i < poseArrCount; ++i) poseArr[i] = vr::TrackedDevicePose_t{};
      return;
    }
    MockState::shared().cachePoses(ps);
    fillPosesIntoArray(ps, poseArr, poseArrCount);
  }

  vr::HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() override {
    return posemath::kIdentity34;
  }
  vr::HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() override {
    return posemath::kIdentity34;
  }

  uint32_t GetSortedTrackedDeviceIndicesOfClass(vr::ETrackedDeviceClass cls,
                                                vr::TrackedDeviceIndex_t* arr,
                                                uint32_t arrCount,
                                                vr::TrackedDeviceIndex_t /*relativeTo*/) override {
    // Hardcoded device topology: 0 = HMD, 1 = left ctrl, 2 = right ctrl.
    if (!arr || arrCount == 0) return 0;
    uint32_t n = 0;
    if (cls == vr::TrackedDeviceClass_HMD) {
      arr[n++] = 0;
    } else if (cls == vr::TrackedDeviceClass_Controller) {
      const auto mask = MockState::shared().caps().controllerMask;
      if ((mask & 1) && n < arrCount) arr[n++] = 1;
      if ((mask & 2) && n < arrCount) arr[n++] = 2;
    }
    return n;
  }

  vr::EDeviceActivityLevel GetTrackedDeviceActivityLevel(vr::TrackedDeviceIndex_t idx) override {
    if (idx > 2) return vr::k_EDeviceActivityLevel_Unknown;
    return vr::k_EDeviceActivityLevel_UserInteraction;
  }

  void ApplyTransform(vr::TrackedDevicePose_t* out,
                      const vr::TrackedDevicePose_t* in,
                      const vr::HmdMatrix34_t* xf) override {
    if (!out || !in || !xf) return;
    *out = *in;
    // Multiply 3x4 matrices: m_out = xf * in.deviceToAbs.
    auto& a = xf->m;
    auto& b = in->mDeviceToAbsoluteTracking.m;
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 4; ++c) {
        float v = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
        if (c == 3) v += a[r][3];
        out->mDeviceToAbsoluteTracking.m[r][c] = v;
      }
    }
  }

  vr::TrackedDeviceIndex_t GetTrackedDeviceIndexForControllerRole(
      vr::ETrackedControllerRole role) override {
    if (role == vr::TrackedControllerRole_LeftHand)  return 1;
    if (role == vr::TrackedControllerRole_RightHand) return 2;
    return vr::k_unTrackedDeviceIndexInvalid;
  }
  vr::ETrackedControllerRole GetControllerRoleForTrackedDeviceIndex(
      vr::TrackedDeviceIndex_t idx) override {
    if (idx == 1) return vr::TrackedControllerRole_LeftHand;
    if (idx == 2) return vr::TrackedControllerRole_RightHand;
    return vr::TrackedControllerRole_Invalid;
  }

  vr::ETrackedDeviceClass GetTrackedDeviceClass(vr::TrackedDeviceIndex_t idx) override {
    if (idx == 0) return vr::TrackedDeviceClass_HMD;
    if (idx == 1 || idx == 2) return vr::TrackedDeviceClass_Controller;
    return vr::TrackedDeviceClass_Invalid;
  }

  bool IsTrackedDeviceConnected(vr::TrackedDeviceIndex_t idx) override {
    if (idx == 0) return true;  // HMD is always present once we're up.
    const auto mask = MockState::shared().caps().controllerMask;
    if (idx == 1) return (mask & 1) != 0;
    if (idx == 2) return (mask & 2) != 0;
    return false;
  }

  // ---- Property accessors (return safe defaults; games tolerate misses) ----

  bool GetBoolTrackedDeviceProperty(vr::TrackedDeviceIndex_t,
                                    vr::ETrackedDeviceProperty,
                                    vr::ETrackedPropertyError* err) override {
    if (err) *err = vr::TrackedProp_UnknownProperty;
    return false;
  }
  float GetFloatTrackedDeviceProperty(vr::TrackedDeviceIndex_t,
                                      vr::ETrackedDeviceProperty prop,
                                      vr::ETrackedPropertyError* err) override {
    if (err) *err = vr::TrackedProp_Success;
    if (prop == vr::Prop_DisplayFrequency_Float) {
      return static_cast<float>(MockState::shared().caps().refreshRateHz);
    }
    if (prop == vr::Prop_UserIpdMeters_Float) {
      return 0.064f;  // 64 mm placeholder; daemon may override later.
    }
    if (err) *err = vr::TrackedProp_UnknownProperty;
    return 0.0f;
  }
  int32_t GetInt32TrackedDeviceProperty(vr::TrackedDeviceIndex_t,
                                        vr::ETrackedDeviceProperty,
                                        vr::ETrackedPropertyError* err) override {
    if (err) *err = vr::TrackedProp_UnknownProperty;
    return 0;
  }
  uint64_t GetUint64TrackedDeviceProperty(vr::TrackedDeviceIndex_t,
                                          vr::ETrackedDeviceProperty,
                                          vr::ETrackedPropertyError* err) override {
    if (err) *err = vr::TrackedProp_UnknownProperty;
    return 0;
  }
  vr::HmdMatrix34_t GetMatrix34TrackedDeviceProperty(vr::TrackedDeviceIndex_t,
                                                     vr::ETrackedDeviceProperty,
                                                     vr::ETrackedPropertyError* err) override {
    if (err) *err = vr::TrackedProp_UnknownProperty;
    return posemath::kIdentity34;
  }
  uint32_t GetArrayTrackedDeviceProperty(vr::TrackedDeviceIndex_t,
                                         vr::ETrackedDeviceProperty,
                                         vr::PropertyTypeTag_t,
                                         void*, uint32_t,
                                         vr::ETrackedPropertyError* err) override {
    if (err) *err = vr::TrackedProp_UnknownProperty;
    return 0;
  }
  uint32_t GetStringTrackedDeviceProperty(vr::TrackedDeviceIndex_t idx,
                                          vr::ETrackedDeviceProperty prop,
                                          char* buf, uint32_t bufSize,
                                          vr::ETrackedPropertyError* err) override {
    const char* s = "";
    switch (prop) {
      case vr::Prop_TrackingSystemName_String:    s = "fuvr"; break;
      case vr::Prop_ManufacturerName_String:      s = "Meta"; break;
      case vr::Prop_ModelNumber_String:           s = (idx == 0 ? "Meta Quest" : "Meta Touch"); break;
      case vr::Prop_SerialNumber_String:          s = "FUVR-MOCK-0001"; break;
      case vr::Prop_RenderModelName_String:       s = (idx == 0 ? "generic_hmd" : "oculus_quest_touch"); break;
      case vr::Prop_RegisteredDeviceType_String:  s = "fuvr/quest"; break;
      case vr::Prop_ControllerType_String:        s = "oculus_touch"; break;
      default:
        if (err) *err = vr::TrackedProp_UnknownProperty;
        if (buf && bufSize > 0) buf[0] = '\0';
        return 0;
    }
    uint32_t needed = static_cast<uint32_t>(std::strlen(s) + 1);
    if (buf && bufSize >= needed) {
      std::memcpy(buf, s, needed);
      if (err) *err = vr::TrackedProp_Success;
    } else {
      if (err) *err = vr::TrackedProp_BufferTooSmall;
    }
    return needed;
  }
  const char* GetPropErrorNameFromEnum(vr::ETrackedPropertyError) override {
    return "TrackedProp_Unknown";
  }

  // ---- Events ----

  bool PollNextEvent(vr::VREvent_t*, uint32_t) override { return false; }
  bool PollNextEventWithPose(vr::ETrackingUniverseOrigin, vr::VREvent_t*, uint32_t,
                             vr::TrackedDevicePose_t*) override { return false; }
  bool PollNextEventWithPoseAndOverlays(vr::ETrackingUniverseOrigin, vr::VREvent_t*,
                                        uint32_t, vr::TrackedDevicePose_t*,
                                        vr::VROverlayHandle_t*) override { return false; }
  const char* GetEventTypeNameFromEnum(vr::EVREventType) override { return "VREvent_None"; }

  // ---- Hidden-area mesh / stencil (not used on Quest path) ----
  vr::HiddenAreaMesh_t GetHiddenAreaMesh(vr::EVREye, vr::EHiddenAreaMeshType) override {
    vr::HiddenAreaMesh_t empty{};
    return empty;
  }
  bool GetEyeTrackedFoveationCenter(vr::HmdVector2_t*, vr::HmdVector2_t*) override { return false; }
  bool GetEyeTrackedFoveationCenterForProjection(const vr::HmdMatrix44_t*, vr::HmdVector2_t*) override { return false; }

  // ---- Legacy controller state (modern action API is in IVRInput) ----
  bool GetControllerState(vr::TrackedDeviceIndex_t, vr::VRControllerState_t*, uint32_t) override {
    return false;
  }
  bool GetControllerStateWithPose(vr::ETrackingUniverseOrigin,
                                  vr::TrackedDeviceIndex_t,
                                  vr::VRControllerState_t*, uint32_t,
                                  vr::TrackedDevicePose_t*) override {
    return false;
  }
  void TriggerHapticPulse(vr::TrackedDeviceIndex_t idx, uint32_t /*axisId*/,
                          unsigned short usDurationMicroSec) override {
    // Map device index → controller path. Daemon resolves this against
    // the live OpenXR session.
    uint64_t handle = (idx == 1) ? 1 : (idx == 2 ? 2 : 0);
    MockState::shared().rpc().triggerHaptic(
        handle,
        /*startSecondsFromNow*/ 0.0f,
        /*durationSeconds   */ static_cast<float>(usDurationMicroSec) / 1e6f,
        /*frequency Hz      */ 160.0f,
        /*amplitude         */ 1.0f);
  }
  const char* GetButtonIdNameFromEnum(vr::EVRButtonId) override { return "Button_Unknown"; }
  const char* GetControllerAxisTypeNameFromEnum(vr::EVRControllerAxisType) override { return "Axis_Unknown"; }

  // ---- Misc ----
  bool IsInputAvailable() override { return MockState::shared().isInitialized(); }
  bool IsSteamVRDrawingControllers() override { return false; }
  bool ShouldApplicationPause() override { return false; }
  bool ShouldApplicationReduceRenderingWork() override { return false; }
  vr::EVRFirmwareError PerformFirmwareUpdate(vr::TrackedDeviceIndex_t) override {
    return vr::VRFirmwareError_None;
  }
  void AcknowledgeQuit_Exiting() override {}
  uint32_t GetAppContainerFilePaths(char* buf, uint32_t bufSize) override {
    if (buf && bufSize) buf[0] = '\0';
    return 0;
  }
  const char* GetRuntimeVersion() override { return "fuvr-mock 0.1"; }
  vr::EVRInitError SetSDKVersion(uint32_t, uint32_t, uint32_t) override {
    return vr::VRInitError_None;
  }

 private:
  void fillPosesIntoArray(const PoseSet& src,
                          vr::TrackedDevicePose_t* arr,
                          uint32_t count) {
    auto fill = [](vr::TrackedDevicePose_t& dst, const float p[13], bool valid) {
      dst.bDeviceIsConnected = valid ? true : false;
      dst.bPoseIsValid = valid ? true : false;
      dst.eTrackingResult = valid
          ? vr::TrackingResult_Running_OK
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
};

vr::IVRSystem* mockIVRSystem() {
  static MockIVRSystem instance;
  return &instance;
}

}  // namespace fuvr::openvr_shim
