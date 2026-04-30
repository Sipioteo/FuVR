// SPDX-License-Identifier: Apache-2.0
//
// LWJGL's OpenVR bindings request interfaces with a "FnTable:" prefix and
// expect a flat C struct of function pointers (no implicit `this`). The
// C++ vtable layout doesn't match — methods carry an implicit `this` —
// so we provide hand-written extern "C" thunks that forward to the
// singleton `MockIVRChaperone`, packed in declaration order.

#include "openvr.h"

namespace fuvr::openvr_shim {
vr::IVRChaperone* mockIVRChaperone();
}

extern "C" {

static vr::ChaperoneCalibrationState fnt_Chaperone_GetCalibrationState() {
  return fuvr::openvr_shim::mockIVRChaperone()->GetCalibrationState();
}

static bool fnt_Chaperone_GetPlayAreaSize(float* pSizeX, float* pSizeZ) {
  return fuvr::openvr_shim::mockIVRChaperone()->GetPlayAreaSize(pSizeX, pSizeZ);
}

static bool fnt_Chaperone_GetPlayAreaRect(vr::HmdQuad_t* rect) {
  return fuvr::openvr_shim::mockIVRChaperone()->GetPlayAreaRect(rect);
}

static void fnt_Chaperone_ReloadInfo() {
  fuvr::openvr_shim::mockIVRChaperone()->ReloadInfo();
}

static void fnt_Chaperone_SetSceneColor(vr::HmdColor_t color) {
  fuvr::openvr_shim::mockIVRChaperone()->SetSceneColor(color);
}

static void fnt_Chaperone_GetBoundsColor(vr::HmdColor_t* pOutputColorArray,
                                         int nNumOutputColors,
                                         float flCollisionBoundsFadeDistance,
                                         vr::HmdColor_t* pOutputCameraColor) {
  fuvr::openvr_shim::mockIVRChaperone()->GetBoundsColor(
      pOutputColorArray, nNumOutputColors, flCollisionBoundsFadeDistance,
      pOutputCameraColor);
}

static bool fnt_Chaperone_AreBoundsVisible() {
  return fuvr::openvr_shim::mockIVRChaperone()->AreBoundsVisible();
}

static void fnt_Chaperone_ForceBoundsVisible(bool bForce) {
  fuvr::openvr_shim::mockIVRChaperone()->ForceBoundsVisible(bForce);
}

static void fnt_Chaperone_ResetZeroPose(vr::ETrackingUniverseOrigin eTrackingUniverseOrigin) {
  fuvr::openvr_shim::mockIVRChaperone()->ResetZeroPose(eTrackingUniverseOrigin);
}

}  // extern "C"

namespace fuvr::openvr_shim {
namespace {

struct ChaperoneFnTable {
  void* GetCalibrationState;
  void* GetPlayAreaSize;
  void* GetPlayAreaRect;
  void* ReloadInfo;
  void* SetSceneColor;
  void* GetBoundsColor;
  void* AreBoundsVisible;
  void* ForceBoundsVisible;
  void* ResetZeroPose;
};

static const ChaperoneFnTable g_chaperone_fntable = {
    reinterpret_cast<void*>(&fnt_Chaperone_GetCalibrationState),
    reinterpret_cast<void*>(&fnt_Chaperone_GetPlayAreaSize),
    reinterpret_cast<void*>(&fnt_Chaperone_GetPlayAreaRect),
    reinterpret_cast<void*>(&fnt_Chaperone_ReloadInfo),
    reinterpret_cast<void*>(&fnt_Chaperone_SetSceneColor),
    reinterpret_cast<void*>(&fnt_Chaperone_GetBoundsColor),
    reinterpret_cast<void*>(&fnt_Chaperone_AreBoundsVisible),
    reinterpret_cast<void*>(&fnt_Chaperone_ForceBoundsVisible),
    reinterpret_cast<void*>(&fnt_Chaperone_ResetZeroPose),
};

}  // namespace

void* chaperoneFnTable() {
  return const_cast<void*>(static_cast<const void*>(&g_chaperone_fntable));
}

}  // namespace fuvr::openvr_shim
