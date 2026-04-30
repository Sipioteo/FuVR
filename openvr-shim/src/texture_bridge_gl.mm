// SPDX-License-Identifier: Apache-2.0
#include "texture_bridge.hpp"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <IOSurface/IOSurface.h>
#include <CoreFoundation/CoreFoundation.h>
#include <OpenGL/gl.h>
#include <OpenGL/gl3.h>
#include <OpenGL/CGLContext.h>
#include <OpenGL/CGLCurrent.h>
#include <OpenGL/CGLIOSurface.h>

#include <array>
#include <atomic>

#include "log.hpp"

namespace fuvr::openvr_shim {

namespace {

constexpr uint32_t kRingDepth = 3;

struct GLEyeRing {
  std::array<IOSurfaceRef, kRingDepth> surfaces{};
  std::array<GLuint, kRingDepth> textures{};
  uint32_t index = 0;
};

struct GLSbsRing {
  std::array<IOSurfaceRef, kRingDepth> surfaces{};
  std::array<id<MTLTexture>, kRingDepth> textures{};
  uint32_t index = 0;
};

class GLBridge final : public TextureBridge {
 public:
  ~GLBridge() override {
    for (auto& ring : rings_) {
      for (uint32_t i = 0; i < kRingDepth; ++i) {
        if (ring.textures[i]) glDeleteTextures(1, &ring.textures[i]);
        if (ring.surfaces[i]) CFRelease(ring.surfaces[i]);
      }
    }
    if (fbo_src_) glDeleteFramebuffers(1, &fbo_src_);
    if (fbo_dst_) glDeleteFramebuffers(1, &fbo_dst_);
    for (uint32_t i = 0; i < kRingDepth; ++i) {
      if (sbs_.surfaces[i]) CFRelease(sbs_.surfaces[i]);
      sbs_.textures[i] = nil;
    }
    eyeStashSurfaces_[0] = eyeStashSurfaces_[1] = nullptr;
    queue_ = nil;
    device_ = nil;
  }

  bool prepare(uint32_t width, uint32_t height) override {
    if (prepared_ && width == width_ && height == height_) return true;
    width_ = width;
    height_ = height;

    CGLContextObj ctx = CGLGetCurrentContext();
    if (ctx == nullptr) {
      FUVR_LOG("gl: no current CGL context — game must call Submit on its GL thread");
      return false;
    }

    for (uint32_t eye = 0; eye < 2; ++eye) {
      auto& ring = rings_[eye];
      for (uint32_t i = 0; i < kRingDepth; ++i) {
        if (ring.surfaces[i]) {
          CFRelease(ring.surfaces[i]);
          ring.surfaces[i] = nullptr;
        }
        if (ring.textures[i]) {
          glDeleteTextures(1, &ring.textures[i]);
          ring.textures[i] = 0;
        }
        ring.surfaces[i] = createIOSurface(width, height);
        if (!ring.surfaces[i]) {
          FUVR_LOG("gl: IOSurface creation failed");
          return false;
        }
        glGenTextures(1, &ring.textures[i]);
        glBindTexture(GL_TEXTURE_RECTANGLE, ring.textures[i]);
        // Why GL_TEXTURE_RECTANGLE: CGLTexImageIOSurface2D on macOS only
        // accepts rectangle targets — it cannot bind to GL_TEXTURE_2D.
        CGLError err = CGLTexImageIOSurface2D(
            ctx,
            GL_TEXTURE_RECTANGLE,
            GL_RGBA,
            width, height,
            GL_BGRA,
            GL_UNSIGNED_INT_8_8_8_8_REV,
            ring.surfaces[i],
            /*plane*/ 0);
        glBindTexture(GL_TEXTURE_RECTANGLE, 0);
        if (err != kCGLNoError) {
          FUVR_LOG("gl: CGLTexImageIOSurface2D failed: %d", (int)err);
          return false;
        }
      }
      ring.index = 0;
    }

    if (!fbo_src_) glGenFramebuffers(1, &fbo_src_);
    if (!fbo_dst_) glGenFramebuffers(1, &fbo_dst_);

    prepared_ = true;
    return true;
  }

  BridgeFrame copyFromTexture(uint32_t eye,
                              const vr::Texture_t* tex,
                              const vr::VRTextureBounds_t* bounds) override {
    BridgeFrame out{0, nullptr};
    if (!prepared_ || eye > 1 || !tex) return out;
    if (tex->eType != vr::TextureType_OpenGL) {
      FUVR_LOG("gl: ignoring non-GL texture type %d", (int)tex->eType);
      return out;
    }
    GLuint srcTex = static_cast<GLuint>(reinterpret_cast<uintptr_t>(tex->handle));
    if (srcTex == 0) return out;

    // Log first 3 submits per eye to see exactly what Vivecraft passes:
    // is the GL handle the SAME for L/R (single-texture overwrite, requires
    // fence between submits) or DIFFERENT (truly stereo)?
    static std::atomic<uint32_t> seen_calls[2]{0, 0};
    uint32_t n = seen_calls[eye & 1].fetch_add(1, std::memory_order_relaxed);
    if (n < 3) {
      FUVR_LOG("texture_bridge_gl: submit eye=%u call=%u srcTex=%u bounds=%s",
               eye, n, (unsigned)srcTex, bounds ? "yes" : "null");
    }

    auto& ring = rings_[eye];
    uint32_t idx = ring.index;
    ring.index = (ring.index + 1) % kRingDepth;

    // Save state we're about to mutate.
    GLint prev_read_fbo = 0, prev_draw_fbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);

    // Source FBO: attach the game's GL_TEXTURE_2D handle. Detach first
    // because Apple's deprecated GL has a known bug where re-attaching
    // the same FBO color slot can keep the previous texture binding;
    // the symptom is "right eye shows left eye content" since both
    // submits hit fbo_src_ in quick succession.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_src_);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, srcTex, /*level*/ 0);

    // Destination FBO: attach our IOSurface-backed rectangle texture.
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo_dst_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE, 0, 0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_RECTANGLE, ring.textures[idx], 0);

    // Decide Y-flip per OpenVR's GL convention:
    //   1. eType != OpenGL                       → no default flip (top-left).
    //   2. bounds == nullptr && eType == OpenGL  → default GL origin = bottom-left → flip.
    //   3. bounds != nullptr && vMin <= vMax     → game pre-flipped or top-left → no flip.
    //   4. bounds != nullptr && vMin >  vMax     → explicit OpenVR flip flag → flip.
    //
    // U-axis: bounds->uMin/uMax MUST be honored. Vivecraft's macOS path
    // is single-pass stereo: both eyes' Submit calls reference the SAME
    // GL texture handle, distinguished by uMin/uMax (left = 0..0.5,
    // right = 0.5..1.0 typically). Ignoring this caused both halves of
    // our SBS to receive the FULL Vivecraft SBS, which the user
    // perceives as "the same image in both eyes" / cross-eyed
    // convergence. We crop in source-pixel space using the source
    // texture's actual dimensions (queried via glGetTexLevelParameteriv
    // — robust whether Vivecraft uses one wide texture or two
    // per-eye-sized textures).
    float uMin = 0.0f, uMax = 1.0f, vMin = 0.0f, vMax = 1.0f;
    bool flipY;
    if (!bounds) {
      flipY = true;  // GL default origin is bottom-left.
    } else {
      uMin = bounds->uMin;
      uMax = bounds->uMax;
      vMin = bounds->vMin;
      vMax = bounds->vMax;
      flipY = (vMin > vMax);
    }

    // Query the source texture's true size. If the game passes a
    // per-eye texture, srcW will be ~width_; if single-pass SBS, srcW
    // will be ~width_*2.
    GLint srcW = (GLint)width_, srcH = (GLint)height_;
    glBindTexture(GL_TEXTURE_2D, srcTex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &srcW);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &srcH);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (srcW <= 0) srcW = (GLint)width_;
    if (srcH <= 0) srcH = (GLint)height_;

    static std::atomic<bool> logged_first_frame{false};
    bool expected = false;
    if (logged_first_frame.compare_exchange_strong(expected, true)) {
      FUVR_LOG("texture_bridge_gl: first frame eye=%u type=%d srcSize=%dx%d "
               "bounds=%s (uMin=%.3f uMax=%.3f vMin=%.3f vMax=%.3f) -> flipY=%d",
               eye, (int)tex->eType, (int)srcW, (int)srcH,
               bounds ? "yes" : "null", uMin, uMax, vMin, vMax, flipY ? 1 : 0);
    }

    // Source rect from bounds. Y stays flipped via swap of sy0/sy1
    // depending on origin convention; X uses uMin/uMax to crop the
    // correct half of single-pass-SBS sources.
    int sx0 = (int)(uMin * (float)srcW + 0.5f);
    int sx1 = (int)(uMax * (float)srcW + 0.5f);
    if (sx1 <= sx0) { sx0 = 0; sx1 = srcW; }  // safety: degenerate bounds → full
    int sy0, sy1;
    if (flipY) {
      // bottom-left origin: vMin/vMax (or default 0..1) maps to bottom..top;
      // we want to read top→bottom so the destination top-left convention is satisfied.
      sy0 = (int)((1.0f - (bounds ? vMin : 0.0f)) * (float)srcH + 0.5f);
      sy1 = (int)((1.0f - (bounds ? vMax : 1.0f)) * (float)srcH + 0.5f);
    } else {
      sy0 = (int)((bounds ? vMin : 0.0f) * (float)srcH + 0.5f);
      sy1 = (int)((bounds ? vMax : 1.0f) * (float)srcH + 0.5f);
    }
    glBlitFramebuffer(sx0, sy0, sx1, sy1,
                      0, 0, (GLint)width_, (GLint)height_,
                      GL_COLOR_BUFFER_BIT,
                      GL_LINEAR);

    // Restore.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prev_read_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prev_draw_fbo);

    // Force the GL pipeline to *complete* this blit before returning. We
    // previously called glFlush() which is a hint, not a barrier — on
    // macOS GL it left the read of `srcTex` async, so when Vivecraft
    // immediately rendered the next eye into the same texture handle our
    // blit could pick up the wrong frame and the right eye would receive
    // the (already-overwritten-by-then) left content. glFinish here is
    // heavy but correct; revisit with glFenceSync once stable.
    glFinish();

    out.surface = ring.surfaces[idx];
    out.token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    // Stash the per-eye IOSurface so the upcoming finalizeStereoFrame()
    // composite (Metal-side) can read both halves and write the SBS
    // surface. Per-eye copy stays as scratch — only the SBS surface is
    // shipped over XPC in the new pipeline.
    if (eye < 2) eyeStashSurfaces_[eye] = ring.surfaces[idx];
    return out;
  }

  BridgeFrame finalizeStereoFrame() override {
    BridgeFrame out{0, nullptr};
    if (!prepared_) return out;
    if (!eyeStashSurfaces_[0] || !eyeStashSurfaces_[1]) {
      // L without R (or vice versa) — drop, the next pair recovers.
      eyeStashSurfaces_[0] = eyeStashSurfaces_[1] = nullptr;
      return out;
    }
    if (!ensureMetal()) return out;
    if (!ensureSbsSlot()) return out;

    uint32_t idx = sbs_.index;
    sbs_.index = (sbs_.index + 1) % kRingDepth;
    id<MTLTexture> dst = sbs_.textures[idx];
    if (!dst) return out;

    // Wrap each per-eye IOSurface as a transient MTLTexture and blit
    // into the SBS surface. The IOSurface is shared memory — the GL
    // writes are already flushed (see glFlush() above) and Metal
    // sees them through IOSurface's coherence guarantees.
    id<MTLTexture> srcTex[2] = {nil, nil};
    for (uint32_t eye = 0; eye < 2; ++eye) {
      MTLTextureDescriptor* d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:width_
                                      height:height_
                                   mipmapped:NO];
      d.usage = MTLTextureUsageShaderRead;
      d.storageMode = MTLStorageModeShared;
      srcTex[eye] = [device_ newTextureWithDescriptor:d
                                            iosurface:eyeStashSurfaces_[eye]
                                                plane:0];
      if (!srcTex[eye]) {
        eyeStashSurfaces_[0] = eyeStashSurfaces_[1] = nullptr;
        return out;
      }
    }

    id<MTLCommandBuffer> cb = [queue_ commandBuffer];
    cb.label = @"FuVR.shim.sbs.composite";
    id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
    MTLSize eyeSize = MTLSizeMake(width_, height_, 1);
    for (uint32_t eye = 0; eye < 2; ++eye) {
      [blit copyFromTexture:srcTex[eye]
                sourceSlice:0
                sourceLevel:0
               sourceOrigin:MTLOriginMake(0, 0, 0)
                 sourceSize:eyeSize
                  toTexture:dst
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake(eye * width_, 0, 0)];
    }
    [blit endEncoding];
    [cb commit];

    eyeStashSurfaces_[0] = eyeStashSurfaces_[1] = nullptr;

    out.surface = sbs_.surfaces[idx];
    out.token = nextToken_.fetch_add(1, std::memory_order_relaxed);
    return out;
  }

 private:
  bool ensureMetal() {
    if (device_) return true;
    device_ = MTLCreateSystemDefaultDevice();
    if (!device_) {
      FUVR_LOG("gl: MTLCreateSystemDefaultDevice failed");
      return false;
    }
    queue_ = [device_ newCommandQueue];
    queue_.label = @"FuVR.openvr_shim.gl-sbs";
    return queue_ != nil;
  }

  bool ensureSbsSlot() {
    if (sbs_.textures[0]) return true;
    uint32_t sbsW = width_ * 2;
    uint32_t sbsH = height_;
    for (uint32_t i = 0; i < kRingDepth; ++i) {
      NSDictionary* props = @{
        (id)kIOSurfaceWidth:           @(sbsW),
        (id)kIOSurfaceHeight:          @(sbsH),
        (id)kIOSurfaceBytesPerElement: @(4),
        (id)kIOSurfacePixelFormat:     @((unsigned)'BGRA'),
        (id)kIOSurfaceIsGlobal:        @YES,
      };
      IOSurfaceRef surf = IOSurfaceCreate((CFDictionaryRef)props);
      if (!surf) {
        FUVR_LOG("gl: SBS IOSurfaceCreate failed");
        return false;
      }
      MTLTextureDescriptor* d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                       width:sbsW
                                      height:sbsH
                                   mipmapped:NO];
      d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      d.storageMode = MTLStorageModeShared;
      id<MTLTexture> mtl = [device_ newTextureWithDescriptor:d
                                                   iosurface:surf
                                                       plane:0];
      if (!mtl) {
        CFRelease(surf);
        FUVR_LOG("gl: SBS newTextureWithDescriptor failed");
        return false;
      }
      sbs_.surfaces[i] = surf;
      sbs_.textures[i] = mtl;
    }
    return true;
  }

  static IOSurfaceRef createIOSurface(uint32_t w, uint32_t h) {
    NSDictionary* props = @{
      (id)kIOSurfaceWidth:           @(w),
      (id)kIOSurfaceHeight:          @(h),
      (id)kIOSurfaceBytesPerElement: @(4),
      (id)kIOSurfacePixelFormat:     @((unsigned)'BGRA'),
      (id)kIOSurfaceIsGlobal:        @YES,
    };
    return IOSurfaceCreate((CFDictionaryRef)props);
  }

  std::array<GLEyeRing, 2> rings_{};
  GLSbsRing sbs_{};
  std::array<IOSurfaceRef, 2> eyeStashSurfaces_{nullptr, nullptr};
  id<MTLDevice> device_{nil};
  id<MTLCommandQueue> queue_{nil};
  GLuint fbo_src_{0};
  GLuint fbo_dst_{0};
  uint32_t width_{0};
  uint32_t height_{0};
  bool prepared_{false};
  std::atomic<uint64_t> nextToken_{1};
};

}  // namespace

std::unique_ptr<TextureBridge> makeGLBridge() {
  return std::make_unique<GLBridge>();
}

}  // namespace fuvr::openvr_shim
