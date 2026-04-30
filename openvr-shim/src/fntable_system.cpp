// SPDX-License-Identifier: Apache-2.0
//
// LWJGL-style FnTable for IVRSystem. LWJGL expects a flat struct of plain
// C function pointers — not a C++ vtable. We wrap each virtual method on
// `vr::IVRSystem` with an `extern "C"` thunk that calls the existing
// singleton.
//
// SLOT ORDER CONTRACT
// -------------------
// The FnTable layout is whatever the *consumer's* generated bindings
// expect, NOT whatever our local `openvr.h` happens to declare. Our
// consumer is `org.lwjgl:lwjgl-openvr:3.3.2`, which was code-generated
// against OpenVR SDK v1.23.7 (IVRSystem_022, 46 virtual methods).
//
// Our `third_party/openvr/openvr.h` is a newer (post-v2.0) Valve header
// with several extra virtuals interleaved into IVRSystem:
//   - ComputeDistortionSet           (after  ComputeDistortion)
//   - PollNextEventWithPoseAndOverlays (after PollNextEventWithPose)
//   - GetEyeTrackedFoveationCenter      (after GetHiddenAreaMesh)
//   - GetEyeTrackedFoveationCenterForProjection
//   - SetSDKVersion                  (last)
// If we exposed those slots, LWJGL would call slot N expecting method N
// from v1.23.7 and instead reach the wrong thunk. Concretely, Vivecraft's
// first VRSystem_GetStringTrackedDeviceProperty (LWJGL slot 27) was
// landing on our GetArrayTrackedDeviceProperty thunk because every slot
// past ComputeDistortion was shifted by +1, +2, +3, +4 cumulatively —
// SIGSEGV at +0x34 in MockIVRSystem::GetArrayTrackedDeviceProperty.
//
// The fix: keep the C++ thunks one-per-method (cheap, harmless for unused
// ones) but lay out the EXPORTED FnTable as exactly the 46 v1.23.7 slots
// in IVRSystem_022 declaration order. The extra thunks above are simply
// not referenced from the table.

#include "openvr.h"

namespace fuvr::openvr_shim {
vr::IVRSystem* mockIVRSystem();
}

extern "C" {

static void fnt_System_GetRecommendedRenderTargetSize(uint32_t* pnWidth, uint32_t* pnHeight) {
  fuvr::openvr_shim::mockIVRSystem()->GetRecommendedRenderTargetSize(pnWidth, pnHeight);
}

static vr::HmdMatrix44_t fnt_System_GetProjectionMatrix(vr::EVREye eEye, float fNearZ, float fFarZ) {
  return fuvr::openvr_shim::mockIVRSystem()->GetProjectionMatrix(eEye, fNearZ, fFarZ);
}

static void fnt_System_GetProjectionRaw(vr::EVREye eEye, float* pfLeft, float* pfRight, float* pfTop, float* pfBottom) {
  fuvr::openvr_shim::mockIVRSystem()->GetProjectionRaw(eEye, pfLeft, pfRight, pfTop, pfBottom);
}

static bool fnt_System_ComputeDistortion(vr::EVREye eEye, float fU, float fV, vr::DistortionCoordinates_t* pDistortionCoordinates) {
  return fuvr::openvr_shim::mockIVRSystem()->ComputeDistortion(eEye, fU, fV, pDistortionCoordinates);
}

static bool fnt_System_ComputeDistortionSet(vr::EVREye eEye, vr::EVRDistortionChannel eChannel, bool bAsNormalizedDeviceCoordinates,
                                            uint32_t nNumCoordinates, const vr::DistortionCoordinate_t* pInput, vr::DistortionCoordinate_t* pOutput) {
  return fuvr::openvr_shim::mockIVRSystem()->ComputeDistortionSet(eEye, eChannel, bAsNormalizedDeviceCoordinates,
                                                                  nNumCoordinates, pInput, pOutput);
}

static vr::HmdMatrix34_t fnt_System_GetEyeToHeadTransform(vr::EVREye eEye) {
  return fuvr::openvr_shim::mockIVRSystem()->GetEyeToHeadTransform(eEye);
}

static bool fnt_System_GetTimeSinceLastVsync(float* pfSecondsSinceLastVsync, uint64_t* pulFrameCounter) {
  return fuvr::openvr_shim::mockIVRSystem()->GetTimeSinceLastVsync(pfSecondsSinceLastVsync, pulFrameCounter);
}

static int32_t fnt_System_GetD3D9AdapterIndex() {
  return fuvr::openvr_shim::mockIVRSystem()->GetD3D9AdapterIndex();
}

static void fnt_System_GetDXGIOutputInfo(int32_t* pnAdapterIndex) {
  fuvr::openvr_shim::mockIVRSystem()->GetDXGIOutputInfo(pnAdapterIndex);
}

static void fnt_System_GetOutputDevice(uint64_t* pnDevice, vr::ETextureType textureType, struct VkInstance_T* pInstance) {
  fuvr::openvr_shim::mockIVRSystem()->GetOutputDevice(pnDevice, textureType, pInstance);
}

static bool fnt_System_IsDisplayOnDesktop() {
  return fuvr::openvr_shim::mockIVRSystem()->IsDisplayOnDesktop();
}

static bool fnt_System_SetDisplayVisibility(bool bIsVisibleOnDesktop) {
  return fuvr::openvr_shim::mockIVRSystem()->SetDisplayVisibility(bIsVisibleOnDesktop);
}

static void fnt_System_GetDeviceToAbsoluteTrackingPose(vr::ETrackingUniverseOrigin eOrigin, float fPredictedSecondsToPhotonsFromNow,
                                                       vr::TrackedDevicePose_t* pTrackedDevicePoseArray, uint32_t unTrackedDevicePoseArrayCount) {
  fuvr::openvr_shim::mockIVRSystem()->GetDeviceToAbsoluteTrackingPose(eOrigin, fPredictedSecondsToPhotonsFromNow,
                                                                      pTrackedDevicePoseArray, unTrackedDevicePoseArrayCount);
}

static vr::HmdMatrix34_t fnt_System_GetSeatedZeroPoseToStandingAbsoluteTrackingPose() {
  return fuvr::openvr_shim::mockIVRSystem()->GetSeatedZeroPoseToStandingAbsoluteTrackingPose();
}

static vr::HmdMatrix34_t fnt_System_GetRawZeroPoseToStandingAbsoluteTrackingPose() {
  return fuvr::openvr_shim::mockIVRSystem()->GetRawZeroPoseToStandingAbsoluteTrackingPose();
}

static uint32_t fnt_System_GetSortedTrackedDeviceIndicesOfClass(vr::ETrackedDeviceClass eTrackedDeviceClass,
                                                                vr::TrackedDeviceIndex_t* punTrackedDeviceIndexArray,
                                                                uint32_t unTrackedDeviceIndexArrayCount,
                                                                vr::TrackedDeviceIndex_t unRelativeToTrackedDeviceIndex) {
  return fuvr::openvr_shim::mockIVRSystem()->GetSortedTrackedDeviceIndicesOfClass(eTrackedDeviceClass, punTrackedDeviceIndexArray,
                                                                                  unTrackedDeviceIndexArrayCount, unRelativeToTrackedDeviceIndex);
}

static vr::EDeviceActivityLevel fnt_System_GetTrackedDeviceActivityLevel(vr::TrackedDeviceIndex_t unDeviceId) {
  return fuvr::openvr_shim::mockIVRSystem()->GetTrackedDeviceActivityLevel(unDeviceId);
}

static void fnt_System_ApplyTransform(vr::TrackedDevicePose_t* pOutputPose, const vr::TrackedDevicePose_t* pTrackedDevicePose,
                                      const vr::HmdMatrix34_t* pTransform) {
  fuvr::openvr_shim::mockIVRSystem()->ApplyTransform(pOutputPose, pTrackedDevicePose, pTransform);
}

static vr::TrackedDeviceIndex_t fnt_System_GetTrackedDeviceIndexForControllerRole(vr::ETrackedControllerRole unDeviceType) {
  return fuvr::openvr_shim::mockIVRSystem()->GetTrackedDeviceIndexForControllerRole(unDeviceType);
}

static vr::ETrackedControllerRole fnt_System_GetControllerRoleForTrackedDeviceIndex(vr::TrackedDeviceIndex_t unDeviceIndex) {
  return fuvr::openvr_shim::mockIVRSystem()->GetControllerRoleForTrackedDeviceIndex(unDeviceIndex);
}

static vr::ETrackedDeviceClass fnt_System_GetTrackedDeviceClass(vr::TrackedDeviceIndex_t unDeviceIndex) {
  return fuvr::openvr_shim::mockIVRSystem()->GetTrackedDeviceClass(unDeviceIndex);
}

static bool fnt_System_IsTrackedDeviceConnected(vr::TrackedDeviceIndex_t unDeviceIndex) {
  return fuvr::openvr_shim::mockIVRSystem()->IsTrackedDeviceConnected(unDeviceIndex);
}

static bool fnt_System_GetBoolTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                    vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetBoolTrackedDeviceProperty(unDeviceIndex, prop, pError);
}

static float fnt_System_GetFloatTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                      vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetFloatTrackedDeviceProperty(unDeviceIndex, prop, pError);
}

static int32_t fnt_System_GetInt32TrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                        vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetInt32TrackedDeviceProperty(unDeviceIndex, prop, pError);
}

static uint64_t fnt_System_GetUint64TrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                          vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetUint64TrackedDeviceProperty(unDeviceIndex, prop, pError);
}

static vr::HmdMatrix34_t fnt_System_GetMatrix34TrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                                     vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetMatrix34TrackedDeviceProperty(unDeviceIndex, prop, pError);
}

static uint32_t fnt_System_GetArrayTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                         vr::PropertyTypeTag_t propType, void* pBuffer, uint32_t unBufferSize,
                                                         vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetArrayTrackedDeviceProperty(unDeviceIndex, prop, propType, pBuffer, unBufferSize, pError);
}

static uint32_t fnt_System_GetStringTrackedDeviceProperty(vr::TrackedDeviceIndex_t unDeviceIndex, vr::ETrackedDeviceProperty prop,
                                                          char* pchValue, uint32_t unBufferSize, vr::ETrackedPropertyError* pError) {
  return fuvr::openvr_shim::mockIVRSystem()->GetStringTrackedDeviceProperty(unDeviceIndex, prop, pchValue, unBufferSize, pError);
}

static const char* fnt_System_GetPropErrorNameFromEnum(vr::ETrackedPropertyError error) {
  return fuvr::openvr_shim::mockIVRSystem()->GetPropErrorNameFromEnum(error);
}

static bool fnt_System_PollNextEvent(vr::VREvent_t* pEvent, uint32_t uncbVREvent) {
  return fuvr::openvr_shim::mockIVRSystem()->PollNextEvent(pEvent, uncbVREvent);
}

static bool fnt_System_PollNextEventWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, uint32_t uncbVREvent,
                                             vr::TrackedDevicePose_t* pTrackedDevicePose) {
  return fuvr::openvr_shim::mockIVRSystem()->PollNextEventWithPose(eOrigin, pEvent, uncbVREvent, pTrackedDevicePose);
}

static bool fnt_System_PollNextEventWithPoseAndOverlays(vr::ETrackingUniverseOrigin eOrigin, vr::VREvent_t* pEvent, uint32_t uncbVREvent,
                                                        vr::TrackedDevicePose_t* pTrackedDevicePose, vr::VROverlayHandle_t* pulOverlayHandle) {
  return fuvr::openvr_shim::mockIVRSystem()->PollNextEventWithPoseAndOverlays(eOrigin, pEvent, uncbVREvent, pTrackedDevicePose, pulOverlayHandle);
}

static const char* fnt_System_GetEventTypeNameFromEnum(vr::EVREventType eType) {
  return fuvr::openvr_shim::mockIVRSystem()->GetEventTypeNameFromEnum(eType);
}

static vr::HiddenAreaMesh_t fnt_System_GetHiddenAreaMesh(vr::EVREye eEye, vr::EHiddenAreaMeshType type) {
  return fuvr::openvr_shim::mockIVRSystem()->GetHiddenAreaMesh(eEye, type);
}

static bool fnt_System_GetEyeTrackedFoveationCenter(vr::HmdVector2_t* pNdcLeft, vr::HmdVector2_t* pNdcRight) {
  return fuvr::openvr_shim::mockIVRSystem()->GetEyeTrackedFoveationCenter(pNdcLeft, pNdcRight);
}

static bool fnt_System_GetEyeTrackedFoveationCenterForProjection(const vr::HmdMatrix44_t* pProjMat, vr::HmdVector2_t* pNdc) {
  return fuvr::openvr_shim::mockIVRSystem()->GetEyeTrackedFoveationCenterForProjection(pProjMat, pNdc);
}

static bool fnt_System_GetControllerState(vr::TrackedDeviceIndex_t unControllerDeviceIndex, vr::VRControllerState_t* pControllerState,
                                          uint32_t unControllerStateSize) {
  return fuvr::openvr_shim::mockIVRSystem()->GetControllerState(unControllerDeviceIndex, pControllerState, unControllerStateSize);
}

static bool fnt_System_GetControllerStateWithPose(vr::ETrackingUniverseOrigin eOrigin, vr::TrackedDeviceIndex_t unControllerDeviceIndex,
                                                  vr::VRControllerState_t* pControllerState, uint32_t unControllerStateSize,
                                                  vr::TrackedDevicePose_t* pTrackedDevicePose) {
  return fuvr::openvr_shim::mockIVRSystem()->GetControllerStateWithPose(eOrigin, unControllerDeviceIndex, pControllerState,
                                                                        unControllerStateSize, pTrackedDevicePose);
}

static void fnt_System_TriggerHapticPulse(vr::TrackedDeviceIndex_t unControllerDeviceIndex, uint32_t unAxisId, unsigned short usDurationMicroSec) {
  fuvr::openvr_shim::mockIVRSystem()->TriggerHapticPulse(unControllerDeviceIndex, unAxisId, usDurationMicroSec);
}

static const char* fnt_System_GetButtonIdNameFromEnum(vr::EVRButtonId eButtonId) {
  return fuvr::openvr_shim::mockIVRSystem()->GetButtonIdNameFromEnum(eButtonId);
}

static const char* fnt_System_GetControllerAxisTypeNameFromEnum(vr::EVRControllerAxisType eAxisType) {
  return fuvr::openvr_shim::mockIVRSystem()->GetControllerAxisTypeNameFromEnum(eAxisType);
}

static bool fnt_System_IsInputAvailable() {
  return fuvr::openvr_shim::mockIVRSystem()->IsInputAvailable();
}

static bool fnt_System_IsSteamVRDrawingControllers() {
  return fuvr::openvr_shim::mockIVRSystem()->IsSteamVRDrawingControllers();
}

static bool fnt_System_ShouldApplicationPause() {
  return fuvr::openvr_shim::mockIVRSystem()->ShouldApplicationPause();
}

static bool fnt_System_ShouldApplicationReduceRenderingWork() {
  return fuvr::openvr_shim::mockIVRSystem()->ShouldApplicationReduceRenderingWork();
}

static vr::EVRFirmwareError fnt_System_PerformFirmwareUpdate(vr::TrackedDeviceIndex_t unDeviceIndex) {
  return fuvr::openvr_shim::mockIVRSystem()->PerformFirmwareUpdate(unDeviceIndex);
}

static void fnt_System_AcknowledgeQuit_Exiting() {
  fuvr::openvr_shim::mockIVRSystem()->AcknowledgeQuit_Exiting();
}

static uint32_t fnt_System_GetAppContainerFilePaths(char* pchBuffer, uint32_t unBufferSize) {
  return fuvr::openvr_shim::mockIVRSystem()->GetAppContainerFilePaths(pchBuffer, unBufferSize);
}

static const char* fnt_System_GetRuntimeVersion() {
  return fuvr::openvr_shim::mockIVRSystem()->GetRuntimeVersion();
}

static vr::EVRInitError fnt_System_SetSDKVersion(uint32_t nVersionMajor, uint32_t nVersionMinor, uint32_t nVersionBuild) {
  return fuvr::openvr_shim::mockIVRSystem()->SetSDKVersion(nVersionMajor, nVersionMinor, nVersionBuild);
}

}  // extern "C"

namespace fuvr::openvr_shim {
namespace {
// IVRSystem_022 (OpenVR SDK v1.23.7) — 46 slots, the layout LWJGL 3.3.2
// generated against. DO NOT add, remove, or reorder entries without
// regenerating the LWJGL binding to match.
struct SystemFnTable {
  void* GetRecommendedRenderTargetSize;          // slot  0
  void* GetProjectionMatrix;                     // slot  1
  void* GetProjectionRaw;                        // slot  2
  void* ComputeDistortion;                       // slot  3
  void* GetEyeToHeadTransform;                   // slot  4
  void* GetTimeSinceLastVsync;                   // slot  5
  void* GetD3D9AdapterIndex;                     // slot  6
  void* GetDXGIOutputInfo;                       // slot  7
  void* GetOutputDevice;                         // slot  8
  void* IsDisplayOnDesktop;                      // slot  9
  void* SetDisplayVisibility;                    // slot 10
  void* GetDeviceToAbsoluteTrackingPose;         // slot 11
  void* GetSeatedZeroPoseToStandingAbsoluteTrackingPose; // 12
  void* GetRawZeroPoseToStandingAbsoluteTrackingPose;    // 13
  void* GetSortedTrackedDeviceIndicesOfClass;    // slot 14
  void* GetTrackedDeviceActivityLevel;           // slot 15
  void* ApplyTransform;                          // slot 16
  void* GetTrackedDeviceIndexForControllerRole;  // slot 17
  void* GetControllerRoleForTrackedDeviceIndex;  // slot 18
  void* GetTrackedDeviceClass;                   // slot 19
  void* IsTrackedDeviceConnected;                // slot 20
  void* GetBoolTrackedDeviceProperty;            // slot 21
  void* GetFloatTrackedDeviceProperty;           // slot 22
  void* GetInt32TrackedDeviceProperty;           // slot 23
  void* GetUint64TrackedDeviceProperty;          // slot 24
  void* GetMatrix34TrackedDeviceProperty;        // slot 25
  void* GetArrayTrackedDeviceProperty;           // slot 26
  void* GetStringTrackedDeviceProperty;          // slot 27
  void* GetPropErrorNameFromEnum;                // slot 28
  void* PollNextEvent;                           // slot 29
  void* PollNextEventWithPose;                   // slot 30
  void* GetEventTypeNameFromEnum;                // slot 31
  void* GetHiddenAreaMesh;                       // slot 32
  void* GetControllerState;                      // slot 33
  void* GetControllerStateWithPose;              // slot 34
  void* TriggerHapticPulse;                      // slot 35
  void* GetButtonIdNameFromEnum;                 // slot 36
  void* GetControllerAxisTypeNameFromEnum;       // slot 37
  void* IsInputAvailable;                        // slot 38
  void* IsSteamVRDrawingControllers;             // slot 39
  void* ShouldApplicationPause;                  // slot 40
  void* ShouldApplicationReduceRenderingWork;    // slot 41
  void* PerformFirmwareUpdate;                   // slot 42
  void* AcknowledgeQuit_Exiting;                 // slot 43
  void* GetAppContainerFilePaths;                // slot 44
  void* GetRuntimeVersion;                       // slot 45
};

static const SystemFnTable g_system_fntable = {
  reinterpret_cast<void*>(&fnt_System_GetRecommendedRenderTargetSize),
  reinterpret_cast<void*>(&fnt_System_GetProjectionMatrix),
  reinterpret_cast<void*>(&fnt_System_GetProjectionRaw),
  reinterpret_cast<void*>(&fnt_System_ComputeDistortion),
  reinterpret_cast<void*>(&fnt_System_GetEyeToHeadTransform),
  reinterpret_cast<void*>(&fnt_System_GetTimeSinceLastVsync),
  reinterpret_cast<void*>(&fnt_System_GetD3D9AdapterIndex),
  reinterpret_cast<void*>(&fnt_System_GetDXGIOutputInfo),
  reinterpret_cast<void*>(&fnt_System_GetOutputDevice),
  reinterpret_cast<void*>(&fnt_System_IsDisplayOnDesktop),
  reinterpret_cast<void*>(&fnt_System_SetDisplayVisibility),
  reinterpret_cast<void*>(&fnt_System_GetDeviceToAbsoluteTrackingPose),
  reinterpret_cast<void*>(&fnt_System_GetSeatedZeroPoseToStandingAbsoluteTrackingPose),
  reinterpret_cast<void*>(&fnt_System_GetRawZeroPoseToStandingAbsoluteTrackingPose),
  reinterpret_cast<void*>(&fnt_System_GetSortedTrackedDeviceIndicesOfClass),
  reinterpret_cast<void*>(&fnt_System_GetTrackedDeviceActivityLevel),
  reinterpret_cast<void*>(&fnt_System_ApplyTransform),
  reinterpret_cast<void*>(&fnt_System_GetTrackedDeviceIndexForControllerRole),
  reinterpret_cast<void*>(&fnt_System_GetControllerRoleForTrackedDeviceIndex),
  reinterpret_cast<void*>(&fnt_System_GetTrackedDeviceClass),
  reinterpret_cast<void*>(&fnt_System_IsTrackedDeviceConnected),
  reinterpret_cast<void*>(&fnt_System_GetBoolTrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetFloatTrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetInt32TrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetUint64TrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetMatrix34TrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetArrayTrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetStringTrackedDeviceProperty),
  reinterpret_cast<void*>(&fnt_System_GetPropErrorNameFromEnum),
  reinterpret_cast<void*>(&fnt_System_PollNextEvent),
  reinterpret_cast<void*>(&fnt_System_PollNextEventWithPose),
  reinterpret_cast<void*>(&fnt_System_GetEventTypeNameFromEnum),
  reinterpret_cast<void*>(&fnt_System_GetHiddenAreaMesh),
  reinterpret_cast<void*>(&fnt_System_GetControllerState),
  reinterpret_cast<void*>(&fnt_System_GetControllerStateWithPose),
  reinterpret_cast<void*>(&fnt_System_TriggerHapticPulse),
  reinterpret_cast<void*>(&fnt_System_GetButtonIdNameFromEnum),
  reinterpret_cast<void*>(&fnt_System_GetControllerAxisTypeNameFromEnum),
  reinterpret_cast<void*>(&fnt_System_IsInputAvailable),
  reinterpret_cast<void*>(&fnt_System_IsSteamVRDrawingControllers),
  reinterpret_cast<void*>(&fnt_System_ShouldApplicationPause),
  reinterpret_cast<void*>(&fnt_System_ShouldApplicationReduceRenderingWork),
  reinterpret_cast<void*>(&fnt_System_PerformFirmwareUpdate),
  reinterpret_cast<void*>(&fnt_System_AcknowledgeQuit_Exiting),
  reinterpret_cast<void*>(&fnt_System_GetAppContainerFilePaths),
  reinterpret_cast<void*>(&fnt_System_GetRuntimeVersion),
};
}  // namespace

void* systemFnTable() { return const_cast<SystemFnTable*>(&g_system_fntable); }

}  // namespace fuvr::openvr_shim
