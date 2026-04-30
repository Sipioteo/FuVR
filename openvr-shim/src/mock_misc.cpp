// SPDX-License-Identifier: Apache-2.0
//
// Stub interfaces for OpenVR features the daemon doesn't model: overlays,
// render models, settings, applications, screenshots, notifications,
// chaperone setup, tracked-camera, resources. We expose them so games
// don't crash on `VR_GetGenericInterface`, but every method returns the
// "feature unavailable" code its enum defines.

#include <cstring>

#include "openvr.h"

namespace fuvr::openvr_shim {

// IVROverlay is intentionally NOT implemented — its vtable surface is
// large (~100 virtuals) and the Valve ABI snapshot evolves often. Most
// legacy SteamVR Mac titles (Vivecraft included) tolerate a missing
// overlay interface and fall back to in-world HUD rendering.
//
// The original class body has been retired; `mockIVROverlay()` now
// returns nullptr, which `VR_GetGenericInterface` propagates as
// `VRInitError_Init_InvalidInterface`. Re-enable this class only if a
// target game proves to need overlays.
#if 0
class MockIVROverlay final : public vr::IVROverlay {
 public:
  vr::EVROverlayError FindOverlay(const char*, vr::VROverlayHandle_t* h) override {
    if (h) *h = vr::k_ulOverlayHandleInvalid;
    return vr::VROverlayError_UnknownOverlay;
  }
  vr::EVROverlayError CreateOverlay(const char*, const char*, vr::VROverlayHandle_t* h) override {
    if (h) *h = vr::k_ulOverlayHandleInvalid;
    return vr::VROverlayError_PermissionDenied;
  }
  vr::EVROverlayError DestroyOverlay(vr::VROverlayHandle_t) override { return vr::VROverlayError_None; }
  uint32_t GetOverlayKey(vr::VROverlayHandle_t, char* buf, uint32_t sz, vr::EVROverlayError* err) override {
    if (err) *err = vr::VROverlayError_InvalidHandle;
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  uint32_t GetOverlayName(vr::VROverlayHandle_t, char* buf, uint32_t sz, vr::EVROverlayError* err) override {
    if (err) *err = vr::VROverlayError_InvalidHandle;
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  vr::EVROverlayError SetOverlayName(vr::VROverlayHandle_t, const char*) override { return vr::VROverlayError_InvalidHandle; }
  vr::EVROverlayError GetOverlayImageData(vr::VROverlayHandle_t, void*, uint32_t, uint32_t*, uint32_t*) override { return vr::VROverlayError_InvalidHandle; }
  const char* GetOverlayErrorNameFromEnum(vr::EVROverlayError) override { return "VROverlayError_Unknown"; }
  vr::EVROverlayError SetOverlayRenderingPid(vr::VROverlayHandle_t, uint32_t) override { return vr::VROverlayError_None; }
  uint32_t GetOverlayRenderingPid(vr::VROverlayHandle_t) override { return 0; }
  vr::EVROverlayError SetOverlayFlag(vr::VROverlayHandle_t, vr::VROverlayFlags, bool) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayFlag(vr::VROverlayHandle_t, vr::VROverlayFlags, bool*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayFlags(vr::VROverlayHandle_t, uint32_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayColor(vr::VROverlayHandle_t, float, float, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayColor(vr::VROverlayHandle_t, float*, float*, float*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayAlpha(vr::VROverlayHandle_t, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayAlpha(vr::VROverlayHandle_t, float*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTexelAspect(vr::VROverlayHandle_t, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTexelAspect(vr::VROverlayHandle_t, float*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlaySortOrder(vr::VROverlayHandle_t, uint32_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlaySortOrder(vr::VROverlayHandle_t, uint32_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayWidthInMeters(vr::VROverlayHandle_t, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayWidthInMeters(vr::VROverlayHandle_t, float*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayCurvature(vr::VROverlayHandle_t, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayCurvature(vr::VROverlayHandle_t, float*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayPreCurvePitch(vr::VROverlayHandle_t, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayPreCurvePitch(vr::VROverlayHandle_t, float*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTextureColorSpace(vr::VROverlayHandle_t, vr::EColorSpace) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTextureColorSpace(vr::VROverlayHandle_t, vr::EColorSpace*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTextureBounds(vr::VROverlayHandle_t, const vr::VRTextureBounds_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTextureBounds(vr::VROverlayHandle_t, vr::VRTextureBounds_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTransformType(vr::VROverlayHandle_t, vr::VROverlayTransformType*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTransformAbsolute(vr::VROverlayHandle_t, vr::ETrackingUniverseOrigin, const vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTransformAbsolute(vr::VROverlayHandle_t, vr::ETrackingUniverseOrigin*, vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTransformTrackedDeviceRelative(vr::VROverlayHandle_t, vr::TrackedDeviceIndex_t, const vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTransformTrackedDeviceRelative(vr::VROverlayHandle_t, vr::TrackedDeviceIndex_t*, vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTransformTrackedDeviceComponent(vr::VROverlayHandle_t, vr::TrackedDeviceIndex_t, const char*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTransformTrackedDeviceComponent(vr::VROverlayHandle_t, vr::TrackedDeviceIndex_t*, char*, uint32_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTransformOverlayRelative(vr::VROverlayHandle_t, vr::VROverlayHandle_t*, vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTransformOverlayRelative(vr::VROverlayHandle_t, vr::VROverlayHandle_t, const vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTransformCursor(vr::VROverlayHandle_t, const vr::HmdVector2_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTransformCursor(vr::VROverlayHandle_t, vr::HmdVector2_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTransformProjection(vr::VROverlayHandle_t, vr::ETrackingUniverseOrigin,
                                                    const vr::HmdMatrix34_t*, const vr::VROverlayProjection_t*, vr::EVREye) override { return vr::VROverlayError_None; }
  vr::EVROverlayError ShowOverlay(vr::VROverlayHandle_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError HideOverlay(vr::VROverlayHandle_t) override { return vr::VROverlayError_None; }
  bool IsOverlayVisible(vr::VROverlayHandle_t) override { return false; }
  vr::EVROverlayError GetTransformForOverlayCoordinates(vr::VROverlayHandle_t, vr::ETrackingUniverseOrigin, vr::HmdVector2_t, vr::HmdMatrix34_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError WaitFrameSync(uint32_t) override { return vr::VROverlayError_None; }
  bool PollNextOverlayEvent(vr::VROverlayHandle_t, vr::VREvent_t*, uint32_t) override { return false; }
  vr::EVROverlayError GetOverlayInputMethod(vr::VROverlayHandle_t, vr::VROverlayInputMethod*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayInputMethod(vr::VROverlayHandle_t, vr::VROverlayInputMethod) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayMouseScale(vr::VROverlayHandle_t, vr::HmdVector2_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayMouseScale(vr::VROverlayHandle_t, const vr::HmdVector2_t*) override { return vr::VROverlayError_None; }
  bool ComputeOverlayIntersection(vr::VROverlayHandle_t, const vr::VROverlayIntersectionParams_t*, vr::VROverlayIntersectionResults_t*) override { return false; }
  bool IsHoverTargetOverlay(vr::VROverlayHandle_t) override { return false; }
  vr::EVROverlayError SetOverlayIntersectionMask(vr::VROverlayHandle_t, vr::VROverlayIntersectionMaskPrimitive_t*, uint32_t, uint32_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError TriggerLaserMouseHapticVibration(vr::VROverlayHandle_t, float, float, float) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayCursor(vr::VROverlayHandle_t, vr::VROverlayHandle_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayCursorPositionOverride(vr::VROverlayHandle_t, const vr::HmdVector2_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError ClearOverlayCursorPositionOverride(vr::VROverlayHandle_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayTexture(vr::VROverlayHandle_t, const vr::Texture_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError ClearOverlayTexture(vr::VROverlayHandle_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayRaw(vr::VROverlayHandle_t, void*, uint32_t, uint32_t, uint32_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError SetOverlayFromFile(vr::VROverlayHandle_t, const char*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTexture(vr::VROverlayHandle_t, void**, void*, uint32_t*, uint32_t*, uint32_t*, vr::ETextureType*, vr::EColorSpace*, vr::VRTextureBounds_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError ReleaseNativeOverlayHandle(vr::VROverlayHandle_t, void*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetOverlayTextureSize(vr::VROverlayHandle_t, uint32_t*, uint32_t*) override { return vr::VROverlayError_None; }
  vr::EVROverlayError CreateDashboardOverlay(const char*, const char*, vr::VROverlayHandle_t*, vr::VROverlayHandle_t*) override { return vr::VROverlayError_None; }
  bool IsDashboardVisible() override { return false; }
  bool IsActiveDashboardOverlay(vr::VROverlayHandle_t) override { return false; }
  vr::EVROverlayError SetDashboardOverlaySceneProcess(vr::VROverlayHandle_t, uint32_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError GetDashboardOverlaySceneProcess(vr::VROverlayHandle_t, uint32_t*) override { return vr::VROverlayError_None; }
  void ShowDashboard(const char*) override {}
  vr::TrackedDeviceIndex_t GetPrimaryDashboardDevice() override { return vr::k_unTrackedDeviceIndexInvalid; }
  vr::EVROverlayError ShowKeyboard(vr::EGamepadTextInputMode, vr::EGamepadTextInputLineMode, uint32_t, const char*, uint32_t, const char*, uint64_t) override { return vr::VROverlayError_None; }
  vr::EVROverlayError ShowKeyboardForOverlay(vr::VROverlayHandle_t, vr::EGamepadTextInputMode, vr::EGamepadTextInputLineMode, uint32_t, const char*, uint32_t, const char*, uint64_t) override { return vr::VROverlayError_None; }
  uint32_t GetKeyboardText(char* buf, uint32_t sz) override { if (buf && sz) buf[0] = '\0'; return 0; }
  void HideKeyboard() override {}
  void SetKeyboardTransformAbsolute(vr::ETrackingUniverseOrigin, const vr::HmdMatrix34_t*) override {}
  void SetKeyboardPositionForOverlay(vr::VROverlayHandle_t, vr::HmdRect2_t) override {}
  vr::EVROverlayError ShowMessageOverlay(const char*, const char*, const char*, const char*, const char*, const char*) override { return vr::VROverlayError_None; }
  void CloseMessageOverlay() override {}
};
#endif  // disabled MockIVROverlay

// ---- IVRSettings ----
class MockIVRSettings final : public vr::IVRSettings {
 public:
  const char* GetSettingsErrorNameFromEnum(vr::EVRSettingsError) override { return "Settings_Unknown"; }
  void SetBool(const char*, const char*, bool, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; }
  void SetInt32(const char*, const char*, int32_t, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; }
  void SetFloat(const char*, const char*, float, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; }
  void SetString(const char*, const char*, const char*, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; }
  bool GetBool(const char*, const char*, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; return false; }
  int32_t GetInt32(const char*, const char*, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; return 0; }
  float GetFloat(const char*, const char*, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; return 0.0f; }
  void GetString(const char*, const char*, char* buf, uint32_t sz, vr::EVRSettingsError* err) override {
    if (buf && sz) buf[0] = '\0';
    if (err) *err = vr::VRSettingsError_None;
  }
  void RemoveSection(const char*, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; }
  void RemoveKeyInSection(const char*, const char*, vr::EVRSettingsError* err) override { if (err) *err = vr::VRSettingsError_None; }
};

// ---- IVRRenderModels (Vivecraft requests render models for controllers; stub safely) ----
class MockIVRRenderModels final : public vr::IVRRenderModels {
 public:
  vr::EVRRenderModelError LoadRenderModel_Async(const char*, vr::RenderModel_t** ppRenderModel) override {
    if (ppRenderModel) *ppRenderModel = nullptr;
    return vr::VRRenderModelError_NotSupported;
  }
  void FreeRenderModel(vr::RenderModel_t*) override {}
  vr::EVRRenderModelError LoadTexture_Async(vr::TextureID_t, vr::RenderModel_TextureMap_t** out) override {
    if (out) *out = nullptr;
    return vr::VRRenderModelError_NotSupported;
  }
  void FreeTexture(vr::RenderModel_TextureMap_t*) override {}
  vr::EVRRenderModelError LoadTextureD3D11_Async(vr::TextureID_t, void*, void**) override { return vr::VRRenderModelError_NotSupported; }
  vr::EVRRenderModelError LoadIntoTextureD3D11_Async(vr::TextureID_t, void*) override { return vr::VRRenderModelError_NotSupported; }
  void FreeTextureD3D11(void*) override {}
  uint32_t GetRenderModelName(uint32_t, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  uint32_t GetRenderModelCount() override { return 0; }
  uint32_t GetComponentCount(const char*) override { return 0; }
  uint32_t GetComponentName(const char*, uint32_t, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  uint64_t GetComponentButtonMask(const char*, const char*) override { return 0; }
  uint32_t GetComponentRenderModelName(const char*, const char*, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  bool GetComponentStateForDevicePath(const char*, const char*, vr::VRInputValueHandle_t,
                                      const vr::RenderModel_ControllerMode_State_t*,
                                      vr::RenderModel_ComponentState_t*) override { return false; }
  bool GetComponentState(const char*, const char*, const vr::VRControllerState_t*,
                         const vr::RenderModel_ControllerMode_State_t*,
                         vr::RenderModel_ComponentState_t*) override { return false; }
  bool RenderModelHasComponent(const char*, const char*) override { return false; }
  uint32_t GetRenderModelThumbnailURL(const char*, char* buf, uint32_t sz, vr::EVRRenderModelError* err) override {
    if (buf && sz) buf[0] = '\0';
    if (err) *err = vr::VRRenderModelError_None;
    return 0;
  }
  uint32_t GetRenderModelOriginalPath(const char*, char* buf, uint32_t sz, vr::EVRRenderModelError* err) override {
    if (buf && sz) buf[0] = '\0';
    if (err) *err = vr::VRRenderModelError_None;
    return 0;
  }
  const char* GetRenderModelErrorNameFromEnum(vr::EVRRenderModelError) override { return "RenderModel_Unknown"; }
};

// IVRApplications: Vivecraft pulls this interface to identify itself
// (`IdentifyApplication`) and probe install state. We stub the entire
// surface — none of the side effects matter for the streaming use case.
class MockIVRApplications final : public vr::IVRApplications {
 public:
  vr::EVRApplicationError AddApplicationManifest(const char*, bool) override { return vr::VRApplicationError_None; }
  vr::EVRApplicationError RemoveApplicationManifest(const char*) override { return vr::VRApplicationError_None; }
  bool IsApplicationInstalled(const char*) override { return false; }
  uint32_t GetApplicationCount() override { return 0; }
  vr::EVRApplicationError GetApplicationKeyByIndex(uint32_t, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return vr::VRApplicationError_InvalidIndex;
  }
  vr::EVRApplicationError GetApplicationKeyByProcessId(uint32_t, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return vr::VRApplicationError_NotImplemented;
  }
  vr::EVRApplicationError LaunchApplication(const char*) override { return vr::VRApplicationError_NotImplemented; }
  vr::EVRApplicationError LaunchTemplateApplication(const char*, const char*, const vr::AppOverrideKeys_t*, uint32_t) override { return vr::VRApplicationError_NotImplemented; }
  vr::EVRApplicationError LaunchApplicationFromMimeType(const char*, const char*) override { return vr::VRApplicationError_NotImplemented; }
  vr::EVRApplicationError LaunchDashboardOverlay(const char*) override { return vr::VRApplicationError_NotImplemented; }
  bool CancelApplicationLaunch(const char*) override { return false; }
  vr::EVRApplicationError IdentifyApplication(uint32_t, const char*) override { return vr::VRApplicationError_None; }
  uint32_t GetApplicationProcessId(const char*) override { return 0; }
  const char* GetApplicationsErrorNameFromEnum(vr::EVRApplicationError) override { return "VRApplicationError_None"; }
  uint32_t GetApplicationPropertyString(const char*, vr::EVRApplicationProperty, char* buf, uint32_t sz, vr::EVRApplicationError* err) override {
    if (buf && sz) buf[0] = '\0';
    if (err) *err = vr::VRApplicationError_PropertyNotSet;
    return 0;
  }
  bool GetApplicationPropertyBool(const char*, vr::EVRApplicationProperty, vr::EVRApplicationError* err) override {
    if (err) *err = vr::VRApplicationError_PropertyNotSet;
    return false;
  }
  uint64_t GetApplicationPropertyUint64(const char*, vr::EVRApplicationProperty, vr::EVRApplicationError* err) override {
    if (err) *err = vr::VRApplicationError_PropertyNotSet;
    return 0;
  }
  vr::EVRApplicationError SetApplicationAutoLaunch(const char*, bool) override { return vr::VRApplicationError_NotImplemented; }
  bool GetApplicationAutoLaunch(const char*) override { return false; }
  vr::EVRApplicationError SetDefaultApplicationForMimeType(const char*, const char*) override { return vr::VRApplicationError_NotImplemented; }
  bool GetDefaultApplicationForMimeType(const char*, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return false;
  }
  bool GetApplicationSupportedMimeTypes(const char*, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return false;
  }
  uint32_t GetApplicationsThatSupportMimeType(const char*, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  uint32_t GetApplicationLaunchArguments(uint32_t, char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return 0;
  }
  vr::EVRApplicationError GetStartingApplication(char* buf, uint32_t sz) override {
    if (buf && sz) buf[0] = '\0';
    return vr::VRApplicationError_NotImplemented;
  }
  vr::EVRSceneApplicationState GetSceneApplicationState() override {
    return vr::EVRSceneApplicationState_Running;
  }
  vr::EVRApplicationError PerformApplicationPrelaunchCheck(const char*) override { return vr::VRApplicationError_None; }
  const char* GetSceneApplicationStateNameFromEnum(vr::EVRSceneApplicationState) override { return "Running"; }
  vr::EVRApplicationError LaunchInternalProcess(const char*, const char*, const char*) override { return vr::VRApplicationError_NotImplemented; }
  vr::EVRApplicationError RegisterSubprocess(uint32_t) override { return vr::VRApplicationError_None; }
  uint32_t GetCurrentSceneProcessId() override { return 0; }
};

vr::IVROverlay*       mockIVROverlay()       { return nullptr; }
vr::IVRSettings*      mockIVRSettings()      { static MockIVRSettings i;      return &i; }
vr::IVRRenderModels*  mockIVRRenderModels()  { static MockIVRRenderModels i;  return &i; }
vr::IVRApplications*  mockIVRApplications()  { static MockIVRApplications i;  return &i; }

}  // namespace fuvr::openvr_shim
