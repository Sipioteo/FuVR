// SPDX-License-Identifier: Apache-2.0
//
// Flat C function-pointer table for IVRRenderModels.
//
// LWJGL's OpenVR bindings request `"FnTable:IVRRenderModels_<n>"` and
// expect a struct whose layout matches Valve's reference C ABI: a flat
// array of plain function pointers (no implicit `this`), one per virtual
// method, in declaration order. We expose that table here by wrapping
// every virtual method on `vr::IVRRenderModels` in an `extern "C"` thunk
// that forwards to the singleton returned by `mockIVRRenderModels()`.

#include "openvr.h"

namespace fuvr::openvr_shim {
vr::IVRRenderModels* mockIVRRenderModels();
}

extern "C" {

static vr::EVRRenderModelError fnt_RenderModels_LoadRenderModel_Async(
    const char* pchRenderModelName, vr::RenderModel_t** ppRenderModel) {
  return fuvr::openvr_shim::mockIVRRenderModels()->LoadRenderModel_Async(pchRenderModelName, ppRenderModel);
}

static void fnt_RenderModels_FreeRenderModel(vr::RenderModel_t* pRenderModel) {
  fuvr::openvr_shim::mockIVRRenderModels()->FreeRenderModel(pRenderModel);
}

static vr::EVRRenderModelError fnt_RenderModels_LoadTexture_Async(
    vr::TextureID_t textureId, vr::RenderModel_TextureMap_t** ppTexture) {
  return fuvr::openvr_shim::mockIVRRenderModels()->LoadTexture_Async(textureId, ppTexture);
}

static void fnt_RenderModels_FreeTexture(vr::RenderModel_TextureMap_t* pTexture) {
  fuvr::openvr_shim::mockIVRRenderModels()->FreeTexture(pTexture);
}

static vr::EVRRenderModelError fnt_RenderModels_LoadTextureD3D11_Async(
    vr::TextureID_t textureId, void* pD3D11Device, void** ppD3D11Texture2D) {
  return fuvr::openvr_shim::mockIVRRenderModels()->LoadTextureD3D11_Async(textureId, pD3D11Device, ppD3D11Texture2D);
}

static vr::EVRRenderModelError fnt_RenderModels_LoadIntoTextureD3D11_Async(
    vr::TextureID_t textureId, void* pDstTexture) {
  return fuvr::openvr_shim::mockIVRRenderModels()->LoadIntoTextureD3D11_Async(textureId, pDstTexture);
}

static void fnt_RenderModels_FreeTextureD3D11(void* pD3D11Texture2D) {
  fuvr::openvr_shim::mockIVRRenderModels()->FreeTextureD3D11(pD3D11Texture2D);
}

static uint32_t fnt_RenderModels_GetRenderModelName(
    uint32_t unRenderModelIndex, char* pchRenderModelName, uint32_t unRenderModelNameLen) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetRenderModelName(
      unRenderModelIndex, pchRenderModelName, unRenderModelNameLen);
}

static uint32_t fnt_RenderModels_GetRenderModelCount() {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetRenderModelCount();
}

static uint32_t fnt_RenderModels_GetComponentCount(const char* pchRenderModelName) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetComponentCount(pchRenderModelName);
}

static uint32_t fnt_RenderModels_GetComponentName(
    const char* pchRenderModelName, uint32_t unComponentIndex,
    char* pchComponentName, uint32_t unComponentNameLen) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetComponentName(
      pchRenderModelName, unComponentIndex, pchComponentName, unComponentNameLen);
}

static uint64_t fnt_RenderModels_GetComponentButtonMask(
    const char* pchRenderModelName, const char* pchComponentName) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetComponentButtonMask(
      pchRenderModelName, pchComponentName);
}

static uint32_t fnt_RenderModels_GetComponentRenderModelName(
    const char* pchRenderModelName, const char* pchComponentName,
    char* pchComponentRenderModelName, uint32_t unComponentRenderModelNameLen) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetComponentRenderModelName(
      pchRenderModelName, pchComponentName, pchComponentRenderModelName, unComponentRenderModelNameLen);
}

static bool fnt_RenderModels_GetComponentStateForDevicePath(
    const char* pchRenderModelName, const char* pchComponentName,
    vr::VRInputValueHandle_t devicePath,
    const vr::RenderModel_ControllerMode_State_t* pState,
    vr::RenderModel_ComponentState_t* pComponentState) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetComponentStateForDevicePath(
      pchRenderModelName, pchComponentName, devicePath, pState, pComponentState);
}

static bool fnt_RenderModels_GetComponentState(
    const char* pchRenderModelName, const char* pchComponentName,
    const vr::VRControllerState_t* pControllerState,
    const vr::RenderModel_ControllerMode_State_t* pState,
    vr::RenderModel_ComponentState_t* pComponentState) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetComponentState(
      pchRenderModelName, pchComponentName, pControllerState, pState, pComponentState);
}

static bool fnt_RenderModels_RenderModelHasComponent(
    const char* pchRenderModelName, const char* pchComponentName) {
  return fuvr::openvr_shim::mockIVRRenderModels()->RenderModelHasComponent(
      pchRenderModelName, pchComponentName);
}

static uint32_t fnt_RenderModels_GetRenderModelThumbnailURL(
    const char* pchRenderModelName, char* pchThumbnailURL,
    uint32_t unThumbnailURLLen, vr::EVRRenderModelError* peError) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetRenderModelThumbnailURL(
      pchRenderModelName, pchThumbnailURL, unThumbnailURLLen, peError);
}

static uint32_t fnt_RenderModels_GetRenderModelOriginalPath(
    const char* pchRenderModelName, char* pchOriginalPath,
    uint32_t unOriginalPathLen, vr::EVRRenderModelError* peError) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetRenderModelOriginalPath(
      pchRenderModelName, pchOriginalPath, unOriginalPathLen, peError);
}

static const char* fnt_RenderModels_GetRenderModelErrorNameFromEnum(vr::EVRRenderModelError error) {
  return fuvr::openvr_shim::mockIVRRenderModels()->GetRenderModelErrorNameFromEnum(error);
}

}  // extern "C"

namespace fuvr::openvr_shim {
namespace {

struct RenderModelsFnTable {
  void* LoadRenderModel_Async;
  void* FreeRenderModel;
  void* LoadTexture_Async;
  void* FreeTexture;
  void* LoadTextureD3D11_Async;
  void* LoadIntoTextureD3D11_Async;
  void* FreeTextureD3D11;
  void* GetRenderModelName;
  void* GetRenderModelCount;
  void* GetComponentCount;
  void* GetComponentName;
  void* GetComponentButtonMask;
  void* GetComponentRenderModelName;
  void* GetComponentStateForDevicePath;
  void* GetComponentState;
  void* RenderModelHasComponent;
  void* GetRenderModelThumbnailURL;
  void* GetRenderModelOriginalPath;
  void* GetRenderModelErrorNameFromEnum;
};

static const RenderModelsFnTable g_rendermodels_fntable = {
  reinterpret_cast<void*>(&fnt_RenderModels_LoadRenderModel_Async),
  reinterpret_cast<void*>(&fnt_RenderModels_FreeRenderModel),
  reinterpret_cast<void*>(&fnt_RenderModels_LoadTexture_Async),
  reinterpret_cast<void*>(&fnt_RenderModels_FreeTexture),
  reinterpret_cast<void*>(&fnt_RenderModels_LoadTextureD3D11_Async),
  reinterpret_cast<void*>(&fnt_RenderModels_LoadIntoTextureD3D11_Async),
  reinterpret_cast<void*>(&fnt_RenderModels_FreeTextureD3D11),
  reinterpret_cast<void*>(&fnt_RenderModels_GetRenderModelName),
  reinterpret_cast<void*>(&fnt_RenderModels_GetRenderModelCount),
  reinterpret_cast<void*>(&fnt_RenderModels_GetComponentCount),
  reinterpret_cast<void*>(&fnt_RenderModels_GetComponentName),
  reinterpret_cast<void*>(&fnt_RenderModels_GetComponentButtonMask),
  reinterpret_cast<void*>(&fnt_RenderModels_GetComponentRenderModelName),
  reinterpret_cast<void*>(&fnt_RenderModels_GetComponentStateForDevicePath),
  reinterpret_cast<void*>(&fnt_RenderModels_GetComponentState),
  reinterpret_cast<void*>(&fnt_RenderModels_RenderModelHasComponent),
  reinterpret_cast<void*>(&fnt_RenderModels_GetRenderModelThumbnailURL),
  reinterpret_cast<void*>(&fnt_RenderModels_GetRenderModelOriginalPath),
  reinterpret_cast<void*>(&fnt_RenderModels_GetRenderModelErrorNameFromEnum),
};

}  // namespace

void* renderModelsFnTable() {
  return const_cast<void*>(static_cast<const void*>(&g_rendermodels_fntable));
}

}  // namespace fuvr::openvr_shim
