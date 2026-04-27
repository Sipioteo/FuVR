// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "fuvr/daemon_client.hpp"
#include "fuvr/iosurface_swapchain.hpp"
#include "fuvr/runtime.hpp"
#include "fuvr/xr_fuvr_metal_enable.h"

namespace fuvr::runtime {

namespace detail {
std::unordered_map<uint64_t, Session*>& sessions() noexcept;
uint64_t nextHandleAlloc() noexcept;
std::mutex& globalMutex() noexcept;
}  // namespace detail

XrResult xrCreateSession_impl(XrInstance instance, const XrSessionCreateInfo* info,
                               XrSession* out) noexcept {
  if (std::getenv("FUVR_RT_DEBUG"))
    std::fprintf(stderr, "[fuvr-rt] xrCreateSession(systemId=%llu)\n",
                 info ? (unsigned long long)info->systemId : 0ull);
  Instance* inst = lookupInstance(instance);
  if (inst == nullptr || info == nullptr || out == nullptr) {
    if (std::getenv("FUVR_RT_DEBUG"))
      std::fprintf(stderr, "[fuvr-rt]   -> handle invalid\n");
    return XR_ERROR_HANDLE_INVALID;
  }
  auto session = std::make_unique<Session>();
  session->instance = inst;
  session->state = XR_SESSION_STATE_IDLE;

  void* mtlDevice = nullptr;
  void* mtlCommandQueue = nullptr;
  for (const XrBaseInStructure* p =
           static_cast<const XrBaseInStructure*>(info->next);
       p != nullptr; p = p->next) {
    if (p->type == XR_TYPE_GRAPHICS_BINDING_METAL_KHR) {
      // Why: KHR binding gives a command queue (id<MTLCommandQueue>); we
      // derive the device from queue.device on the Objective-C++ side.
      const auto* b = reinterpret_cast<const XrGraphicsBindingMetalKHR*>(p);
      mtlCommandQueue = b->commandQueue;
      mtlDevice = deviceFromCommandQueue(b->commandQueue);
      break;
    }
    if (p->type == XR_TYPE_GRAPHICS_BINDING_METAL_FUVR) {
      const auto* b = reinterpret_cast<const XrGraphicsBindingMetalFUVR*>(p);
      mtlDevice = b->mtlDevice;
      break;
    }
  }
  // Why: in-process tests and headless tooling fall back to the system
  // default Metal device. Real apps (Blender, Godot) pass it via the KHR
  // binding above.
  if (mtlDevice == nullptr) {
    mtlDevice = defaultMetalDevice();
  }
  session->metalDevice = mtlDevice;
  session->metalCommandQueue = mtlCommandQueue;

  auto daemon = std::make_shared<DaemonClient>();
  session->daemon = daemon;
  session->xpcClient = IOSurfaceXpcClient::create("com.fuvr.daemon.surface");
  daemon->setXpcClient(session->xpcClient.get());
  session->frameSink = makeDaemonFrameSink(daemon.get());

  bool daemonConnected = false;
  if (std::getenv("FUVR_RT_DEBUG"))
    std::fprintf(stderr, "[fuvr-rt]   created Session, attempting daemon connect\n");
  if (daemon->ensureConnected()) {
    if (std::getenv("FUVR_RT_DEBUG"))
      std::fprintf(stderr, "[fuvr-rt]   daemon connected\n");
    daemonConnected = true;
    StartSessionParams params{};
    params.perEyeWidth = 2064;
    params.perEyeHeight = 2208;
    params.refreshRateHz = 90;
    StartSessionResult result{};
    if (daemon->startSession(params, &result)) {
      session->daemonSessionId = result.sessionId;
      daemon->subscribePoses(result.sessionId);
      daemon->subscribeInputs(result.sessionId);
      daemon->subscribeEncodeStats();
    }
    // Pose lookahead: render budget (encode 25 + decode 15 + scanout 5 = 45ms)
    // + measured one-way network delay from daemon's clock-sync handshake.
    // If oneWayDelay is unknown (no Quest peer yet), fall back to the env var
    // FUVR_RT_POSE_LOOKAHEAD_MS, else 70ms. This is a one-shot offset, not an
    // integrator, so it cannot drift.
    {
      constexpr uint64_t kRenderBudgetNs = 45'000'000ull;
      uint64_t lookahead = 0;
      if (result.oneWayDelayNs != 0) {
        lookahead = kRenderBudgetNs + result.oneWayDelayNs;
      } else if (const char* env = std::getenv("FUVR_RT_POSE_LOOKAHEAD_MS")) {
        lookahead = static_cast<uint64_t>(std::strtoull(env, nullptr, 10)) *
                    1'000'000ull;
      } else {
        lookahead = 70'000'000ull;
      }
      session->poseLookaheadNs = lookahead;
      if (std::getenv("FUVR_RT_DEBUG"))
        std::fprintf(stderr,
                     "[fuvr-rt] pose lookahead = %llu ms (oneWayDelay=%llu ns, "
                     "renderBudget=%llu ns)\n",
                     (unsigned long long)(lookahead / 1'000'000ull),
                     (unsigned long long)result.oneWayDelayNs,
                     (unsigned long long)kRenderBudgetNs);
    }
    Session* sessRaw = session.get();
    sessRaw->daemonAlive.store(true);
    daemon->setPoseCallback([sessRaw](const PoseSample& s) {
      sessRaw->predictor.push(s);
    });
    daemon->setEncodeStatsCallback([sessRaw](const EncodeStatSample& s) {
      sessRaw->encoderStats.push(s);
    });
    daemon->setInputCallback([sessRaw](const InputSnapshot& snap) {
      sessRaw->actionState.update(snap);
    });
    daemon->setDisconnectCallback([sessRaw]() {
      sessRaw->daemonAlive.store(false);
      Instance* i = sessRaw->instance;
      if (i != nullptr) {
        i->events.pushSessionStateChanged(sessRaw->handle,
                                          XR_SESSION_STATE_LOSS_PENDING);
      }
    });
    daemon->setReconnectCallback([sessRaw]() {
      sessRaw->daemonAlive.store(true);
      sessRaw->reconnectCount.fetch_add(1, std::memory_order_relaxed);
      // Re-subscribe to streams; the daemon may have lost subscription state.
      if (sessRaw->daemon && sessRaw->daemonSessionId != 0) {
        sessRaw->daemon->subscribePoses(sessRaw->daemonSessionId);
        sessRaw->daemon->subscribeInputs(sessRaw->daemonSessionId);
        sessRaw->daemon->subscribeEncodeStats();
      }
      Instance* i = sessRaw->instance;
      if (i != nullptr) {
        i->events.pushSessionStateChanged(sessRaw->handle,
                                          XR_SESSION_STATE_READY);
      }
    });
  }

  const uint64_t h = detail::nextHandleAlloc();
  session->handle = reinterpret_cast<XrSession>(h);
  Session* raw = session.get();
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::sessions().emplace(h, raw);
  }
  {
    std::lock_guard<std::mutex> lk(inst->mutex);
    inst->sessions.push_back(std::move(session));
  }
  *out = raw->handle;
  if (daemonConnected) {
    inst->events.pushSessionStateChanged(raw->handle, XR_SESSION_STATE_IDLE);
    inst->events.pushSessionStateChanged(raw->handle, XR_SESSION_STATE_READY);
    raw->state = XR_SESSION_STATE_READY;
  }
  return XR_SUCCESS;
}

XrResult xrDestroySession_impl(XrSession sessionHandle) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (s->daemon) {
    s->daemon->shutdown();
  }
  if (s->metalDevice != nullptr) {
    releaseMetalDevice(s->metalDevice);
    s->metalDevice = nullptr;
  }
  {
    std::lock_guard<std::mutex> lk(detail::globalMutex());
    detail::sessions().erase(reinterpret_cast<uint64_t>(sessionHandle));
  }
  Instance* inst = s->instance;
  std::lock_guard<std::mutex> lk(inst->mutex);
  for (auto it = inst->sessions.begin(); it != inst->sessions.end(); ++it) {
    if (it->get() == s) {
      inst->sessions.erase(it);
      break;
    }
  }
  return XR_SUCCESS;
}

XrResult xrBeginSession_impl(XrSession sessionHandle,
                              const XrSessionBeginInfo* info) noexcept {
  if (std::getenv("FUVR_RT_DEBUG"))
    std::fprintf(stderr, "[fuvr-rt] xrBeginSession()\n");
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->state = XR_SESSION_STATE_SYNCHRONIZED;
  Instance* inst = s->instance;
  if (inst != nullptr) {
    inst->events.pushSessionStateChanged(s->handle,
                                          XR_SESSION_STATE_SYNCHRONIZED);
    inst->events.pushSessionStateChanged(s->handle, XR_SESSION_STATE_VISIBLE);
    inst->events.pushSessionStateChanged(s->handle, XR_SESSION_STATE_FOCUSED);
    s->state = XR_SESSION_STATE_FOCUSED;
    if (!s->interactionProfileEmitted) {
      inst->events.pushInteractionProfileChanged(s->handle);
      s->interactionProfileEmitted = true;
    }
  }
  s->beginSessionSent = true;
  return XR_SUCCESS;
}

XrResult xrEndSession_impl(XrSession sessionHandle) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  Instance* inst = s->instance;
  if (inst != nullptr) {
    inst->events.pushSessionStateChanged(s->handle,
                                          XR_SESSION_STATE_STOPPING);
    inst->events.pushSessionStateChanged(s->handle, XR_SESSION_STATE_IDLE);
  }
  s->state = XR_SESSION_STATE_IDLE;
  return XR_SUCCESS;
}

XrResult xrRequestExitSession_impl(XrSession sessionHandle) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->state = XR_SESSION_STATE_EXITING;
  return XR_SUCCESS;
}

XrResult xrWaitFrame_impl(XrSession sessionHandle, const XrFrameWaitInfo*,
                           XrFrameState* state) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || state == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  using clock = std::chrono::steady_clock;
  const auto now = clock::now().time_since_epoch();
  // predictedDisplayTime targets the moment this frame will actually be
  // photons-on-eyes on the Quest. That is now + one frame period (compositor)
  // + end-to-end pipeline latency (encode + transport + decode + scanout),
  // which we capture in poseLookaheadNs. xrLocateViews will pass this same
  // forward time into the predictor so Blender renders for the future pose.
  state->predictedDisplayTime = static_cast<XrTime>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() +
      11'111'111 + static_cast<int64_t>(s->poseLookaheadNs));
  state->predictedDisplayPeriod = 11'111'111;
  state->shouldRender = XR_TRUE;
  return XR_SUCCESS;
}

XrResult xrBeginFrame_impl(XrSession sessionHandle,
                            const XrFrameBeginInfo*) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  s->frameId.fetch_add(1, std::memory_order_relaxed);
  return XR_SUCCESS;
}

XrResult xrEndFrame_impl(XrSession sessionHandle,
                          const XrFrameEndInfo* info) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (std::getenv("FUVR_RT_DEBUG")) {
    static std::atomic<uint64_t> endFrameCount{0};
    uint64_t n = endFrameCount.fetch_add(1, std::memory_order_relaxed);
    if (n < 5 || (n % 90) == 0)
      std::fprintf(stderr, "[fuvr-rt] xrEndFrame #%llu layers=%u\n",
                   (unsigned long long)n, info->layerCount);
  }
  if (s->frameSink) {
    SubmittedFrame f{};
    f.frameId = s->frameId.load(std::memory_order_relaxed);
    f.targetDisplayTimeNs = static_cast<uint64_t>(info->displayTime);
    f.sessionId = s->daemonSessionId;

    struct ResolvedImage {
      IOSurfaceRef surface{nullptr};
      void* mtlTexture{nullptr};
      uint32_t width{0};
      uint32_t height{0};
    };
    auto resolveSwapchain = [&](XrSwapchain handle) -> ResolvedImage {
      for (auto& sc : s->swapchains) {
        if (sc->handle != handle) continue;
        if (sc->images.empty()) return {};
        const uint32_t idx = sc->lastReleasedIndex %
                             static_cast<uint32_t>(sc->images.size());
        return {sc->images[idx]->surface, sc->images[idx]->mtlTexture,
                sc->width, sc->height};
      }
      return {};
    };

    std::vector<IOSurfaceRef> layers;
    {
      std::lock_guard<std::mutex> lk(s->mutex);
      // Walk every composition layer; extract the primary swapchain image
      // ref. Projection layers (most common) carry per-view subImages; we
      // pick the first view's swapchain. Non-projection layers (quad, etc.)
      // expose .subImage directly.
      for (uint32_t i = 0; i < info->layerCount; ++i) {
        const XrCompositionLayerBaseHeader* layer = info->layers[i];
        if (layer == nullptr) continue;
        IOSurfaceRef surf = nullptr;
        if (layer->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
          const auto* proj =
              reinterpret_cast<const XrCompositionLayerProjection*>(layer);
          // STEREO-SPLIT: when the app provides per-eye views (the spec-
          // mandated case for PRIMARY_STEREO), combine them into a single
          // side-by-side IOSurface that the daemon's encoder consumes as one
          // 2*perEyeWidth x perEyeHeight stream. The Quest GL compositor
          // splits it back into [0,0.5] / [0.5,1] u-ranges per eye (see
          // quest-app/.../eye_blit.cpp).
          if (proj->viewCount >= 2) {
            ResolvedImage L = resolveSwapchain(proj->views[0].subImage.swapchain);
            ResolvedImage R = resolveSwapchain(proj->views[1].subImage.swapchain);
            if (L.mtlTexture != nullptr && R.mtlTexture != nullptr) {
              if (!s->stereoBlitter) {
                s->stereoBlitter = std::make_unique<StereoBlitter>();
                if (!s->stereoBlitter->init(s->metalDevice,
                                             s->metalCommandQueue,
                                             L.width, L.height)) {
                  s->stereoBlitter.reset();
                }
              }
              if (s->stereoBlitter) {
                surf = s->stereoBlitter->blitToCombined(L.mtlTexture,
                                                         R.mtlTexture);
                if (surf != nullptr) {
                  f.width = L.width * 2;
                  f.height = L.height;
                  f.leftMetalTexture = L.mtlTexture;
                  f.rightMetalTexture = R.mtlTexture;
                }
              }
            }
            if (surf == nullptr && proj->viewCount > 0) {
              // Fallback: blitter unavailable or only one eye resolved.
              surf = resolveSwapchain(proj->views[0].subImage.swapchain).surface;
            }
          } else if (proj->viewCount > 0) {
            surf = resolveSwapchain(proj->views[0].subImage.swapchain).surface;
          }
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
          const auto* quad =
              reinterpret_cast<const XrCompositionLayerQuad*>(layer);
          surf = resolveSwapchain(quad->subImage.swapchain).surface;
        }
        if (surf != nullptr) layers.push_back(surf);
      }
      // Why: legacy code path — if no layers were enumerated (e.g. tests
      // that don't pass any XrCompositionLayer*), fall back to the first
      // swapchain so existing M0 spike code keeps working.
      if (layers.empty() && !s->swapchains.empty()) {
        Swapchain* sc = s->swapchains.front().get();
        if (!sc->images.empty()) {
          const uint32_t idx = sc->lastReleasedIndex %
                               static_cast<uint32_t>(sc->images.size());
          layers.push_back(sc->images[idx]->surface);
          f.leftMetalTexture = sc->images[idx]->mtlTexture;
          f.rightMetalTexture = sc->images[idx]->mtlTexture;
          f.width = sc->width;
          f.height = sc->height;
        }
      }
    }

    if (!layers.empty()) {
      f.ioSurface = layers.front();
      for (size_t i = 1; i < layers.size(); ++i) {
        f.extraLayers.push_back(layers[i]);
      }
    }

    // Why: the Quest's ATW shader receives this pose as `q_render` and
    // computes Δq = q_now * q_render⁻¹. The image Blender just rendered
    // used the *predicted* display-time pose (xrLocateViews → predict()),
    // not the latest raw sample. Shipping predictor.latest() instead made
    // Δq carry the full lookahead of head motion, snapping the warp on
    // every move. Use the same predicted pose Blender rendered with.
    auto rendered = s->predictor.predict(static_cast<uint64_t>(info->displayTime));
    if (rendered.has_value()) {
      f.renderedLeft = rendered->leftEye;
      f.renderedRight = rendered->rightEye;
    }
    s->frameSink->submit(f);
  }
  return XR_SUCCESS;
}

XrResult xrLocateViews_impl(XrSession sessionHandle,
                             const XrViewLocateInfo* info, XrViewState* state,
                             uint32_t capacity, uint32_t* countOutput,
                             XrView* views) noexcept {
  Session* s = lookupSession(sessionHandle);
  if (s == nullptr || info == nullptr || countOutput == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  if (views == nullptr || capacity == 0) {
    *countOutput = 2;
    return XR_SUCCESS;
  }
  if (capacity < 2) {
    *countOutput = 2;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  if (state != nullptr) {
    state->viewStateFlags = XR_VIEW_STATE_ORIENTATION_VALID_BIT |
                            XR_VIEW_STATE_POSITION_VALID_BIT |
                            XR_VIEW_STATE_ORIENTATION_TRACKED_BIT |
                            XR_VIEW_STATE_POSITION_TRACKED_BIT;
  }
  auto predicted = s->predictor.predict(static_cast<uint64_t>(info->displayTime));
  // Why: previous code returned a symmetric ±0.95 rad fov for both eyes. The
  // Quest 3 actually has an asymmetric per-eye fov (lenses canted toward the
  // nose), so Blender rendering with the symmetric fov produced a per-eye
  // geometry that didn't match what the headset displays — left/right images
  // refused to fuse stereoscopically. These values approximate Quest 3's
  // per-eye fov (outer ~64°, inner ~46°, vertical ~48°). Plumbing the exact
  // runtime-reported fov through the wire is a follow-up; for now this is
  // close enough that depth perception works and ATW's fov_render→fov_now
  // fall-back stays well under a degree off.
  constexpr float kOuter = 1.117f;  // ~64°
  constexpr float kInner = 0.803f;  // ~46°
  constexpr float kVert  = 0.838f;  // ~48°
  for (uint32_t i = 0; i < 2; ++i) {
    views[i].type = XR_TYPE_VIEW;
    views[i].next = nullptr;
    if (i == 0) {           // left eye: outer-left, inner-right
      views[i].fov.angleLeft  = -kOuter;
      views[i].fov.angleRight = +kInner;
    } else {                // right eye: inner-left, outer-right
      views[i].fov.angleLeft  = -kInner;
      views[i].fov.angleRight = +kOuter;
    }
    views[i].fov.angleUp   = +kVert;
    views[i].fov.angleDown = -kVert;
    if (predicted.has_value()) {
      const Pose& p = (i == 0) ? predicted->leftEye : predicted->rightEye;
      views[i].pose.position = {p.position.x, p.position.y, p.position.z};
      views[i].pose.orientation = {p.orientation.x, p.orientation.y,
                                    p.orientation.z, p.orientation.w};
    } else {
      views[i].pose.position = {0.0f, 0.0f, 0.0f};
      views[i].pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    }
  }
  *countOutput = 2;
  return XR_SUCCESS;
}

XrResult xrEnumerateReferenceSpaces_impl(XrSession sessionHandle,
                                          uint32_t capacity, uint32_t* count,
                                          XrReferenceSpaceType* out) noexcept {
  if (lookupSession(sessionHandle) == nullptr || count == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  static const XrReferenceSpaceType kTypes[] = {
      XR_REFERENCE_SPACE_TYPE_VIEW,
      XR_REFERENCE_SPACE_TYPE_LOCAL,
      XR_REFERENCE_SPACE_TYPE_STAGE,
  };
  const uint32_t total = 3;
  if (out == nullptr || capacity == 0) {
    *count = total;
    return XR_SUCCESS;
  }
  if (capacity < total) {
    *count = total;
    return XR_ERROR_SIZE_INSUFFICIENT;
  }
  for (uint32_t i = 0; i < total; ++i) {
    out[i] = kTypes[i];
  }
  *count = total;
  return XR_SUCCESS;
}

}  // namespace fuvr::runtime
