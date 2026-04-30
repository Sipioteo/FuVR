// SPDX-License-Identifier: Apache-2.0
//
// LWJGL OpenVR per-method native thunks.
//
// LWJGL 3.3.2 declares a small handful of openvr binding methods as
// `private static native` directly (rather than going through the generic
// `JNI.callXXX(... , fnPtr)` dispatcher). Upstream's `liblwjgl_openvr.dylib`
// ships handcrafted JNI symbols for these. Because we replace that dylib at
// runtime via the symlink shim, we must export matching symbols ourselves
// or the JVM's native-method linker fails with `UnsatisfiedLinkError` the
// first time Vivecraft / a mod calls one of these.
//
// Argument order — non-obvious, confirmed by disassembling the LWJGL classes:
//
//   For sret-returning native methods (the C function returns a struct
//   via hidden-sret-pointer ABI), LWJGL's caller pushes args:
//       (regular_args..., fnPtr, out_buffer)
//   i.e. fnPtr is in the "last regular arg" slot and out_buffer trails it.
//
//   For methods that take an output pointer as a regular arg (not sret —
//   e.g. GetTransformForOverlayCoordinates returns jint and writes its
//   matrix through a normal pointer arg), LWJGL pushes fnPtr LAST as
//   usual: (regular_args..., out_ptr, fnPtr).
//
//   For pure-input methods, fnPtr is also last.
//
// Calling convention for sret returns — also non-obvious:
//
//   Apple's ARM64 ABI uses x8 (NOT x0) for the hidden sret pointer when a
//   function returns a struct >16B (and not an HFA). If we expressed our
//   thunk with `typedef void (*FnT)(StructT*, args...)` and passed the
//   Java output buffer as the explicit first arg, the C compiler would
//   emit a normal call (out in x0, args shifted), but the target function
//   was compiled C++ with `StructT (*)(args...)` and reads the sret ptr
//   from x8 — getting whatever junk happened to be in x8. That junk
//   pointer then fed the function's internal `out{}` zero-initializer
//   memset, crashing with BUS_ADRALN at an unaligned address.
//
//   The fix is to declare FnT with the actual return-by-value signature
//   and let the C compiler emit the correct ABI sequence (sret via x8 on
//   Apple, x0 on AAPCS standard). We capture the result on our stack and
//   memcpy to the Java output buffer.
//
//   This applies to HmdMatrix34_t (48B) and HmdMatrix44_t (64B) — both
//   too large for register return on every supported ABI.
//
//   HmdColor_t (16B / 4 floats) is HFA and returned in s0..s3 on arm64;
//   HiddenAreaMesh_t (16B / ptr+uint+pad) is non-HFA ≤16B, returned in
//   x0:x1. Both use register return and the existing `typedef T (*FnT)`
//   declarations are correct as-is — no sret involved.

#include <jni.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Mirror just the openvr struct shapes we need — keep this file pure C
// and free of any C++/openvr-header dependency. Layouts must match
// `third_party/openvr/openvr.h` exactly.
typedef struct { float m[3][4]; } HmdMatrix34_t;       // 48 bytes
typedef struct { float m[4][4]; } HmdMatrix44_t;       // 64 bytes
typedef struct { float v[2]; } HmdVector2_t;           //  8 bytes
typedef struct { float r, g, b, a; } HmdColor_t;       // 16 bytes
typedef struct { HmdVector2_t tl, br; } HmdRect2_t;    // 16 bytes
typedef struct {
  const HmdVector2_t *pVertexData;
  uint32_t unTriangleCount;
} HiddenAreaMesh_t;                                    // 16 bytes (with padding)

// ---- VRSystem ---------------------------------------------------------

// HmdMatrix44_t GetProjectionMatrix(EVREye eEye, float fNearZ, float fFarZ)
//   Java: nVRSystem_GetProjectionMatrix(int eye, float near, float far, long fnPtr, long out)
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetProjectionMatrix(
    JNIEnv *env, jclass cls,
    jint eEye, jfloat fNearZ, jfloat fFarZ,
    jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HmdMatrix44_t (*FnT)(int32_t, float, float);
  HmdMatrix44_t r = ((FnT)(intptr_t)fnPtr)(eEye, fNearZ, fFarZ);
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// HmdMatrix34_t GetEyeToHeadTransform(EVREye eEye)
//   Java: nVRSystem_GetEyeToHeadTransform(int eye, long fnPtr, long out)
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetEyeToHeadTransform(
    JNIEnv *env, jclass cls,
    jint eEye, jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HmdMatrix34_t (*FnT)(int32_t);
  HmdMatrix34_t r = ((FnT)(intptr_t)fnPtr)(eEye);
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose()
//   Java: nVRSystem_GetSeatedZeroPoseToStandingAbsoluteTrackingPose(long fnPtr, long out)
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetSeatedZeroPoseToStandingAbsoluteTrackingPose(
    JNIEnv *env, jclass cls,
    jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HmdMatrix34_t (*FnT)(void);
  HmdMatrix34_t r = ((FnT)(intptr_t)fnPtr)();
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose()
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetRawZeroPoseToStandingAbsoluteTrackingPose(
    JNIEnv *env, jclass cls,
    jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HmdMatrix34_t (*FnT)(void);
  HmdMatrix34_t r = ((FnT)(intptr_t)fnPtr)();
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// HmdMatrix34_t GetMatrix34TrackedDeviceProperty(TrackedDeviceIndex_t,
//                                                ETrackedDeviceProperty,
//                                                ETrackedPropertyError* pError)
//   Java: nVRSystem_GetMatrix34TrackedDeviceProperty(int devIdx, int prop,
//                                                    long pError, long fnPtr, long out)
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetMatrix34TrackedDeviceProperty(
    JNIEnv *env, jclass cls,
    jint unDeviceIndex, jint prop, jlong pError, jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HmdMatrix34_t (*FnT)(uint32_t, int32_t, int32_t*);
  HmdMatrix34_t r = ((FnT)(intptr_t)fnPtr)((uint32_t)unDeviceIndex,
                                           prop,
                                           (int32_t*)(intptr_t)pError);
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// HiddenAreaMesh_t GetHiddenAreaMesh(EVREye eEye, EHiddenAreaMeshType type)
//   Java: nVRSystem_GetHiddenAreaMesh(int eye, int type, long fnPtr, long out)
//   Returned in registers (16 bytes, non-HFA → x0:x1). Capture and memcpy.
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRSystem_nVRSystem_1GetHiddenAreaMesh(
    JNIEnv *env, jclass cls,
    jint eEye, jint type, jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HiddenAreaMesh_t (*FnT)(int32_t, int32_t);
  HiddenAreaMesh_t r = ((FnT)(intptr_t)fnPtr)(eEye, type);
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// ---- VRCompositor ----------------------------------------------------

// HmdColor_t GetCurrentFadeColor(bool bBackground)
//   Java: nVRCompositor_GetCurrentFadeColor(boolean bg, long fnPtr, long out)
//   Returned in registers (16B HFA → s0..s3 on arm64). Capture and memcpy.
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRCompositor_nVRCompositor_1GetCurrentFadeColor(
    JNIEnv *env, jclass cls,
    jboolean bBackground, jlong fnPtr, jlong out) {
  (void)env; (void)cls;
  typedef HmdColor_t (*FnT)(bool);
  HmdColor_t r = ((FnT)(intptr_t)fnPtr)(bBackground ? true : false);
  if (out) memcpy((void*)(intptr_t)out, &r, sizeof(r));
}

// ---- VRChaperone ------------------------------------------------------

// void SetSceneColor(HmdColor_t color)
//   Java: nVRChaperone_SetSceneColor(long colorPtr, long fnPtr)
//   color is passed BY VALUE — load 16 bytes and forward. fnPtr is last (no output buffer).
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VRChaperone_nVRChaperone_1SetSceneColor(
    JNIEnv *env, jclass cls,
    jlong colorPtr, jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(HmdColor_t);
  HmdColor_t c = {0,0,0,0};
  if (colorPtr) memcpy(&c, (void*)(intptr_t)colorPtr, sizeof(c));
  ((FnT)(intptr_t)fnPtr)(c);
}

// ---- VROverlay -------------------------------------------------------

// EVROverlayError GetTransformForOverlayCoordinates(VROverlayHandle_t,
//                                                   ETrackingUniverseOrigin,
//                                                   HmdVector2_t coordsInOverlay,
//                                                   HmdMatrix34_t* pmatTransform)
//   Java: nVROverlay_GetTransformForOverlayCoordinates(long handle, int origin,
//                                                      long coordsPtr, long matOut, long fnPtr) -> int
//   Returns jint via normal return. matOut is a regular pointer arg so fnPtr is LAST per LWJGL convention.
JNIEXPORT jint JNICALL
Java_org_lwjgl_openvr_VROverlay_nVROverlay_1GetTransformForOverlayCoordinates(
    JNIEnv *env, jclass cls,
    jlong ulOverlayHandle, jint eTrackingOrigin,
    jlong coordsPtr, jlong matOut, jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(uint64_t, int32_t, HmdVector2_t, HmdMatrix34_t*);
  HmdVector2_t coords = {{0.0f, 0.0f}};
  if (coordsPtr) memcpy(&coords, (void*)(intptr_t)coordsPtr, sizeof(coords));
  return ((FnT)(intptr_t)fnPtr)((uint64_t)ulOverlayHandle,
                                eTrackingOrigin,
                                coords,
                                (HmdMatrix34_t*)(intptr_t)matOut);
}

// void SetKeyboardPositionForOverlay(VROverlayHandle_t, HmdRect2_t avoidRect)
//   Java: nVROverlay_SetKeyboardPositionForOverlay(long handle, long rectPtr, long fnPtr)
JNIEXPORT void JNICALL
Java_org_lwjgl_openvr_VROverlay_nVROverlay_1SetKeyboardPositionForOverlay(
    JNIEnv *env, jclass cls,
    jlong ulOverlayHandle, jlong rectPtr, jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(uint64_t, HmdRect2_t);
  HmdRect2_t r;
  memset(&r, 0, sizeof(r));
  if (rectPtr) memcpy(&r, (void*)(intptr_t)rectPtr, sizeof(r));
  ((FnT)(intptr_t)fnPtr)((uint64_t)ulOverlayHandle, r);
}
