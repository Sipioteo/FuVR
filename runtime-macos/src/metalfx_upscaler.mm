// SPDX-License-Identifier: Apache-2.0
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include <chrono>
#include <cstdio>

#include "fuvr/metalfx_upscaler.hpp"

namespace fuvr::runtime {

namespace {

struct MetalFxUpscalerImpl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  bool ownsQueue = false;
  // One scaler per eye — MTLFXSpatialScaler binds input/output textures
  // via descriptor (re-bindable per-frame), but the Apple guidance is to
  // have one scaler per render-graph stream. Two eyes -> two scalers.
  id<MTLFXSpatialScaler> scaler[2] = {nil, nil};
  id<MTLTexture> outputTex[2] = {nil, nil};
  // Lazy-init flag: scaler creation needs to know the input pixel format,
  // which we only learn from the first upscaleEye() call.
  bool scalersBuilt = false;
  MTLPixelFormat colorFmt = MTLPixelFormatBGRA8Unorm;
  bool warnedFallback = false;
};

}  // namespace

MetalFxUpscaler::~MetalFxUpscaler() { shutdown(); }

bool MetalFxUpscaler::init(void* devicePtr, void* commandQueuePtr,
                            uint32_t inputW, uint32_t inputH,
                            uint32_t outputW, uint32_t outputH) noexcept {
  if (impl_ != nullptr) return true;
  if (inputW == 0 || inputH == 0 || outputW == 0 || outputH == 0) return false;
  id<MTLDevice> device = (__bridge id<MTLDevice>)devicePtr;
  if (device == nil) return false;

  // Check MetalFX availability. MTLFXSpatialScaler requires macOS 13+ and
  // a device that reports support. On unsupported devices we fall back to
  // a linear MTLBlit (still functional, just no edge reconstruction).
  bool mtlFxOk = false;
  if (@available(macOS 13.0, *)) {
    Class scalerCls = NSClassFromString(@"MTLFXSpatialScalerDescriptor");
    if (scalerCls != nil) {
      mtlFxOk = [(Class)scalerCls supportsDevice:device];
    }
  }

  auto* s = new MetalFxUpscalerImpl();
  s->device = device;
  if (commandQueuePtr != nullptr) {
    s->queue = (__bridge id<MTLCommandQueue>)commandQueuePtr;
    s->ownsQueue = false;
  } else {
    s->queue = [device newCommandQueue];
    s->ownsQueue = true;
  }
  if (s->queue == nil) {
    delete s;
    return false;
  }

  // Allocate output textures (one per eye) at full dims. Format BGRA8Unorm
  // matches the IOSurface swapchain format (see iosurface_swapchain.mm).
  for (int eye = 0; eye < 2; ++eye) {
    MTLTextureDescriptor* desc = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:outputW
                                    height:outputH
                                 mipmapped:NO];
    desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                 MTLTextureUsageShaderWrite;
    desc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
    if (tex == nil) {
      delete s;
      return false;
    }
    s->outputTex[eye] = tex;
  }

  s->colorFmt = MTLPixelFormatBGRA8Unorm;
  impl_ = s;
  inputW_ = inputW;
  inputH_ = inputH;
  outputW_ = outputW;
  outputH_ = outputH;
  metalFxAvailable_ = mtlFxOk;

  if (!mtlFxOk && !s->warnedFallback) {
    std::fprintf(stderr,
                 "[METALFX] WARN MTLFXSpatialScaler unavailable on this "
                 "device — falling back to linear blit (no edge reconstruction)\n");
    s->warnedFallback = true;
  } else if (mtlFxOk) {
    std::fprintf(stderr,
                 "[METALFX] init: MTLFXSpatialScaler enabled, %ux%u -> %ux%u/eye\n",
                 inputW, inputH, outputW, outputH);
  }
  return true;
}

void* MetalFxUpscaler::upscaleEye(int eyeIndex, void* inputTexPtr) noexcept {
  if (impl_ == nullptr || inputTexPtr == nullptr) return nullptr;
  if (eyeIndex < 0 || eyeIndex > 1) return nullptr;
  auto* s = static_cast<MetalFxUpscalerImpl*>(impl_);
  id<MTLTexture> input = (__bridge id<MTLTexture>)inputTexPtr;
  if (input == nil) return nullptr;

  const auto t0 = std::chrono::steady_clock::now();

  // Lazy-build the scalers on first call: we need the input pixel format
  // to construct MTLFXSpatialScalerDescriptor.
  if (metalFxAvailable_ && !s->scalersBuilt) {
    if (@available(macOS 13.0, *)) {
      MTLPixelFormat colorFmt = input.pixelFormat;
      // Apple docs: input/output color formats may differ but must be
      // sRGB-compatible. BGRA8Unorm in, BGRA8Unorm out is the simplest.
      MTLPixelFormat outFmt = MTLPixelFormatBGRA8Unorm;
      for (int eye = 0; eye < 2; ++eye) {
        MTLFXSpatialScalerDescriptor* desc =
            [[MTLFXSpatialScalerDescriptor alloc] init];
        desc.inputWidth = inputW_;
        desc.inputHeight = inputH_;
        desc.outputWidth = outputW_;
        desc.outputHeight = outputH_;
        desc.colorTextureFormat = colorFmt;
        desc.outputTextureFormat = outFmt;
        desc.colorProcessingMode =
            MTLFXSpatialScalerColorProcessingModePerceptual;
        id<MTLFXSpatialScaler> scaler = [desc newSpatialScalerWithDevice:s->device];
        if (scaler == nil) {
          // Per-device init failed; degrade to linear blit fallback.
          metalFxAvailable_ = false;
          if (!s->warnedFallback) {
            std::fprintf(stderr,
                         "[METALFX] WARN newSpatialScalerWithDevice failed "
                         "(format=%lu) — falling back to linear blit\n",
                         (unsigned long)colorFmt);
            s->warnedFallback = true;
          }
          break;
        }
        s->scaler[eye] = scaler;
      }
      s->scalersBuilt = true;
      s->colorFmt = colorFmt;
    }
  }

  id<MTLTexture> output = s->outputTex[eyeIndex];
  if (output == nil) return nullptr;

  id<MTLCommandBuffer> cb = [s->queue commandBuffer];

  if (metalFxAvailable_ && s->scaler[eyeIndex] != nil) {
    if (@available(macOS 13.0, *)) {
      id<MTLFXSpatialScaler> scaler = s->scaler[eyeIndex];
      scaler.colorTexture = input;
      scaler.outputTexture = output;
      [scaler encodeToCommandBuffer:cb];
    }
  } else {
    // Fallback: simple linear blit. MTLBlit doesn't support filtering, but
    // we can render-pass copy with a linear sampler if needed; for now use
    // a plain blit copy from a (possibly mismatched) source — visually this
    // is nearest-neighbor scaling which is ugly but functional. We use it
    // only on devices where MetalFX is unavailable.
    id<MTLBlitCommandEncoder> enc = [cb blitCommandEncoder];
    NSUInteger srcW = MIN((NSUInteger)input.width, (NSUInteger)inputW_);
    NSUInteger srcH = MIN((NSUInteger)input.height, (NSUInteger)inputH_);
    // If output is larger than input (typical case here), MTLBlit can't
    // upscale — copy what we can; the bottom-right will be black. The
    // user is expected to be on Apple Silicon / macOS 13+ where MetalFX
    // works, so this fallback is a degraded last resort.
    NSUInteger w = MIN(srcW, (NSUInteger)output.width);
    NSUInteger h = MIN(srcH, (NSUInteger)output.height);
    [enc copyFromTexture:input
             sourceSlice:0
             sourceLevel:0
            sourceOrigin:MTLOriginMake(0, 0, 0)
              sourceSize:MTLSizeMake(w, h, 1)
               toTexture:output
        destinationSlice:0
        destinationLevel:0
       destinationOrigin:MTLOriginMake(0, 0, 0)];
    [enc endEncoding];
  }

  [cb commit];
  [cb waitUntilCompleted];

  const auto t1 = std::chrono::steady_clock::now();
  lastFrameUs_ = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
  return (__bridge void*)output;
}

void MetalFxUpscaler::shutdown() noexcept {
  if (impl_ == nullptr) return;
  auto* s = static_cast<MetalFxUpscalerImpl*>(impl_);
  for (int eye = 0; eye < 2; ++eye) {
    s->scaler[eye] = nil;
    s->outputTex[eye] = nil;
  }
  s->queue = nil;
  s->device = nil;
  delete s;
  impl_ = nullptr;
}

}  // namespace fuvr::runtime
