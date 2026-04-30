// SPDX-License-Identifier: Apache-2.0
//
// Flat C function-pointer table for IVRSettings_003. LWJGL's OpenVR
// bindings (3.3.2 → OpenVR 1.23.7) request "FnTable:IVRSettings_003" via
// VR_GetGenericInterface. Without a non-null return, LWJGL leaves
// `OpenVR.VRSettings` as null and Vivecraft NPEs the first time it calls
// `VRSettings_GetFloat` (e.g. for the supersampling key).
//
// FuVR has no settings store — every accessor returns a sane default
// with `VRSettingsError_None`. Vivecraft and similar tolerate this and
// fall back to compile-time defaults (no supersampling, default render
// scale). The slot order below matches the static method declaration
// order of `org.lwjgl.openvr.OpenVR$IVRSettings`, which IS the FnTable
// layout LWJGL expects:
//
//   GetSettingsErrorNameFromEnum
//   SetBool, SetInt32, SetFloat, SetString
//   GetBool, GetInt32, GetFloat, GetString
//   RemoveSection, RemoveKeyInSection
//
// (Note: although our local `openvr.h` declares newer IVRSettings
// versions, LWJGL 3.3.2 specifically requests the v1.23.7-shaped
// IVRSettings_003 — same slot count and order, no drift to handle.)

#include <cstring>

#include "openvr.h"

extern "C" {

static const char* fnt_Settings_GetSettingsErrorNameFromEnum(vr::EVRSettingsError /*eError*/) {
  return "VRSettingsError_None";
}

static void fnt_Settings_SetBool(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                 bool /*bValue*/, vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
}

static void fnt_Settings_SetInt32(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                  int32_t /*nValue*/, vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
}

static void fnt_Settings_SetFloat(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                  float /*flValue*/, vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
}

static void fnt_Settings_SetString(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                   const char* /*pchValue*/, vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
}

static bool fnt_Settings_GetBool(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                 vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
  return false;
}

static int32_t fnt_Settings_GetInt32(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                     vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
  return 0;
}

static float fnt_Settings_GetFloat(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                   vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
  return 0.0f;
}

static void fnt_Settings_GetString(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                   char* pchValue, uint32_t unValueLen,
                                   vr::EVRSettingsError* peError) {
  if (pchValue && unValueLen > 0) pchValue[0] = '\0';
  if (peError) *peError = vr::VRSettingsError_None;
}

static void fnt_Settings_RemoveSection(const char* /*pchSection*/, vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
}

static void fnt_Settings_RemoveKeyInSection(const char* /*pchSection*/, const char* /*pchSettingsKey*/,
                                            vr::EVRSettingsError* peError) {
  if (peError) *peError = vr::VRSettingsError_None;
}

}  // extern "C"

namespace fuvr::openvr_shim {
namespace {

struct SettingsFnTable {
  void* GetSettingsErrorNameFromEnum;
  void* SetBool;
  void* SetInt32;
  void* SetFloat;
  void* SetString;
  void* GetBool;
  void* GetInt32;
  void* GetFloat;
  void* GetString;
  void* RemoveSection;
  void* RemoveKeyInSection;
};

static const SettingsFnTable g_settings_fntable = {
  reinterpret_cast<void*>(&fnt_Settings_GetSettingsErrorNameFromEnum),
  reinterpret_cast<void*>(&fnt_Settings_SetBool),
  reinterpret_cast<void*>(&fnt_Settings_SetInt32),
  reinterpret_cast<void*>(&fnt_Settings_SetFloat),
  reinterpret_cast<void*>(&fnt_Settings_SetString),
  reinterpret_cast<void*>(&fnt_Settings_GetBool),
  reinterpret_cast<void*>(&fnt_Settings_GetInt32),
  reinterpret_cast<void*>(&fnt_Settings_GetFloat),
  reinterpret_cast<void*>(&fnt_Settings_GetString),
  reinterpret_cast<void*>(&fnt_Settings_RemoveSection),
  reinterpret_cast<void*>(&fnt_Settings_RemoveKeyInSection),
};

}  // namespace

void* settingsFnTable() {
  return const_cast<void*>(static_cast<const void*>(&g_settings_fntable));
}

}  // namespace fuvr::openvr_shim
