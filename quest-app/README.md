# FuVR Quest Client

Android NDK app that receives a video stream from a FuVR-enabled Mac, decodes
it with MediaCodec, and presents it as an OpenXR projection layer on Meta
Quest 2 / 3 / 3S / Pro. Pose and controller input are forwarded back to the
Mac at ~1 kHz.

## Requirements

- Android Studio Hedgehog (2023.1.1+) or AGP 8.5+ standalone
- Android NDK r26+ (`26.3.11579264` pinned in `gradle.properties`)
- Gradle 8.7 (wrapper provided)
- JDK 17
- `capnp` CLI on `$PATH` — **build-time dependency**. The `generateCapnp`
  Gradle task wires into `preBuild` and emits C++ sources from
  `../proto/fuvr.capnp` into `app/src/main/cpp/proto_gen/`. The build fails
  with an explicit error if `capnp` is missing (`brew install capnp` /
  `apt-get install capnproto`). The Cap'n Proto C++ runtime itself is built
  from a pinned source tarball via `ExternalProject_Add` inside CMake — no
  separate runtime install is needed.
- Quest in Developer Mode, USB cable, ADB

## Build

```sh
./gradlew assembleDebug
```

The output APK lands in `app/build/outputs/apk/debug/app-debug.apk`.

## Install & run

```sh
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.fuvr.quest/.MainActivity
```

## USB tunnel for development

The default transport is TCP loopback to `127.0.0.1:9943`. Forward that
port over the USB cable so the Quest reaches the Mac side:

```sh
adb reverse tcp:9943 tcp:9943
```

The Mac transport stub lives in the `transport` Rust crate at the repo root.
See its CLI for spike testing (`cargo run -p transport --example spike`).

## Notes

- `minSdk = 29` covers Quest 2 and up; Quest runs Android 10 (API 29) and
  Android 12 (API 32) depending on firmware.
- Only `arm64-v8a` is built. Quest is 64-bit only.
- The OpenXR loader is pulled in via the AAR at
  `org.khronos.openxr:openxr_loader_for_android:1.1.36` with `prefab=true`.
- See `TODO.md` for the runtime work still pending after the M0 skeleton.

## Side-by-side stereo strategy

The Mac encoder produces a single `4128 × 2208` frame per timestamp, with the
left eye occupying `x ∈ [0, 2064)` and the right eye `x ∈ [2064, 4128)`. We
deliberately decode it once into a single `AHardwareBuffer` (zero-copy via
`AImageReader`), bind it as a `GL_TEXTURE_EXTERNAL_OES`, and then run a tiny
GLES3 fullscreen-triangle shader twice per frame: once to blit the left half
into the left per-eye `XrSwapchain` image (sampling `u ∈ [0, 0.5]`) and once
into the right (sampling `u ∈ [0.5, 1.0]`). This keeps the wire format
codec-agnostic and avoids the cost of two encoder/decoder pairs while
maintaining IPD-correct projection through OpenXR's
`XrCompositionLayerProjection`.

## Host-side native tests

```sh
./gradlew :app:hostTest
```

Builds and runs `app/src/main/cpp/tests/test_fragment_reassembly` with the
host C++ toolchain (NOT the NDK). It validates fragment reassembly logic
without a Quest device.
