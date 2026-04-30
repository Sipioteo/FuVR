// SPDX-License-Identifier: Apache-2.0
//
// LWJGL-style FnTable for IVRCompositor. LWJGL prefixes interface lookups
// with "FnTable:" and expects a flat struct of plain C function pointers
// — NOT a C++ vtable. We provide that here by wrapping each virtual
// method on `vr::IVRCompositor` with an `extern "C"` thunk that calls
// the existing C++ singleton.
//
// Method order MUST match the declaration order in
// `third_party/openvr/openvr.h` (and consequently LWJGL's auto-generated
// binding). Adding a method out-of-order corrupts every subsequent call.

#include "openvr.h"

namespace fuvr::openvr_shim {
vr::IVRCompositor* mockIVRCompositor();
}

extern "C" {

static void fnt_Compositor_SetTrackingSpace(vr::ETrackingUniverseOrigin eOrigin) {
  fuvr::openvr_shim::mockIVRCompositor()->SetTrackingSpace(eOrigin);
}

static vr::ETrackingUniverseOrigin fnt_Compositor_GetTrackingSpace() {
  return fuvr::openvr_shim::mockIVRCompositor()->GetTrackingSpace();
}

static vr::EVRCompositorError fnt_Compositor_WaitGetPoses(
    vr::TrackedDevicePose_t* pRenderPoseArray, uint32_t unRenderPoseArrayCount,
    vr::TrackedDevicePose_t* pGamePoseArray, uint32_t unGamePoseArrayCount) {
  return fuvr::openvr_shim::mockIVRCompositor()->WaitGetPoses(
      pRenderPoseArray, unRenderPoseArrayCount,
      pGamePoseArray, unGamePoseArrayCount);
}

static vr::EVRCompositorError fnt_Compositor_GetLastPoses(
    vr::TrackedDevicePose_t* pRenderPoseArray, uint32_t unRenderPoseArrayCount,
    vr::TrackedDevicePose_t* pGamePoseArray, uint32_t unGamePoseArrayCount) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetLastPoses(
      pRenderPoseArray, unRenderPoseArrayCount,
      pGamePoseArray, unGamePoseArrayCount);
}

static vr::EVRCompositorError fnt_Compositor_GetLastPoseForTrackedDeviceIndex(
    vr::TrackedDeviceIndex_t unDeviceIndex,
    vr::TrackedDevicePose_t* pOutputPose,
    vr::TrackedDevicePose_t* pOutputGamePose) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetLastPoseForTrackedDeviceIndex(
      unDeviceIndex, pOutputPose, pOutputGamePose);
}

static vr::EVRCompositorError fnt_Compositor_Submit(
    vr::EVREye eEye, const vr::Texture_t* pTexture,
    const vr::VRTextureBounds_t* pBounds, vr::EVRSubmitFlags nSubmitFlags) {
  return fuvr::openvr_shim::mockIVRCompositor()->Submit(
      eEye, pTexture, pBounds, nSubmitFlags);
}

static void fnt_Compositor_ClearLastSubmittedFrame() {
  fuvr::openvr_shim::mockIVRCompositor()->ClearLastSubmittedFrame();
}

static void fnt_Compositor_PostPresentHandoff() {
  fuvr::openvr_shim::mockIVRCompositor()->PostPresentHandoff();
}

static bool fnt_Compositor_GetFrameTiming(
    vr::Compositor_FrameTiming* pTiming, uint32_t unFramesAgo) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetFrameTiming(pTiming, unFramesAgo);
}

static uint32_t fnt_Compositor_GetFrameTimings(
    vr::Compositor_FrameTiming* pTiming, uint32_t nFrames) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetFrameTimings(pTiming, nFrames);
}

static float fnt_Compositor_GetFrameTimeRemaining() {
  return fuvr::openvr_shim::mockIVRCompositor()->GetFrameTimeRemaining();
}

static void fnt_Compositor_GetCumulativeStats(
    vr::Compositor_CumulativeStats* pStats, uint32_t nStatsSizeInBytes) {
  fuvr::openvr_shim::mockIVRCompositor()->GetCumulativeStats(pStats, nStatsSizeInBytes);
}

static void fnt_Compositor_FadeToColor(
    float fSeconds, float fRed, float fGreen, float fBlue, float fAlpha,
    bool bBackground) {
  fuvr::openvr_shim::mockIVRCompositor()->FadeToColor(
      fSeconds, fRed, fGreen, fBlue, fAlpha, bBackground);
}

static vr::HmdColor_t fnt_Compositor_GetCurrentFadeColor(bool bBackground) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetCurrentFadeColor(bBackground);
}

static void fnt_Compositor_FadeGrid(float fSeconds, bool bFadeGridIn) {
  fuvr::openvr_shim::mockIVRCompositor()->FadeGrid(fSeconds, bFadeGridIn);
}

static float fnt_Compositor_GetCurrentGridAlpha() {
  return fuvr::openvr_shim::mockIVRCompositor()->GetCurrentGridAlpha();
}

static vr::EVRCompositorError fnt_Compositor_SetSkyboxOverride(
    const vr::Texture_t* pTextures, uint32_t unTextureCount) {
  return fuvr::openvr_shim::mockIVRCompositor()->SetSkyboxOverride(pTextures, unTextureCount);
}

static void fnt_Compositor_ClearSkyboxOverride() {
  fuvr::openvr_shim::mockIVRCompositor()->ClearSkyboxOverride();
}

static void fnt_Compositor_CompositorBringToFront() {
  fuvr::openvr_shim::mockIVRCompositor()->CompositorBringToFront();
}

static void fnt_Compositor_CompositorGoToBack() {
  fuvr::openvr_shim::mockIVRCompositor()->CompositorGoToBack();
}

static void fnt_Compositor_CompositorQuit() {
  fuvr::openvr_shim::mockIVRCompositor()->CompositorQuit();
}

static bool fnt_Compositor_IsFullscreen() {
  return fuvr::openvr_shim::mockIVRCompositor()->IsFullscreen();
}

static uint32_t fnt_Compositor_GetCurrentSceneFocusProcess() {
  return fuvr::openvr_shim::mockIVRCompositor()->GetCurrentSceneFocusProcess();
}

static uint32_t fnt_Compositor_GetLastFrameRenderer() {
  return fuvr::openvr_shim::mockIVRCompositor()->GetLastFrameRenderer();
}

static bool fnt_Compositor_CanRenderScene() {
  return fuvr::openvr_shim::mockIVRCompositor()->CanRenderScene();
}

static void fnt_Compositor_ShowMirrorWindow() {
  fuvr::openvr_shim::mockIVRCompositor()->ShowMirrorWindow();
}

static void fnt_Compositor_HideMirrorWindow() {
  fuvr::openvr_shim::mockIVRCompositor()->HideMirrorWindow();
}

static bool fnt_Compositor_IsMirrorWindowVisible() {
  return fuvr::openvr_shim::mockIVRCompositor()->IsMirrorWindowVisible();
}

static void fnt_Compositor_CompositorDumpImages() {
  fuvr::openvr_shim::mockIVRCompositor()->CompositorDumpImages();
}

static bool fnt_Compositor_ShouldAppRenderWithLowResources() {
  return fuvr::openvr_shim::mockIVRCompositor()->ShouldAppRenderWithLowResources();
}

static void fnt_Compositor_ForceInterleavedReprojectionOn(bool bOverride) {
  fuvr::openvr_shim::mockIVRCompositor()->ForceInterleavedReprojectionOn(bOverride);
}

static void fnt_Compositor_ForceReconnectProcess() {
  fuvr::openvr_shim::mockIVRCompositor()->ForceReconnectProcess();
}

static void fnt_Compositor_SuspendRendering(bool bSuspend) {
  fuvr::openvr_shim::mockIVRCompositor()->SuspendRendering(bSuspend);
}

static vr::EVRCompositorError fnt_Compositor_GetMirrorTextureD3D11(
    vr::EVREye eEye, void* pD3D11DeviceOrResource,
    void** ppD3D11ShaderResourceView) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetMirrorTextureD3D11(
      eEye, pD3D11DeviceOrResource, ppD3D11ShaderResourceView);
}

static void fnt_Compositor_ReleaseMirrorTextureD3D11(void* pD3D11ShaderResourceView) {
  fuvr::openvr_shim::mockIVRCompositor()->ReleaseMirrorTextureD3D11(pD3D11ShaderResourceView);
}

static vr::EVRCompositorError fnt_Compositor_GetMirrorTextureGL(
    vr::EVREye eEye, vr::glUInt_t* pglTextureId,
    vr::glSharedTextureHandle_t* pglSharedTextureHandle) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetMirrorTextureGL(
      eEye, pglTextureId, pglSharedTextureHandle);
}

static bool fnt_Compositor_ReleaseSharedGLTexture(
    vr::glUInt_t glTextureId, vr::glSharedTextureHandle_t glSharedTextureHandle) {
  return fuvr::openvr_shim::mockIVRCompositor()->ReleaseSharedGLTexture(
      glTextureId, glSharedTextureHandle);
}

static void fnt_Compositor_LockGLSharedTextureForAccess(
    vr::glSharedTextureHandle_t glSharedTextureHandle) {
  fuvr::openvr_shim::mockIVRCompositor()->LockGLSharedTextureForAccess(glSharedTextureHandle);
}

static void fnt_Compositor_UnlockGLSharedTextureForAccess(
    vr::glSharedTextureHandle_t glSharedTextureHandle) {
  fuvr::openvr_shim::mockIVRCompositor()->UnlockGLSharedTextureForAccess(glSharedTextureHandle);
}

static uint32_t fnt_Compositor_GetVulkanInstanceExtensionsRequired(
    char* pchValue, uint32_t unBufferSize) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetVulkanInstanceExtensionsRequired(
      pchValue, unBufferSize);
}

static uint32_t fnt_Compositor_GetVulkanDeviceExtensionsRequired(
    struct VkPhysicalDevice_T* pPhysicalDevice,
    char* pchValue, uint32_t unBufferSize) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetVulkanDeviceExtensionsRequired(
      pPhysicalDevice, pchValue, unBufferSize);
}

static void fnt_Compositor_SetExplicitTimingMode(vr::EVRCompositorTimingMode eTimingMode) {
  fuvr::openvr_shim::mockIVRCompositor()->SetExplicitTimingMode(eTimingMode);
}

static vr::EVRCompositorError fnt_Compositor_SubmitExplicitTimingData() {
  return fuvr::openvr_shim::mockIVRCompositor()->SubmitExplicitTimingData();
}

static bool fnt_Compositor_IsMotionSmoothingEnabled() {
  return fuvr::openvr_shim::mockIVRCompositor()->IsMotionSmoothingEnabled();
}

static bool fnt_Compositor_IsMotionSmoothingSupported() {
  return fuvr::openvr_shim::mockIVRCompositor()->IsMotionSmoothingSupported();
}

static bool fnt_Compositor_IsCurrentSceneFocusAppLoading() {
  return fuvr::openvr_shim::mockIVRCompositor()->IsCurrentSceneFocusAppLoading();
}

static vr::EVRCompositorError fnt_Compositor_SetStageOverride_Async(
    const char* pchRenderModelPath,
    const vr::HmdMatrix34_t* pTransform,
    const vr::Compositor_StageRenderSettings* pRenderSettings,
    uint32_t nSizeOfRenderSettings) {
  return fuvr::openvr_shim::mockIVRCompositor()->SetStageOverride_Async(
      pchRenderModelPath, pTransform, pRenderSettings, nSizeOfRenderSettings);
}

static void fnt_Compositor_ClearStageOverride() {
  fuvr::openvr_shim::mockIVRCompositor()->ClearStageOverride();
}

static bool fnt_Compositor_GetCompositorBenchmarkResults(
    vr::Compositor_BenchmarkResults* pBenchmarkResults,
    uint32_t nSizeOfBenchmarkResults) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetCompositorBenchmarkResults(
      pBenchmarkResults, nSizeOfBenchmarkResults);
}

static vr::EVRCompositorError fnt_Compositor_GetLastPosePredictionIDs(
    uint32_t* pRenderPosePredictionID, uint32_t* pGamePosePredictionID) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetLastPosePredictionIDs(
      pRenderPosePredictionID, pGamePosePredictionID);
}

static vr::EVRCompositorError fnt_Compositor_GetPosesForFrame(
    uint32_t unPosePredictionID,
    vr::TrackedDevicePose_t* pPoseArray,
    uint32_t unPoseArrayCount) {
  return fuvr::openvr_shim::mockIVRCompositor()->GetPosesForFrame(
      unPosePredictionID, pPoseArray, unPoseArrayCount);
}

}  // extern "C"

namespace fuvr::openvr_shim {

namespace {
struct CompositorFnTable {
  void* SetTrackingSpace;
  void* GetTrackingSpace;
  void* WaitGetPoses;
  void* GetLastPoses;
  void* GetLastPoseForTrackedDeviceIndex;
  void* Submit;
  void* ClearLastSubmittedFrame;
  void* PostPresentHandoff;
  void* GetFrameTiming;
  void* GetFrameTimings;
  void* GetFrameTimeRemaining;
  void* GetCumulativeStats;
  void* FadeToColor;
  void* GetCurrentFadeColor;
  void* FadeGrid;
  void* GetCurrentGridAlpha;
  void* SetSkyboxOverride;
  void* ClearSkyboxOverride;
  void* CompositorBringToFront;
  void* CompositorGoToBack;
  void* CompositorQuit;
  void* IsFullscreen;
  void* GetCurrentSceneFocusProcess;
  void* GetLastFrameRenderer;
  void* CanRenderScene;
  void* ShowMirrorWindow;
  void* HideMirrorWindow;
  void* IsMirrorWindowVisible;
  void* CompositorDumpImages;
  void* ShouldAppRenderWithLowResources;
  void* ForceInterleavedReprojectionOn;
  void* ForceReconnectProcess;
  void* SuspendRendering;
  void* GetMirrorTextureD3D11;
  void* ReleaseMirrorTextureD3D11;
  void* GetMirrorTextureGL;
  void* ReleaseSharedGLTexture;
  void* LockGLSharedTextureForAccess;
  void* UnlockGLSharedTextureForAccess;
  void* GetVulkanInstanceExtensionsRequired;
  void* GetVulkanDeviceExtensionsRequired;
  void* SetExplicitTimingMode;
  void* SubmitExplicitTimingData;
  void* IsMotionSmoothingEnabled;
  void* IsMotionSmoothingSupported;
  void* IsCurrentSceneFocusAppLoading;
  void* SetStageOverride_Async;
  void* ClearStageOverride;
  void* GetCompositorBenchmarkResults;
  void* GetLastPosePredictionIDs;
  void* GetPosesForFrame;
};

CompositorFnTable g_compositor_fntable = {
  reinterpret_cast<void*>(&fnt_Compositor_SetTrackingSpace),
  reinterpret_cast<void*>(&fnt_Compositor_GetTrackingSpace),
  reinterpret_cast<void*>(&fnt_Compositor_WaitGetPoses),
  reinterpret_cast<void*>(&fnt_Compositor_GetLastPoses),
  reinterpret_cast<void*>(&fnt_Compositor_GetLastPoseForTrackedDeviceIndex),
  reinterpret_cast<void*>(&fnt_Compositor_Submit),
  reinterpret_cast<void*>(&fnt_Compositor_ClearLastSubmittedFrame),
  reinterpret_cast<void*>(&fnt_Compositor_PostPresentHandoff),
  reinterpret_cast<void*>(&fnt_Compositor_GetFrameTiming),
  reinterpret_cast<void*>(&fnt_Compositor_GetFrameTimings),
  reinterpret_cast<void*>(&fnt_Compositor_GetFrameTimeRemaining),
  reinterpret_cast<void*>(&fnt_Compositor_GetCumulativeStats),
  reinterpret_cast<void*>(&fnt_Compositor_FadeToColor),
  reinterpret_cast<void*>(&fnt_Compositor_GetCurrentFadeColor),
  reinterpret_cast<void*>(&fnt_Compositor_FadeGrid),
  reinterpret_cast<void*>(&fnt_Compositor_GetCurrentGridAlpha),
  reinterpret_cast<void*>(&fnt_Compositor_SetSkyboxOverride),
  reinterpret_cast<void*>(&fnt_Compositor_ClearSkyboxOverride),
  reinterpret_cast<void*>(&fnt_Compositor_CompositorBringToFront),
  reinterpret_cast<void*>(&fnt_Compositor_CompositorGoToBack),
  reinterpret_cast<void*>(&fnt_Compositor_CompositorQuit),
  reinterpret_cast<void*>(&fnt_Compositor_IsFullscreen),
  reinterpret_cast<void*>(&fnt_Compositor_GetCurrentSceneFocusProcess),
  reinterpret_cast<void*>(&fnt_Compositor_GetLastFrameRenderer),
  reinterpret_cast<void*>(&fnt_Compositor_CanRenderScene),
  reinterpret_cast<void*>(&fnt_Compositor_ShowMirrorWindow),
  reinterpret_cast<void*>(&fnt_Compositor_HideMirrorWindow),
  reinterpret_cast<void*>(&fnt_Compositor_IsMirrorWindowVisible),
  reinterpret_cast<void*>(&fnt_Compositor_CompositorDumpImages),
  reinterpret_cast<void*>(&fnt_Compositor_ShouldAppRenderWithLowResources),
  reinterpret_cast<void*>(&fnt_Compositor_ForceInterleavedReprojectionOn),
  reinterpret_cast<void*>(&fnt_Compositor_ForceReconnectProcess),
  reinterpret_cast<void*>(&fnt_Compositor_SuspendRendering),
  reinterpret_cast<void*>(&fnt_Compositor_GetMirrorTextureD3D11),
  reinterpret_cast<void*>(&fnt_Compositor_ReleaseMirrorTextureD3D11),
  reinterpret_cast<void*>(&fnt_Compositor_GetMirrorTextureGL),
  reinterpret_cast<void*>(&fnt_Compositor_ReleaseSharedGLTexture),
  reinterpret_cast<void*>(&fnt_Compositor_LockGLSharedTextureForAccess),
  reinterpret_cast<void*>(&fnt_Compositor_UnlockGLSharedTextureForAccess),
  reinterpret_cast<void*>(&fnt_Compositor_GetVulkanInstanceExtensionsRequired),
  reinterpret_cast<void*>(&fnt_Compositor_GetVulkanDeviceExtensionsRequired),
  reinterpret_cast<void*>(&fnt_Compositor_SetExplicitTimingMode),
  reinterpret_cast<void*>(&fnt_Compositor_SubmitExplicitTimingData),
  reinterpret_cast<void*>(&fnt_Compositor_IsMotionSmoothingEnabled),
  reinterpret_cast<void*>(&fnt_Compositor_IsMotionSmoothingSupported),
  reinterpret_cast<void*>(&fnt_Compositor_IsCurrentSceneFocusAppLoading),
  reinterpret_cast<void*>(&fnt_Compositor_SetStageOverride_Async),
  reinterpret_cast<void*>(&fnt_Compositor_ClearStageOverride),
  reinterpret_cast<void*>(&fnt_Compositor_GetCompositorBenchmarkResults),
  reinterpret_cast<void*>(&fnt_Compositor_GetLastPosePredictionIDs),
  reinterpret_cast<void*>(&fnt_Compositor_GetPosesForFrame),
};
}  // namespace

void* compositorFnTable() { return &g_compositor_fntable; }

}  // namespace fuvr::openvr_shim
