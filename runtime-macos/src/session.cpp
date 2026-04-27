// SPDX-License-Identifier: Apache-2.0
#include <atomic>
#include <chrono>
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
  Instance* inst = lookupInstance(instance);
  if (inst == nullptr || info == nullptr || out == nullptr) {
    return XR_ERROR_HANDLE_INVALID;
  }
  auto session = std::make_unique<Session>();
  session->instance = inst;
  session->state = XR_SESSION_STATE_IDLE;

  void* mtlDevice = nullptr;
  for (const XrBaseInStructure* p =
           static_cast<const XrBaseInStructure*>(info->next);
       p != nullptr; p = p->next) {
    if (p->type == XR_TYPE_GRAPHICS_BINDING_METAL_FUVR) {
      const auto* b = reinterpret_cast<const XrGraphicsBindingMetalFUVR*>(p);
      mtlDevice = b->mtlDevice;
      break;
    }
  }
  // Why: M0/M1 apps don't yet pass XR_FUVR_metal_enable; fall back to the
  // system default device. M3 apps will provide their own.
  if (mtlDevice == nullptr) {
    mtlDevice = defaultMetalDevice();
  }
  session->metalDevice = mtlDevice;

  auto daemon = std::make_shared<DaemonClient>();
  session->daemon = daemon;
  session->xpcClient = IOSurfaceXpcClient::create("com.fuvr.daemon.surface");
  daemon->setXpcClient(session->xpcClient.get());
  session->frameSink = makeDaemonFrameSink(daemon.get());

  bool daemonConnected = false;
  if (daemon->ensureConnected()) {
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
  state->predictedDisplayTime = static_cast<XrTime>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(now).count() +
      11'111'111);
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
  if (s->frameSink) {
    SubmittedFrame f{};
    f.frameId = s->frameId.load(std::memory_order_relaxed);
    f.targetDisplayTimeNs = static_cast<uint64_t>(info->displayTime);
    f.sessionId = s->daemonSessionId;

    auto resolveSwapchain = [&](XrSwapchain handle) -> IOSurfaceRef {
      for (auto& sc : s->swapchains) {
        if (sc->handle != handle) continue;
        if (sc->images.empty()) return nullptr;
        const uint32_t idx = sc->lastReleasedIndex %
                             static_cast<uint32_t>(sc->images.size());
        return sc->images[idx]->surface;
      }
      return nullptr;
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
          if (proj->viewCount > 0) {
            surf = resolveSwapchain(proj->views[0].subImage.swapchain);
          }
        } else if (layer->type == XR_TYPE_COMPOSITION_LAYER_QUAD) {
          const auto* quad =
              reinterpret_cast<const XrCompositionLayerQuad*>(layer);
          surf = resolveSwapchain(quad->subImage.swapchain);
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

    auto latest = s->predictor.latest();
    if (latest.has_value()) {
      f.renderedLeft = latest->leftEye;
      f.renderedRight = latest->rightEye;
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
  for (uint32_t i = 0; i < 2; ++i) {
    views[i].type = XR_TYPE_VIEW;
    views[i].next = nullptr;
    views[i].fov.angleLeft = -0.95f;
    views[i].fov.angleRight = 0.95f;
    views[i].fov.angleUp = 0.95f;
    views[i].fov.angleDown = -0.95f;
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
