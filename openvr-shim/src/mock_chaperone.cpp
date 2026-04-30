// SPDX-License-Identifier: Apache-2.0
//
// MockIVRChaperone — minimal play-area shim. Vivecraft polls
// `GetPlayAreaSize` on init to size the world; we report a 2×2m default
// matching the daemon's typical guardian config. The full chaperone
// surface (bounds visibility, fade controls) is stubbed.

#include "openvr.h"

#include "pose_math.hpp"

namespace fuvr::openvr_shim {

class MockIVRChaperone final : public vr::IVRChaperone {
 public:
  vr::ChaperoneCalibrationState GetCalibrationState() override {
    return vr::ChaperoneCalibrationState_OK;
  }
  bool GetPlayAreaSize(float* sizeX, float* sizeZ) override {
    if (sizeX) *sizeX = 2.0f;
    if (sizeZ) *sizeZ = 2.0f;
    return true;
  }
  bool GetPlayAreaRect(vr::HmdQuad_t* rect) override {
    if (!rect) return false;
    // Square corners centred at origin in tracking space.
    rect->vCorners[0] = {-1.0f, 0.0f, -1.0f};
    rect->vCorners[1] = { 1.0f, 0.0f, -1.0f};
    rect->vCorners[2] = { 1.0f, 0.0f,  1.0f};
    rect->vCorners[3] = {-1.0f, 0.0f,  1.0f};
    return true;
  }
  void ReloadInfo() override {}
  void SetSceneColor(vr::HmdColor_t) override {}
  void GetBoundsColor(vr::HmdColor_t*, int, float, vr::HmdColor_t*) override {}
  bool AreBoundsVisible() override { return false; }
  void ForceBoundsVisible(bool) override {}
  void ResetZeroPose(vr::ETrackingUniverseOrigin) override {}
};

vr::IVRChaperone* mockIVRChaperone() {
  static MockIVRChaperone instance;
  return &instance;
}

}  // namespace fuvr::openvr_shim
