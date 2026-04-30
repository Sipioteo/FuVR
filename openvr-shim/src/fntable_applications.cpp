// SPDX-License-Identifier: Apache-2.0
//
// Flat C function-pointer table for IVRApplications. LWJGL's OpenVR bindings
// request "FnTable:IVRApplications_<n>" and expect a struct of plain C
// function pointers (no implicit `this`). C++ vtables don't satisfy that ABI,
// so we synthesize a parallel table of extern "C" thunks that forward into
// the singleton mock implementation.

#include "openvr.h"

namespace fuvr::openvr_shim {
vr::IVRApplications* mockIVRApplications();
}

extern "C" {

static vr::EVRApplicationError fnt_Applications_AddApplicationManifest(const char* pchApplicationManifestFullPath, bool bTemporary) {
  return fuvr::openvr_shim::mockIVRApplications()->AddApplicationManifest(pchApplicationManifestFullPath, bTemporary);
}

static vr::EVRApplicationError fnt_Applications_RemoveApplicationManifest(const char* pchApplicationManifestFullPath) {
  return fuvr::openvr_shim::mockIVRApplications()->RemoveApplicationManifest(pchApplicationManifestFullPath);
}

static bool fnt_Applications_IsApplicationInstalled(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->IsApplicationInstalled(pchAppKey);
}

static uint32_t fnt_Applications_GetApplicationCount() {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationCount();
}

static vr::EVRApplicationError fnt_Applications_GetApplicationKeyByIndex(uint32_t unApplicationIndex, char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationKeyByIndex(unApplicationIndex, pchAppKeyBuffer, unAppKeyBufferLen);
}

static vr::EVRApplicationError fnt_Applications_GetApplicationKeyByProcessId(uint32_t unProcessId, char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationKeyByProcessId(unProcessId, pchAppKeyBuffer, unAppKeyBufferLen);
}

static vr::EVRApplicationError fnt_Applications_LaunchApplication(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->LaunchApplication(pchAppKey);
}

static vr::EVRApplicationError fnt_Applications_LaunchTemplateApplication(const char* pchTemplateAppKey, const char* pchNewAppKey, const vr::AppOverrideKeys_t* pKeys, uint32_t unKeys) {
  return fuvr::openvr_shim::mockIVRApplications()->LaunchTemplateApplication(pchTemplateAppKey, pchNewAppKey, pKeys, unKeys);
}

static vr::EVRApplicationError fnt_Applications_LaunchApplicationFromMimeType(const char* pchMimeType, const char* pchArgs) {
  return fuvr::openvr_shim::mockIVRApplications()->LaunchApplicationFromMimeType(pchMimeType, pchArgs);
}

static vr::EVRApplicationError fnt_Applications_LaunchDashboardOverlay(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->LaunchDashboardOverlay(pchAppKey);
}

static bool fnt_Applications_CancelApplicationLaunch(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->CancelApplicationLaunch(pchAppKey);
}

static vr::EVRApplicationError fnt_Applications_IdentifyApplication(uint32_t unProcessId, const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->IdentifyApplication(unProcessId, pchAppKey);
}

static uint32_t fnt_Applications_GetApplicationProcessId(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationProcessId(pchAppKey);
}

static const char* fnt_Applications_GetApplicationsErrorNameFromEnum(vr::EVRApplicationError error) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationsErrorNameFromEnum(error);
}

static uint32_t fnt_Applications_GetApplicationPropertyString(const char* pchAppKey, vr::EVRApplicationProperty eProperty, char* pchPropertyValueBuffer, uint32_t unPropertyValueBufferLen, vr::EVRApplicationError* peError) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationPropertyString(pchAppKey, eProperty, pchPropertyValueBuffer, unPropertyValueBufferLen, peError);
}

static bool fnt_Applications_GetApplicationPropertyBool(const char* pchAppKey, vr::EVRApplicationProperty eProperty, vr::EVRApplicationError* peError) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationPropertyBool(pchAppKey, eProperty, peError);
}

static uint64_t fnt_Applications_GetApplicationPropertyUint64(const char* pchAppKey, vr::EVRApplicationProperty eProperty, vr::EVRApplicationError* peError) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationPropertyUint64(pchAppKey, eProperty, peError);
}

static vr::EVRApplicationError fnt_Applications_SetApplicationAutoLaunch(const char* pchAppKey, bool bAutoLaunch) {
  return fuvr::openvr_shim::mockIVRApplications()->SetApplicationAutoLaunch(pchAppKey, bAutoLaunch);
}

static bool fnt_Applications_GetApplicationAutoLaunch(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationAutoLaunch(pchAppKey);
}

static vr::EVRApplicationError fnt_Applications_SetDefaultApplicationForMimeType(const char* pchAppKey, const char* pchMimeType) {
  return fuvr::openvr_shim::mockIVRApplications()->SetDefaultApplicationForMimeType(pchAppKey, pchMimeType);
}

static bool fnt_Applications_GetDefaultApplicationForMimeType(const char* pchMimeType, char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen) {
  return fuvr::openvr_shim::mockIVRApplications()->GetDefaultApplicationForMimeType(pchMimeType, pchAppKeyBuffer, unAppKeyBufferLen);
}

static bool fnt_Applications_GetApplicationSupportedMimeTypes(const char* pchAppKey, char* pchMimeTypesBuffer, uint32_t unMimeTypesBuffer) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationSupportedMimeTypes(pchAppKey, pchMimeTypesBuffer, unMimeTypesBuffer);
}

static uint32_t fnt_Applications_GetApplicationsThatSupportMimeType(const char* pchMimeType, char* pchAppKeysThatSupportBuffer, uint32_t unAppKeysThatSupportBuffer) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationsThatSupportMimeType(pchMimeType, pchAppKeysThatSupportBuffer, unAppKeysThatSupportBuffer);
}

static uint32_t fnt_Applications_GetApplicationLaunchArguments(uint32_t unHandle, char* pchArgs, uint32_t unArgs) {
  return fuvr::openvr_shim::mockIVRApplications()->GetApplicationLaunchArguments(unHandle, pchArgs, unArgs);
}

static vr::EVRApplicationError fnt_Applications_GetStartingApplication(char* pchAppKeyBuffer, uint32_t unAppKeyBufferLen) {
  return fuvr::openvr_shim::mockIVRApplications()->GetStartingApplication(pchAppKeyBuffer, unAppKeyBufferLen);
}

static vr::EVRSceneApplicationState fnt_Applications_GetSceneApplicationState() {
  return fuvr::openvr_shim::mockIVRApplications()->GetSceneApplicationState();
}

static vr::EVRApplicationError fnt_Applications_PerformApplicationPrelaunchCheck(const char* pchAppKey) {
  return fuvr::openvr_shim::mockIVRApplications()->PerformApplicationPrelaunchCheck(pchAppKey);
}

static const char* fnt_Applications_GetSceneApplicationStateNameFromEnum(vr::EVRSceneApplicationState state) {
  return fuvr::openvr_shim::mockIVRApplications()->GetSceneApplicationStateNameFromEnum(state);
}

static vr::EVRApplicationError fnt_Applications_LaunchInternalProcess(const char* pchBinaryPath, const char* pchArguments, const char* pchWorkingDirectory) {
  return fuvr::openvr_shim::mockIVRApplications()->LaunchInternalProcess(pchBinaryPath, pchArguments, pchWorkingDirectory);
}

static uint32_t fnt_Applications_GetCurrentSceneProcessId() {
  return fuvr::openvr_shim::mockIVRApplications()->GetCurrentSceneProcessId();
}

}  // extern "C"

namespace fuvr::openvr_shim {
namespace {

struct ApplicationsFnTable {
  void* AddApplicationManifest;
  void* RemoveApplicationManifest;
  void* IsApplicationInstalled;
  void* GetApplicationCount;
  void* GetApplicationKeyByIndex;
  void* GetApplicationKeyByProcessId;
  void* LaunchApplication;
  void* LaunchTemplateApplication;
  void* LaunchApplicationFromMimeType;
  void* LaunchDashboardOverlay;
  void* CancelApplicationLaunch;
  void* IdentifyApplication;
  void* GetApplicationProcessId;
  void* GetApplicationsErrorNameFromEnum;
  void* GetApplicationPropertyString;
  void* GetApplicationPropertyBool;
  void* GetApplicationPropertyUint64;
  void* SetApplicationAutoLaunch;
  void* GetApplicationAutoLaunch;
  void* SetDefaultApplicationForMimeType;
  void* GetDefaultApplicationForMimeType;
  void* GetApplicationSupportedMimeTypes;
  void* GetApplicationsThatSupportMimeType;
  void* GetApplicationLaunchArguments;
  void* GetStartingApplication;
  void* GetSceneApplicationState;
  void* PerformApplicationPrelaunchCheck;
  void* GetSceneApplicationStateNameFromEnum;
  void* LaunchInternalProcess;
  void* GetCurrentSceneProcessId;
};

static const ApplicationsFnTable g_applications_fntable = {
  reinterpret_cast<void*>(&fnt_Applications_AddApplicationManifest),
  reinterpret_cast<void*>(&fnt_Applications_RemoveApplicationManifest),
  reinterpret_cast<void*>(&fnt_Applications_IsApplicationInstalled),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationCount),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationKeyByIndex),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationKeyByProcessId),
  reinterpret_cast<void*>(&fnt_Applications_LaunchApplication),
  reinterpret_cast<void*>(&fnt_Applications_LaunchTemplateApplication),
  reinterpret_cast<void*>(&fnt_Applications_LaunchApplicationFromMimeType),
  reinterpret_cast<void*>(&fnt_Applications_LaunchDashboardOverlay),
  reinterpret_cast<void*>(&fnt_Applications_CancelApplicationLaunch),
  reinterpret_cast<void*>(&fnt_Applications_IdentifyApplication),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationProcessId),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationsErrorNameFromEnum),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationPropertyString),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationPropertyBool),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationPropertyUint64),
  reinterpret_cast<void*>(&fnt_Applications_SetApplicationAutoLaunch),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationAutoLaunch),
  reinterpret_cast<void*>(&fnt_Applications_SetDefaultApplicationForMimeType),
  reinterpret_cast<void*>(&fnt_Applications_GetDefaultApplicationForMimeType),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationSupportedMimeTypes),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationsThatSupportMimeType),
  reinterpret_cast<void*>(&fnt_Applications_GetApplicationLaunchArguments),
  reinterpret_cast<void*>(&fnt_Applications_GetStartingApplication),
  reinterpret_cast<void*>(&fnt_Applications_GetSceneApplicationState),
  reinterpret_cast<void*>(&fnt_Applications_PerformApplicationPrelaunchCheck),
  reinterpret_cast<void*>(&fnt_Applications_GetSceneApplicationStateNameFromEnum),
  reinterpret_cast<void*>(&fnt_Applications_LaunchInternalProcess),
  reinterpret_cast<void*>(&fnt_Applications_GetCurrentSceneProcessId),
};

}  // namespace

void* applicationsFnTable() {
  return const_cast<void*>(static_cast<const void*>(&g_applications_fntable));
}

}  // namespace fuvr::openvr_shim
