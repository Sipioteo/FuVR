// SPDX-License-Identifier: Apache-2.0
#include "texture_bridge.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <atomic>

#include "log.hpp"

namespace fuvr::openvr_shim {

namespace {

constexpr uint32_t kRingDepth = 3;

struct MetalEyeRing {
  std::array<IOSurfaceRef, kRingDepth> surfaces{};
  std::array<id<MTLTexture>, kRingDepth> textures{};
  uint32_t index = 0;
};

struct MetalSbsRing {
  std::array<IOSurfaceRef, kRingDepth> surfaces{};
  std::array<id<MTLTexture>, kRingDepth> textures{};
  uint32_t index = 0;
};

class MetalBridge final : public TextureBridge {
 public:
  ~MetalBridge() override {
    for (auto& ring : rings_) {
      for (uint32_t i = 0; i < kRingDepth; ++i) {
        if (ring.surfaces[i]) CFRelease(ring.surfaces[i]);
        ring.textures[i] = nil;
      }
    }
    for (uint32_t i = 0; i < kRingDepth; ++i) {
      if (sbs_.surfaces[i]) CFRelease(sbs_.surfaces[i]);
      sbs_.textures[i] = nil;
    }
    eyeStash_[0] = eyeStash_[1] = nil;
    queue_ = nil;
    device_ = nil;
  }

  bool prepare(uint32_t width, uint32_t height) override {
    if (prepared_ && width == width_ && height == height_) return true;
    width_ = width;
    height_ = height;
    // Device + queue are deferred until the first Submit so we can adopt
    // the game's MTLDevice (avoids cross-device copies).
    return true;
  }

  BridgeFrame copyFromTexture(uint32_t eye,
                              const vr::Texture_t* tex,
                              const vr::VRTextureBounds_t* bounds) override {
    BridgeFrame out{0, nullptr};
    if (eye > 1 || !tex || !tex->handle) return out;
    if (tex->eType != vr::TextureType_Metal &&
        tex->eType != vr::TextureType_IOSurface) {
      FUVR_LOG("metal: ignoring non-Metal texture type %d", (int)tex->eType);
      return out;
    }

    // IOSurface input: wrap as a transient MTLTexture and blit into our
    // ring slot, so the SBS finalize step can composite both eyes
    // uniformly. (Pre-stereo-composite, this path forwarded the surface
    // unchanged — but the daemon side now requires SBS frames only.)
    if (tex->eType == vr::TextureType_IOSurface) {
      if (!device_) {
        device_ = MTLCreateSystemDefaultDevice();
        if (!device_) return out;
        queue_ = [device_ newCommandQueue];
        queue_.label = @"FuVR.openvr_shim.blit";
        if (!queue_) return out;
      }
      IOSurfaceRef srcSurf = (IOSurfaceRef)tex->handle;
      MTLTextureDescriptor* d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:IOSurfaceGetWidth(srcSurf)
                                      height:IOSurfaceGetHeight(srcSurf)
                                   mipmapped:NO];
      d.usage = MTLTextureUsageShaderRead;
      d.storageMode = MTLStorageModeShared;
      id<MTLTexture> wrapped = [device_ newTextureWithDescriptor:d
                                                       iosurface:srcSurf
                                                           plane:0];
      if (!wrapped) return out;

      auto& ring = rings_[eye];
      uint32_t idx = ring.index;
      ring.index = (ring.index + 1) % kRingDepth;
      if (!ring.textures[idx] && !allocSlot(ring, idx)) return out;

      id<MTLCommandBuffer> cb = [queue_ commandBuffer];
      cb.label = @"FuVR.shim.iosurface-copy";
      id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
      MTLSize sz = MTLSizeMake(MIN((NSUInteger)width_, wrapped.width),
                               MIN((NSUInteger)height_, wrapped.height), 1);
      [blit copyFromTexture:wrapped
                sourceSlice:0
                sourceLevel:0
               sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:sz
                  toTexture:ring.textures[idx]
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake(0, 0, 0)];
      [blit endEncoding];
      [cb commit];
      (void)bounds;
      out.surface = ring.surfaces[idx];
      out.token = nextToken_.fetch_add(1, std::memory_order_relaxed);
      eyeStash_[eye] = ring.textures[idx];
      eyeStashed_[eye] = true;
      return out;
    }

    id<MTLTexture> srcTex = (__bridge id<MTLTexture>)tex->handle;
    if (!srcTex) return out;

    // Lazily adopt the game's device on first frame.
    if (!device_) {
      device_ = srcTex.device;
      queue_ = [device_ newCommandQueue];
      queue_.label = @"FuVR.openvr_shim.blit";
      if (!queue_) {
        FUVR_LOG("metal: newCommandQueue failed");
        return out;
      }
    }
    if (srcTex.device != device_) {
      // Cross-device copy is expensive and rare; in legacy games it
      // shouldn't happen because they only have one MTLDevice.
      FUVR_LOG("metal: src texture is on a different MTLDevice — skipping");
      return out;
    }

    auto& ring = rings_[eye];
    uint32_t idx = ring.index;
    ring.index = (ring.index + 1) % kRingDepth;
    if (!ring.textures[idx] && !allocSlot(ring, idx)) return out;
    if (!ring.textures[idx]) return out;

    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    cb.label = @"FuVR.shim.copy";
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];

    // Optionally honour bounds — in practice OpenVR Metal submitters use
    // the full texture, and the cropping math in MTL coordinates is
    // verbose, so we copy the full extent and let the daemon side honour
    // bounds via the wire payload.
    (void)bounds;

    MTLOrigin originSrc = MTLOriginMake(0, 0, 0);
    MTLOrigin originDst = MTLOriginMake(0, 0, 0);
    MTLSize size = MTLSizeMake(MIN(srcTex.width, (NSUInteger)width_),
                               MIN(srcTex.height, (NSUInteger)height_),
                               1);
    [blit copyFromTexture:srcTex
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:originSrc
               sourceSize:size
                toTexture:ring.textures[idx]
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:originDst];
    [blit endEncoding];
    [cb commit];
    // Don't `waitUntilCompleted` — the daemon polls IOSurface readiness
    // via VTCompressionSession and does its own GPU sync.
    out.surface = ring.surfaces[idx];
    out.token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    // Stash the per-eye Metal texture for the upcoming finalizeStereoFrame()
    // composite. We hold the IOSurface-backed texture, not the source —
    // it lives in our ring and won't be reused until the ring wraps.
    eyeStash_[eye] = ring.textures[idx];
    eyeStashed_[eye] = true;
    return out;
  }

  BridgeFrame finalizeStereoFrame() override {
    BridgeFrame out{0, nullptr};
    if (!device_ || !queue_) return out;
    if (!eyeStashed_[0] || !eyeStashed_[1]) {
      // L without R (or vice versa) — nothing to composite.
      eyeStashed_[0] = eyeStashed_[1] = false;
      return out;
    }
    if (!ensureSbsSlot()) return out;

    uint32_t idx = sbs_.index;
    sbs_.index = (sbs_.index + 1) % kRingDepth;
    id<MTLTexture> dst = sbs_.textures[idx];
    if (!dst) return out;

    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    cb.label = @"FuVR.shim.sbs.composite";
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];

    MTLSize eyeSize = MTLSizeMake(width_, height_, 1);
    for (uint32_t eye = 0; eye < 2; ++eye) {
      id<MTLTexture> src = eyeStash_[eye];
      if (!src) continue;
      MTLOrigin srcOrigin = MTLOriginMake(0, 0, 0);
      MTLOrigin dstOrigin = MTLOriginMake(eye * width_, 0, 0);
      [blit copyFromTexture:src
                sourceSlice:0
                sourceLevel:0
               sourceOrigin:srcOrigin
                 sourceSize:eyeSize
                  toTexture:dst
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:dstOrigin];
    }
    [blit endEncoding];
    [cb commit];

    eyeStash_[0] = eyeStash_[1] = nil;
    eyeStashed_[0] = eyeStashed_[1] = false;

    out.surface = sbs_.surfaces[idx];
    out.token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    return out;
  }

 private:
  bool ensureSbsSlot() {
    for (uint32_t i = 0; i < kRingDepth; ++i) {
      if (sbs_.textures[i]) continue;
      uint32_t sbsW = width_ * 2;
      uint32_t sbsH = height_;
      NSDictionary* props = @{
        (id)kIOSurfaceWidth:           @(sbsW),
        (id)kIOSurfaceHeight:          @(sbsH),
        (id)kIOSurfaceBytesPerElement: @(4),
        (id)kIOSurfacePixelFormat:     @((unsigned)'BGRA'),
        (id)kIOSurfaceIsGlobal:        @YES,
      };
      IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)props);
      if (!surf) {
        FUVR_LOG("metal: SBS IOSurfaceCreate failed");
        return false;
      }
      MTLTextureDescriptor* d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:sbsW
                                      height:sbsH
                                   mipmapped:NO];
      d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      d.storageMode = MTLStorageModePrivate;
      id<MTLTexture> mtl = [device_ newTextureWithDescriptor:d
                                                   iosurface:surf
                                                       plane:0];
      if (!mtl) {
        CFRelease(surf);
        FUVR_LOG("metal: SBS newTextureWithDescriptor failed");
        return false;
      }
      sbs_.surfaces[i] = surf;
      sbs_.textures[i] = mtl;
    }
    return sbs_.textures[0] != nil;
  }

  bool allocSlot(MetalEyeRing& ring, uint32_t idx) {
    NSDictionary* props = @{
      (id)kIOSurfaceWidth:           @(width_),
      (id)kIOSurfaceHeight:          @(height_),
      (id)kIOSurfaceBytesPerElement: @(4),
      (id)kIOSurfacePixelFormat:     @((unsigned)'BGRA'),
      (id)kIOSurfaceIsGlobal:        @YES,
    };
    IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)props);
    if (!surf) {
      FUVR_LOG("metal: IOSurfaceCreate failed");
      return false;
    }
    MTLTextureDescriptor* d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:width_
                                    height:height_
                                 mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> mtl = [device_ newTextureWithDescriptor:d
                                                 iosurface:surf
                                                     plane:0];
    if (!mtl) {
      CFRelease(surf);
      FUVR_LOG("metal: newTextureWithDescriptor:iosurface: failed");
      return false;
    }
    ring.surfaces[idx] = surf;
    ring.textures[idx] = mtl;
    return true;
  }

  std::array<MetalEyeRing, 2> rings_{};
  MetalSbsRing sbs_{};
  std::array<id<MTLTexture>, 2> eyeStash_{nil, nil};
  std::array<bool, 2> eyeStashed_{false, false};
  id<MTLDevice> device_{nil};
  id<MTLCommandQueue> queue_{nil};
  uint32_t width_{0};
  uint32_t height_{0};
  bool prepared_{false};
  std::atomic<uint64_t> nextToken_{1ull << 40};  // disjoint from GL bridge
};

}  // namespace

std::unique_ptr<TextureBridge> makeMetalBridge() {
  return std::make_unique<MetalBridge>();
}

}  // namespace fuvr::openvr_shim
