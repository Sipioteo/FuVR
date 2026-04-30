// SPDX-License-Identifier: Apache-2.0
//
// C-ABI entry points expected by Valve's `openvr_api.dll/dylib`. Games
// dlopen() us and dlsym() these symbols by name. Their signatures and
// names must match Valve's reference implementation exactly — this file
// is the only place where the shim is part of the OpenVR ABI surface.
//
// Reference: openvr/src/openvr_api_public.cpp from the Valve repo.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "openvr.h"

#include "log.hpp"
#include "mock_state.hpp"

// Forward declarations of the per-interface singletons.
namespace fuvr::openvr_shim {
vr::IVRSystem*       mockIVRSystem();
vr::IVRCompositor*   mockIVRCompositor();
vr::IVRInput*        mockIVRInput();
vr::IVRChaperone*    mockIVRChaperone();
vr::IVROverlay*      mockIVROverlay();
vr::IVRSettings*     mockIVRSettings();
vr::IVRRenderModels* mockIVRRenderModels();
vr::IVRApplications* mockIVRApplications();

// FnTable getters — each returns the address of a struct of plain C
// function pointers in declaration order. LWJGL bindings request these
// when probing with the `"FnTable:"` prefix.
void* compositorFnTable();
void* systemFnTable();
void* inputFnTable();
void* chaperoneFnTable();
void* applicationsFnTable();
void* renderModelsFnTable();
void* settingsFnTable();
}

namespace {

// Small helper: matches "IVRSystem_026", "IVRSystem_022", etc.
bool versionPrefixMatches(const char* requested, const char* family) {
  if (!requested || !family) return false;
  size_t n = std::strlen(family);
  return std::strncmp(requested, family, n) == 0;
}

uint32_t lastError = vr::VRInitError_None;

}  // namespace

extern "C" {

// `VR_InitInternal2` is the modern entrypoint (signature includes startup
// info). The older `VR_InitInternal` (no startup info) is delegated to it.
__attribute__((visibility("default")))
uint32_t VR_InitInternal2(vr::EVRInitError* peError,
                          vr::EVRApplicationType eType,
                          const char* pStartupInfo) {
  using fuvr::openvr_shim::MockState;
  std::string appKey = pStartupInfo ? std::string(pStartupInfo) : std::string("fuvr.openvr.app");
  if (!MockState::shared().initialize(appKey, static_cast<uint32_t>(eType))) {
    lastError = vr::VRInitError_Init_HmdNotFound;
    if (peError) *peError = vr::VRInitError_Init_HmdNotFound;
    FUVR_LOG("VR_InitInternal2 failed — daemon unreachable on %s",
             fuvr::openvr_shim::wire::kSocketPath);
    return 0;
  }
  lastError = vr::VRInitError_None;
  if (peError) *peError = vr::VRInitError_None;
  // Token is opaque to the game — return a non-zero "OK" sentinel.
  return 1;
}

__attribute__((visibility("default")))
uint32_t VR_InitInternal(vr::EVRInitError* peError, vr::EVRApplicationType eType) {
  return VR_InitInternal2(peError, eType, nullptr);
}

__attribute__((visibility("default")))
void VR_ShutdownInternal() {
  fuvr::openvr_shim::MockState::shared().shutdown();
}

__attribute__((visibility("default")))
bool VR_IsHmdPresent() {
  // True iff we can reach the daemon. Game uses this for the "Launch in
  // VR mode" detection at startup.
  using fuvr::openvr_shim::MockState;
  if (MockState::shared().isInitialized()) return true;
  // Quick probe: open a transient connection.
  fuvr::openvr_shim::DeviceCaps tmp{};
  fuvr::openvr_shim::DaemonRpc probe;
  if (probe.connect("fuvr.probe", static_cast<uint32_t>(vr::VRApplication_Other), tmp)) {
    probe.disconnect();
    return true;
  }
  return false;
}

__attribute__((visibility("default")))
bool VR_IsRuntimeInstalled() { return true; }

__attribute__((visibility("default")))
const char* VR_GetVRInitErrorAsSymbol(vr::EVRInitError e) {
  switch (e) {
    case vr::VRInitError_None:                 return "VRInitError_None";
    case vr::VRInitError_Init_HmdNotFound:     return "VRInitError_Init_HmdNotFound";
    case vr::VRInitError_Init_NotInitialized:  return "VRInitError_Init_NotInitialized";
    default:                                   return "VRInitError_Unknown";
  }
}

__attribute__((visibility("default")))
const char* VR_GetVRInitErrorAsEnglishDescription(vr::EVRInitError e) {
  switch (e) {
    case vr::VRInitError_None:
      return "OK";
    case vr::VRInitError_Init_HmdNotFound:
      return "FuVR daemon (fuvrd) is not reachable. Make sure it is running "
             "and listening on /tmp/fuvr_openvr.sock.";
    default:
      return "FuVR shim returned an unknown initialisation error.";
  }
}

__attribute__((visibility("default")))
bool VR_IsInterfaceVersionValid(const char* version) {
  if (!version) return false;
  // Every IVR* family the shim implements (or stubs) is acceptable.
  return versionPrefixMatches(version, "IVRSystem_")
      || versionPrefixMatches(version, "IVRCompositor_")
      || versionPrefixMatches(version, "IVRInput_")
      || versionPrefixMatches(version, "IVRChaperone_")
      || versionPrefixMatches(version, "IVRChaperoneSetup_")
      || versionPrefixMatches(version, "IVROverlay_")
      || versionPrefixMatches(version, "IVRSettings_")
      || versionPrefixMatches(version, "IVRRenderModels_")
      || versionPrefixMatches(version, "IVRApplications_")
      || versionPrefixMatches(version, "IVRScreenshots_")
      || versionPrefixMatches(version, "IVRTrackedCamera_")
      || versionPrefixMatches(version, "IVRDriverManager_")
      || versionPrefixMatches(version, "IVRResources_")
      || versionPrefixMatches(version, "IVRNotifications_");
}

__attribute__((visibility("default")))
void* VR_GetGenericInterface(const char* version, vr::EVRInitError* peError) {
  using namespace fuvr::openvr_shim;
  if (peError) *peError = vr::VRInitError_None;
  if (!version) {
    if (peError) *peError = vr::VRInitError_Init_InvalidInterface;
    return nullptr;
  }

  // LWJGL OpenVR bindings prefix the version with "FnTable:" — they want
  // a flat struct of C function pointers (the OpenVR FnTable layout)
  // rather than a C++ instance pointer. Both the C++ vtable and the
  // FnTable share the same memory shape: a contiguous array of function
  // pointers in declaration order. Returning the address of the C++
  // vtable (one dereference of the instance pointer) gives LWJGL exactly
  // what it expects — the methods are dispatched directly without going
  // through a class pointer.
  const char* lookup = version;
  bool wantFnTable = false;
  if (versionPrefixMatches(version, "FnTable:")) {
    lookup = version + 8;  // strlen("FnTable:")
    wantFnTable = true;
  }

  // FnTable path: LWJGL et al expect a flat struct of plain C function
  // pointers (no implicit `this`). The dedicated `fntable_*.cpp` files
  // build that struct by wrapping each virtual method in an `extern "C"`
  // thunk. Naive vtable-pointer return crashes because Itanium ABI
  // virtual calls take `this` in the first arg slot.
  if (wantFnTable) {
    void* table = nullptr;
    if      (versionPrefixMatches(lookup, "IVRSystem_"))       table = systemFnTable();
    else if (versionPrefixMatches(lookup, "IVRCompositor_"))   table = compositorFnTable();
    else if (versionPrefixMatches(lookup, "IVRInput_"))        table = inputFnTable();
    else if (versionPrefixMatches(lookup, "IVRChaperone_"))    table = chaperoneFnTable();
    else if (versionPrefixMatches(lookup, "IVRApplications_")) table = applicationsFnTable();
    else if (versionPrefixMatches(lookup, "IVRRenderModels_")) table = renderModelsFnTable();
    else if (versionPrefixMatches(lookup, "IVRSettings_"))     table = settingsFnTable();
    if (!table) {
      FUVR_LOG("GetGenericInterface(%s) — no FnTable, returning null", version);
      if (peError) *peError = vr::VRInitError_Init_InvalidInterface;
    }
    return table;
  }

  // C++ vtable path: native SteamVR Mac titles still take a class
  // pointer. The C++ singleton's virtual dispatch works for them.
  void* impl = nullptr;
  if      (versionPrefixMatches(lookup, "IVRSystem_"))       impl = mockIVRSystem();
  else if (versionPrefixMatches(lookup, "IVRCompositor_"))   impl = mockIVRCompositor();
  else if (versionPrefixMatches(lookup, "IVRInput_"))        impl = mockIVRInput();
  else if (versionPrefixMatches(lookup, "IVRChaperone_"))    impl = mockIVRChaperone();
  else if (versionPrefixMatches(lookup, "IVROverlay_"))      impl = mockIVROverlay();
  else if (versionPrefixMatches(lookup, "IVRSettings_"))     impl = mockIVRSettings();
  else if (versionPrefixMatches(lookup, "IVRRenderModels_")) impl = mockIVRRenderModels();
  else if (versionPrefixMatches(lookup, "IVRApplications_")) impl = mockIVRApplications();

  if (impl == nullptr) {
    FUVR_LOG("GetGenericInterface(%s) — unsupported, returning null", version);
    if (peError) *peError = vr::VRInitError_Init_InvalidInterface;
  }
  return impl;
}

__attribute__((visibility("default")))
uint32_t VR_GetInitToken() { return 1; }

// ---- Legacy entry points required by LWJGL's OpenVR bindings ----
//
// LWJGL resolves the FULL set of C entry points exported by Valve's
// libopenvr_api.dylib at static-init time. Any unresolved symbol becomes
// an `UnsatisfiedLinkError` → `ExceptionInInitializerError` and the JVM
// never even calls VR_InitInternal2. We therefore export sentinel
// implementations of the legacy/auxiliary entry points so the lookup
// succeeds — they return inert values that production code paths
// (LWJGL's actual VR session) never read.

/// Pre-OpenVR-1.0 hmd-error-to-string lookup. Modern code uses
/// `VR_GetVRInitErrorAsEnglishDescription`; LWJGL still resolves this
/// symbol on init.
__attribute__((visibility("default")))
const char* VR_GetStringForHmdError(int32_t error) {
  return VR_GetVRInitErrorAsEnglishDescription(static_cast<vr::EVRInitError>(error));
}

/// Returns the SteamVR install root. We have no install — return an
/// empty string. Valve's prototype returns `bool` (success/fail).
__attribute__((visibility("default")))
bool VR_GetRuntimePath(char* buffer, uint32_t bufferSize, uint32_t* requiredSize) {
  if (requiredSize) *requiredSize = 1;  // just the null terminator
  if (buffer && bufferSize > 0) buffer[0] = '\0';
  return true;
}

/// LWJGL 3.3 names this binding `VR_RuntimePath` (without `Get`),
/// diverging from Valve's `openvr.h` declaration. The Java bindgen
/// resolves the symbol literally — so we must export BOTH names.
/// Without this alias, LWJGL's `VR.Functions.<clinit>` throws
/// `NullPointerException: A required function is missing: VR_RuntimePath`,
/// the JVM marks the class as `ExceptionInInitializerError`, and
/// Vivecraft's VR init fails before `nVR_InitInternal` can run.
__attribute__((visibility("default")))
bool VR_RuntimePath(char* buffer, uint32_t bufferSize, uint32_t* requiredSize) {
  return VR_GetRuntimePath(buffer, bufferSize, requiredSize);
}

/// Some games / launchers probe these directly to find SteamVR before
/// init. We always claim it's installed and pretend the HMD is present
/// (callers fall through to VR_InitInternal2, which is the real check).
__attribute__((visibility("default")))
bool VR_IsRuntimePathOverridden() { return true; }

/// Modern OpenVR exports a `VRTokenForInterface` lookup; LWJGL doesn't
/// need it but other languages' bindings may. Cheap to add.
__attribute__((visibility("default")))
uint32_t VR_GetCurrentSessionToken() { return 1; }

}  // extern "C"
