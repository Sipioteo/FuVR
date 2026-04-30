// SPDX-License-Identifier: Apache-2.0
//
// LWJGL JNI dispatch trampolines that the user's liblwjgl.dylib (3.3.3)
// does NOT export but the custom Vivecraft openvr binding calls. We link
// these into our libopenvr_api.dylib (loaded by the same JVM via
// `System.loadLibrary("lwjgl_openvr")`); the JVM's first-call native
// lookup walks ALL loaded native libraries, so symbols defined here
// satisfy `JNI.callXXX` resolution exactly the same way as if they
// lived in liblwjgl.dylib itself.
//
// Generated mechanically from the list of overloads Vivecraft's binding
// calls; see the orchestrator's diff for provenance. Each trampoline
// just casts the trailing jlong to a function pointer with the
// appropriate signature and invokes it.

#include <jni.h>
#include <stdint.h>
#include <stdbool.h>

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callCV__IISJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jshort a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(int32_t, int32_t, int16_t);
  ((FnT)(intptr_t)fnPtr)(a0, a1, a2);
}

JNIEXPORT jfloat JNICALL Java_org_lwjgl_system_JNI_callF__J(
    JNIEnv *env, jclass cls,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef float (*FnT)(void);
  return ((FnT)(intptr_t)fnPtr)();
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callI__IFJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jfloat a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, float);
  return ((FnT)(intptr_t)fnPtr)(a0, a1);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJI__JFFFJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jfloat a1,
    jfloat a2,
    jfloat a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, float, float, float);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJI__JFJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jfloat a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, float);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJI__JIZJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jboolean a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, bool);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJJI__JFFFFJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jfloat a1,
    jfloat a2,
    jfloat a3,
    jfloat a4,
    jlong a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, float, float, float, float, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, a3, a4, (void*)(intptr_t)a5);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJJI__JJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJJPI__JJJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJJPPPI__JJIJIJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong a3,
    jint a4,
    jlong a5,
    jlong a6,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, void*, int32_t, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3, a4, (void*)(intptr_t)a5, (void*)(intptr_t)a6);
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_JNI_callJJ__JJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void* (*FnT)(void*);
  return (jlong)(intptr_t)((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JIIJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jint a2,
    jlong a3,
    jint a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, int32_t, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, (void*)(intptr_t)a3, a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JIJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JJIIIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jint a3,
    jint a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, int32_t, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, a3, a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JJIIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPI__JJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPJI__JIFJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jfloat a2,
    jlong a3,
    jint a4,
    jlong a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, float, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, (void*)(intptr_t)a3, a4, (void*)(intptr_t)a5);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPJI__JIJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPJI__JJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JIJIJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong a4,
    jint a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, int32_t, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4, a5);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JIJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JIJJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jlong a3,
    jint a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JJIIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JJJIIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jint a3,
    jint a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*, int32_t, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, a3, a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JJJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPI__JJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPJI__JIIIJIJZJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jint a2,
    jint a3,
    jlong a4,
    jint a5,
    jlong a6,
    jboolean a7,
    jlong a8,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, int32_t, int32_t, void*, int32_t, void*, bool, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, a3, (void*)(intptr_t)a4, a5, (void*)(intptr_t)a6, a7, (void*)(intptr_t)a8);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPPI__JIJJJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jlong a3,
    jlong a4,
    jint a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4, a5);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPPI__JJIJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, int32_t, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPPI__JJJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callJPPPPPPPPI__JJJJJJJJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong a3,
    jlong a4,
    jlong a5,
    jlong a6,
    jlong a7,
    jlong a8,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*, void*, void*, void*, void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4, (void*)(intptr_t)a5, (void*)(intptr_t)a6, (void*)(intptr_t)a7, (void*)(intptr_t)a8);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callJPPZ__JJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callJPV__JJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(void*, void*);
  ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callJPZ__JJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2);
}

JNIEXPORT jfloat JNICALL Java_org_lwjgl_system_JNI_callPF__IIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef float (*FnT)(int32_t, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPI__IIIFFJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jint a2,
    jfloat a3,
    jfloat a4,
    jlong a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, int32_t, int32_t, float, float, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, a2, a3, a4, (void*)(intptr_t)a5);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPI__IJIIJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jlong a1,
    jint a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, void*, int32_t, int32_t);
  return ((FnT)(intptr_t)fnPtr)(a0, (void*)(intptr_t)a1, a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPI__JZJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jboolean a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, bool);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPJI__JIIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jint a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPJJI__JJJZJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jboolean a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, void*, void*, bool);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, a3);
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_JNI_callPJ__IIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void* (*FnT)(int32_t, int32_t, void*);
  return (jlong)(intptr_t)((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2);
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_JNI_callPJ__JJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void* (*FnT)(void*);
  return (jlong)(intptr_t)((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPI__IIIJIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jint a2,
    jlong a3,
    jint a4,
    jlong a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, int32_t, int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, a2, (void*)(intptr_t)a3, a4, (void*)(intptr_t)a5);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPI__IIIJJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jint a2,
    jlong a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, int32_t, int32_t, void*, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPI__IIJIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPI__IJJIJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jlong a1,
    jlong a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)(a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, a3);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPJI__IIIJIJZJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jint a2,
    jlong a3,
    jint a4,
    jlong a5,
    jboolean a6,
    jlong a7,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, int32_t, int32_t, void*, int32_t, void*, bool, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, a2, (void*)(intptr_t)a3, a4, (void*)(intptr_t)a5, a6, (void*)(intptr_t)a7);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPJPPZ__JJJJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*, void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4);
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_system_JNI_callPPJ__JIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void* (*FnT)(void*, int32_t, void*);
  return (jlong)(intptr_t)((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2);
}

JNIEXPORT jfloat JNICALL Java_org_lwjgl_system_JNI_callPPPF__JJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef float (*FnT)(void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPPI__IIJJJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jlong a2,
    jlong a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(int32_t, int32_t, void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4);
}

JNIEXPORT jint JNICALL Java_org_lwjgl_system_JNI_callPPPI__JIJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef int32_t (*FnT)(void*, int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPPPPZ__JJJJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*, void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callPPPPV__IJJJJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jlong a1,
    jlong a2,
    jlong a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(int32_t, void*, void*, void*, void*);
  ((FnT)(intptr_t)fnPtr)(a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, (void*)(intptr_t)a3, (void*)(intptr_t)a4);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callPPPPV__JJJIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(void*, void*, void*, int32_t, void*);
  ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callPPPV__JJFJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jfloat a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(void*, void*, float, void*);
  ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callPPPV__JJZJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jboolean a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(void*, void*, bool, void*);
  ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPPZ__JJJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, (void*)(intptr_t)a2);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callPPV__JIFJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jfloat a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(void*, int32_t, float, void*);
  ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPZ__IIJIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jlong a2,
    jint a3,
    jlong a4,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(int32_t, int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2, a3, (void*)(intptr_t)a4);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPZ__IJIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jlong a1,
    jint a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(int32_t, void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, (void*)(intptr_t)a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPZ__JIJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1, (void*)(intptr_t)a2);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPZ__JJIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jint a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1, a2);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPPZ__JJJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jlong a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, void*);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, (void*)(intptr_t)a1);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callPV__IFJIJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jfloat a1,
    jlong a2,
    jint a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(int32_t, float, void*, int32_t);
  ((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2, a3);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPZ__IFFJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jfloat a1,
    jfloat a2,
    jlong a3,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(int32_t, float, float, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, a2, (void*)(intptr_t)a3);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPZ__IIJJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jint a1,
    jlong a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(int32_t, int32_t, void*);
  return ((FnT)(intptr_t)fnPtr)(a0, a1, (void*)(intptr_t)a2);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPZ__IJIJ(
    JNIEnv *env, jclass cls,
    jint a0,
    jlong a1,
    jint a2,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(int32_t, void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)(a0, (void*)(intptr_t)a1, a2);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callPZ__JIJ(
    JNIEnv *env, jclass cls,
    jlong a0,
    jint a1,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void*, int32_t);
  return ((FnT)(intptr_t)fnPtr)((void*)(intptr_t)a0, a1);
}

JNIEXPORT void JNICALL Java_org_lwjgl_system_JNI_callV__FFFFFZJ(
    JNIEnv *env, jclass cls,
    jfloat a0,
    jfloat a1,
    jfloat a2,
    jfloat a3,
    jfloat a4,
    jboolean a5,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef void (*FnT)(float, float, float, float, float, bool);
  ((FnT)(intptr_t)fnPtr)(a0, a1, a2, a3, a4, a5);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callZ__J(
    JNIEnv *env, jclass cls,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void);
  return ((FnT)(intptr_t)fnPtr)();
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_callZ__ZJ(
    JNIEnv *env, jclass cls,
    jboolean a0,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(bool);
  return ((FnT)(intptr_t)fnPtr)(a0);
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_system_JNI_invokeZ__J(
    JNIEnv *env, jclass cls,
    jlong fnPtr) {
  (void)env; (void)cls;
  typedef bool (*FnT)(void);
  return ((FnT)(intptr_t)fnPtr)();
}

#define FUVR_LWJGL_TRAMPOLINE_COUNT 76
