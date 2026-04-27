// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace fuvr::runtime {

// Wraps MTLFXSpatialScaler to upscale per-eye textures rendered at half
// resolution back to full per-eye dimensions before SBS combine + HEVC
// encode. Falls back to a simple Metal blit (linear filtered) if MetalFX
// isn't available on the current OS / device — caller will still get a
// usable upscaled texture, just without MetalFX's edge reconstruction.
//
// Lifetime: one instance per session. Lazily initialized on first
// upscale() call (when the input texture's pixel format is known). Owns
// two output textures (left + right slot) sized at output dims; reused
// across frames.
//
// Threading: not internally synchronized. xrEndFrame_impl is the only
// caller and runs serially per session.
class MetalFxUpscaler {
 public:
  MetalFxUpscaler() = default;
  ~MetalFxUpscaler();
  MetalFxUpscaler(const MetalFxUpscaler&) = delete;
  MetalFxUpscaler& operator=(const MetalFxUpscaler&) = delete;

  // device: id<MTLDevice>, retained externally (Session::metalDevice).
  // commandQueue: id<MTLCommandQueue>; if nullptr, an internal one is
  // created. inputW/H: per-eye render dims (post-RENDER_SCALE). outputW/H:
  // per-eye full dims (the encoder/SBS combine target). Returns false if
  // MetalFX isn't available — caller should disable the upscaler path.
  bool init(void* device, void* commandQueue,
            uint32_t inputW, uint32_t inputH,
            uint32_t outputW, uint32_t outputH) noexcept;

  // eyeIndex: 0 (left) or 1 (right). Returns the upscaled output texture
  // (id<MTLTexture> as void*, NOT retained for caller — owned by upscaler).
  // The returned texture is dimensioned outputW x outputH. Submits and
  // waits internally so the texture is GPU-ready when ownership returns.
  // Returns nullptr on failure.
  void* upscaleEye(int eyeIndex, void* inputTex) noexcept;

  // Width / height of the output textures (== full per-eye dims).
  uint32_t outputWidth() const noexcept { return outputW_; }
  uint32_t outputHeight() const noexcept { return outputH_; }

  // Whether MetalFX path is active (vs. linear blit fallback).
  bool usingMetalFx() const noexcept { return metalFxAvailable_; }

  // Microseconds spent in the most recent upscaleEye() call (CPU+GPU wait).
  // Read by the 1Hz logging in session.cpp.
  uint64_t lastFrameUs() const noexcept { return lastFrameUs_; }

  void shutdown() noexcept;

 private:
  void* impl_{nullptr};  // Opaque ObjC state.
  uint32_t inputW_{0};
  uint32_t inputH_{0};
  uint32_t outputW_{0};
  uint32_t outputH_{0};
  bool metalFxAvailable_{false};
  uint64_t lastFrameUs_{0};
};

}  // namespace fuvr::runtime
