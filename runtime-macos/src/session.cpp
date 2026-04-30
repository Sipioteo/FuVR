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
    // Why: query the daemon for the latest helloFromQuest snapshot so we
    // negotiate at the headset's actual recommended render dims and a refresh
    // rate it actually advertises. Falls back to Quest 3 defaults if no Quest
    // has connected yet (daemon-without-headset case, e.g. CI smoke tests).
    StartSessionParams params{};
    DeviceCapabilities caps{};
    // Scale knobs to lift the Mac-side render-rate ceiling without
    // sacrificing image quality uniformly. Three env vars, all default 1.0:
    //
    //   FUVR_RT_RENDER_SCALE   — uniform scale (legacy / convenience)
    //   FUVR_RT_RENDER_SCALE_X — horizontal-only scale (overrides X)
    //   FUVR_RT_RENDER_SCALE_Y — vertical-only scale   (overrides Y)
    //
    // Lens distortion compresses pixels horizontally near the outer edges
    // more than vertically, so a typical "keep graphics, gain fps" combo
    // is X=0.75 Y=1.0 — render 25% fewer horizontal pixels (33% faster)
    // while keeping full vertical resolution where the eye is most
    // sensitive. The Quest decoder reconfigures from session-start config
    // and the eye_blit shader samples the smaller decoded texture into
    // the 2064x2208 eye swapchain via UVs, so the picture remains correct.
    auto readScale = [](const char* env, float fallback) -> float {
      const char* v = std::getenv(env);
      if (v == nullptr || *v == '\0') return fallback;
      const float f = std::strtof(v, nullptr);
      if (f < 0.25f || f > 1.0f) return fallback;
      return f;
    };
    float uniformScale = readScale("FUVR_RT_RENDER_SCALE", 1.0f);
    float scaleX = readScale("FUVR_RT_RENDER_SCALE_X", uniformScale);
    float scaleY = readScale("FUVR_RT_RENDER_SCALE_Y", uniformScale);
    // METALFX: when FUVR_RT_METALFX=spatial, force Blender to render at
    // half per-eye (the upscaler reconstructs full dim before SBS combine
    // + encode). Lower user-set scales still win — if the user says 0.4 we
    // honor that (they want even cheaper renders). Important asymmetry:
    // here in session.cpp we keep `params.perEye{Width,Height}` at FULL
    // resolution so the encoder/SBS path negotiates 4128x2208 over the
    // wire — matching the upscaled textures we feed it. Only the
    // *Blender-side* swapchain dim (returned from
    // xrEnumerateViewConfigurationViews) is forced to half.
    bool metalFxEnabled = false;
    if (const char* env = std::getenv("FUVR_RT_METALFX")) {
      if (std::strcmp(env, "spatial") == 0) {
        metalFxEnabled = true;
        if (scaleX > 0.5f) scaleX = 0.5f;
        if (scaleY > 0.5f) scaleY = 0.5f;
        std::fprintf(stderr,
                     "[METALFX] enabled (spatial); forcing Blender render "
                     "scale to %.2fx%.2f\n", scaleX, scaleY);
      }
    }
    session->metalFxEnabled = metalFxEnabled;
    auto scaleEvenX = [scaleX](uint32_t v) {
      uint32_t out = static_cast<uint32_t>(v * scaleX);
      return (out + 1u) & ~1u;  // HEVC/H.264 require even dims
    };
    auto scaleEvenY = [scaleY](uint32_t v) {
      uint32_t out = static_cast<uint32_t>(v * scaleY);
      return (out + 1u) & ~1u;
    };
    if (std::getenv("FUVR_RT_DEBUG") || scaleX < 0.999f || scaleY < 0.999f) {
      std::fprintf(stderr,
                   "[fuvr-rt] render scale: X=%.2f Y=%.2f\n", scaleX, scaleY);
    }
    if (daemon->getDeviceCapabilities(&caps) && caps.valid) {
      // Encoder/decoder dims are negotiated via session-start config
      // (Quest reads perEyeWidth/Height and reconfigures decoder), so we
      // can scale freely — Quest swapchain stays 2064x2208, eye_blit
      // samples the smaller decoded texture via UVs.
      // METALFX: keep encoder/SBS dims at FULL — the upscaler will lift
      // half-res Blender textures back to full before they reach the
      // stereo blitter. Without this, the encoder would be configured at
      // half dim and we'd lose the entire point of upscaling.
      if (metalFxEnabled) {
        params.perEyeWidth = ((2064u + 1u) & ~1u);
        params.perEyeHeight = ((2208u + 1u) & ~1u);
      } else {
        params.perEyeWidth = scaleEvenX(2064);
        params.perEyeHeight = scaleEvenY(2208);
      }
      (void)caps.perEyeWidth;
      (void)caps.perEyeHeight;
      // Pick the highest advertised rate <= 120; Quest 3 reports 120 but PCVR
      // streaming over TCP rarely hits 120 stable, so cap at 90 by default.
      // Override via FUVR_RT_REFRESH_HZ if you want to force a specific rate.
      uint32_t rate = 0;
      const char* envRate = std::getenv("FUVR_RT_REFRESH_HZ");
      const uint32_t prefMax = envRate ? static_cast<uint32_t>(
                                              std::strtoul(envRate, nullptr, 10))
                                       : 90u;
      for (uint32_t r : caps.refreshRatesHz) {
        if (r <= prefMax && r > rate) rate = r;
      }
      if (rate == 0 && !caps.refreshRatesHz.empty()) {
        rate = caps.refreshRatesHz.front();
      }
      params.refreshRateHz = rate != 0 ? rate : 90u;
      if (std::getenv("FUVR_RT_DEBUG"))
        std::fprintf(stderr,
                     "[fuvr-rt] caps from daemon: model='%s' perEye=%ux%u rate=%u (advertised %zu rates)\n",
                     caps.deviceModel.c_str(), params.perEyeWidth,
                     params.perEyeHeight, params.refreshRateHz,
                     caps.refreshRatesHz.size());
    } else {
      // TODO: surface this via the connection UI; today we silently fall
      // back. Hardcoded Quest 3 defaults; the daemon will negotiate down
      // when the Quest finally connects.
      if (metalFxEnabled) {
        params.perEyeWidth = ((2064u + 1u) & ~1u);
        params.perEyeHeight = ((2208u + 1u) & ~1u);
      } else {
        params.perEyeWidth = scaleEvenX(2064);
        params.perEyeHeight = scaleEvenY(2208);
      }
      params.refreshRateHz = 90;
      if (std::getenv("FUVR_RT_DEBUG"))
        std::fprintf(stderr,
                     "[fuvr-rt] no caps from daemon yet; using Quest 3 defaults\n");
    }
    // Encoder bitrate / codec — env-driven so the mac-app's Encoder
    // settings page can propagate the user's choice to Blender's session
    // path (which doesn't otherwise see the mac-app UI). The mac-app
    // does `launchctl setenv FUVR_RT_BITRATE_BPS ...` whenever the user
    // changes the slider; Blender (re)launched from Finder inherits the
    // env. If unset, fall back to the previous hardcoded 50 Mbps.
    if (const char* env = std::getenv("FUVR_RT_BITRATE_BPS")) {
      char* endp = nullptr;
      uint64_t bps = std::strtoull(env, &endp, 10);
      if (endp != env && bps > 0 && bps < (1ull << 32)) {
        params.videoBitrateBps = static_cast<uint32_t>(bps);
      }
    }
    if (const char* env = std::getenv("FUVR_RT_CODEC")) {
      if (std::strcmp(env, "h264") == 0 || std::strcmp(env, "avc") == 0) {
        params.useHevc = false;
      } else if (std::strcmp(env, "hevc") == 0 || std::strcmp(env, "h265") == 0) {
        params.useHevc = true;
      }
    }
    if (std::getenv("FUVR_RT_DEBUG"))
      std::fprintf(stderr, "[fuvr-rt] encoder: %s @ %u bps\n",
                   params.useHevc ? "HEVC" : "H264",
                   params.videoBitrateBps);

    // Capture the encoder-side full per-eye dims as the upscaler's output
    // target. With MetalFX on, params.perEye{Width,Height} are full; with
    // it off, they're already-scaled (and the upscaler is unused).
    session->metalFxFullPerEyeWidth = params.perEyeWidth;
    session->metalFxFullPerEyeHeight = params.perEyeHeight;
    StartSessionResult result{};
    if (daemon->startSession(params, &result)) {
      session->daemonSessionId = result.sessionId;
      daemon->subscribePoses(result.sessionId);
      daemon->subscribeInputs(result.sessionId);
      daemon->subscribeEncodeStats();
    }
    // Pose lookahead: how far into the future Blender renders. With Quest-side
    // ATW correctly handling Δq from xrLocateViews(now)·q_render⁻¹ using
    // IMU-Kalman ω, we want the *minimum* lookahead that keeps ATW's warp
    // small enough to stay inside the rendered FOV during typical motion.
    // Larger lookahead = smaller ATW warp BUT bigger noise amplification
    // (ω·Δt amplifies sub-degree IMU jitter linearly with Δt).
    //
    // ALVR/Air Link target ~30 ms here. We override via FUVR_RT_POSE_LOOKAHEAD_MS
    // for tuning. Earlier 70 ms produced visible tremor on micro head motion.
    {
      uint64_t lookahead = 0;
      if (const char* env = std::getenv("FUVR_RT_POSE_LOOKAHEAD_MS")) {
        lookahead = static_cast<uint64_t>(std::strtoull(env, nullptr, 10)) *
                    1'000'000ull;
      } else {
        // Phase C measurement (Δq peak 69° at aggressive motion, lookahead=0):
        // pipeline Mac→Quest ≈ 50ms. ATW must correct that motion. With
        // lookahead = pipeline_estimate (50ms), Mac predicts forward by ~the
        // pipeline and at display q_render ≈ q_now → ATW Δq small. ALVR uses
        // 30-50ms range. Override via FUVR_RT_POSE_LOOKAHEAD_MS.
        lookahead = 50'000'000ull;
      }
      session->poseLookaheadNs = lookahead;
      if (std::getenv("FUVR_RT_DEBUG"))
        std::fprintf(stderr, "[fuvr-rt] pose lookahead = %llu ms\n",
                     (unsigned long long)(lookahead / 1'000'000ull));
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
      Swapchain* sc{nullptr};
      uint32_t idx{0};
      bool valid{false};
    };
    auto resolveSwapchain = [&](XrSwapchain handle) -> ResolvedImage {
      for (auto& sc : s->swapchains) {
        if (sc->handle != handle) continue;
        if (sc->images.empty()) return {};
        const uint32_t idx = sc->lastReleasedIndex %
                             static_cast<uint32_t>(sc->images.size());
        return {sc->images[idx]->surface, sc->images[idx]->mtlTexture,
                sc->width, sc->height, sc.get(), idx, true};
      }
      return {};
    };

    std::vector<IOSurfaceRef> layers;
    // Per-frame rendered FOV resolved from the actual swapchain image(s)
    // being submitted on the projection layer. Falls back to the session's
    // pending locate-FOV when no projection layer is present (legacy
    // single-image path / non-stereo apps).
    bool resolvedFovFromImage = false;
    Fov submittedLeftFov{};
    Fov submittedRightFov{};
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
            // Pull the per-image FOV that xrReleaseSwapchainImage stamped
            // onto these specific images. Each released image carries the
            // FOV xrLocateViews returned for its eye at render time, so
            // even if frames are rendered out-of-order or queued, the FOV
            // we ship on the wire matches the pixels in the surface.
            if (L.valid && L.idx < L.sc->imageLeftFov.size()) {
              submittedLeftFov = L.sc->imageLeftFov[L.idx];
              resolvedFovFromImage = true;
            }
            if (R.valid && R.idx < R.sc->imageRightFov.size()) {
              submittedRightFov = R.sc->imageRightFov[R.idx];
              resolvedFovFromImage = true;
            }
            if (L.mtlTexture != nullptr && R.mtlTexture != nullptr) {
              // METALFX: lazily build the upscaler the first time we see a
              // pair of eye textures with known dims. Input dim is the
              // swapchain's per-eye dim (half, post-FUVR_RT_RENDER_SCALE);
              // output dim is the session's recorded full per-eye dim
              // (matches encoder + SBS combined target).
              void* leftTex = L.mtlTexture;
              void* rightTex = R.mtlTexture;
              uint32_t blitW = L.width;
              uint32_t blitH = L.height;
              if (s->metalFxEnabled) {
                if (!s->metalFxUpscaler) {
                  s->metalFxUpscaler = std::make_unique<MetalFxUpscaler>();
                  if (!s->metalFxUpscaler->init(
                          s->metalDevice, s->metalCommandQueue,
                          L.width, L.height,
                          s->metalFxFullPerEyeWidth,
                          s->metalFxFullPerEyeHeight)) {
                    std::fprintf(stderr,
                                 "[METALFX] init failed; disabling upscaler "
                                 "for this session (will SBS at half dim)\n");
                    s->metalFxUpscaler.reset();
                    s->metalFxEnabled = false;
                  }
                }
                if (s->metalFxUpscaler) {
                  void* uL = s->metalFxUpscaler->upscaleEye(0, L.mtlTexture);
                  void* uR = s->metalFxUpscaler->upscaleEye(1, R.mtlTexture);
                  if (uL != nullptr && uR != nullptr) {
                    leftTex = uL;
                    rightTex = uR;
                    blitW = s->metalFxUpscaler->outputWidth();
                    blitH = s->metalFxUpscaler->outputHeight();
                  }
                  // 1Hz log so the user can verify it's running.
                  static std::atomic<uint64_t> lastLogNs{0};
                  uint64_t nowNs = static_cast<uint64_t>(
                      std::chrono::duration_cast<std::chrono::nanoseconds>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count());
                  uint64_t prev = lastLogNs.load(std::memory_order_relaxed);
                  if (nowNs - prev >= 1'000'000'000ull &&
                      lastLogNs.compare_exchange_strong(prev, nowNs)) {
                    std::fprintf(stderr,
                                 "[METALFX] upscaling %ux%u -> %ux%u/eye, "
                                 "frame_us=%lld\n",
                                 L.width, L.height,
                                 s->metalFxUpscaler->outputWidth(),
                                 s->metalFxUpscaler->outputHeight(),
                                 (long long)s->metalFxUpscaler->lastFrameUs());
                  }
                }
              }
              if (!s->stereoBlitter) {
                s->stereoBlitter = std::make_unique<StereoBlitter>();
                if (!s->stereoBlitter->init(s->metalDevice,
                                             s->metalCommandQueue,
                                             blitW, blitH)) {
                  s->stereoBlitter.reset();
                }
              }
              if (s->stereoBlitter) {
                surf = s->stereoBlitter->blitToCombined(leftTex, rightTex);
                if (surf != nullptr) {
                  f.width = blitW * 2;
                  f.height = blitH;
                  f.leftMetalTexture = leftTex;
                  f.rightMetalTexture = rightTex;
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

    // Why: stamp the *exact same* per-eye pose Blender just rendered
    // with into VideoFragmentHeader so the Quest's OS scan-out timewarp
    // computes Δq = q_now * q_render⁻¹ against the right baseline.
    // Calling predictor.latest() again here would pick up the ~10–15
    // upstream samples that arrived during the Blender render (Quest
    // pushes pose at 1 kHz, Blender renders at ~90 Hz), stamping a
    // newer pose than what Blender actually used. The compositor would
    // then "rewind" part of the user's motion via Δq, dragging the
    // streamed image behind the head. The pose was cached in
    // pendingLocateLeft/RightPose at xrLocateViews_impl time.
    if (s->pendingLocatePoseValid) {
      f.renderedLeft = s->pendingLocateLeftPose;
      f.renderedRight = s->pendingLocateRightPose;
    }
    // Carry the same (overscan-applied) FOV that xrLocateViews returned, so
    // the Quest ATW shader gets fov_render right and doesn't fall back to
    // fov_now (which is narrower than what Blender actually rendered).
    // Prefer the per-image FOV bound to the actual swapchain image being
    // submitted (correct even when frames don't run strict locate→end);
    // fall back to the session-level pending FOV for the non-projection
    // legacy path.
    if (resolvedFovFromImage) {
      f.renderedLeftFov  = submittedLeftFov;
      f.renderedRightFov = submittedRightFov;
    } else {
      f.renderedLeftFov  = s->pendingLocateLeftFov;
      f.renderedRightFov = s->pendingLocateRightFov;
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
  // Smooth the most recent forward-stamped samples instead of using a
  // single sample verbatim. At the Quest's 1 kHz upstream rate every
  // Blender frame would otherwise pick a single millisecond-old IMU
  // sample as q_render — the per-sample orientation noise (~10⁻⁴ rad)
  // shows up as a visible micro-wobble overlaid on real motion, and
  // the OS scan-out timewarp can't fix it because q_render is
  // *defined* by what we ship. An 8-sample window (~8 ms) tracks real
  // head motion (it lags steady angular velocity by ~4 ms) but kills
  // the per-frame jitter, making streamed motion read as fluid even
  // when Blender's render rate is lower than the Quest display rate.
  // Cross-clock dt is still avoided by not re-extrapolating; the pose
  // is cached in pendingLocate*Pose so xrEndFrame_impl stamps the
  // identical pose Blender just rendered with.
  auto predicted = s->predictor.smoothedLatest(8);
  // [LATENCY-DEBUG] At 1Hz, log how far ahead xrLocateViews is asking the
  // predictor to extrapolate (dt_ms), whether the predictor's 60ms cap
  // (pose_predictor.cpp:223) is firing, and how stale the latest sample is
  // at the call site. Gated on FUVR_RT_DEBUG (cached once).
  {
    static const bool kLatencyDebug = std::getenv("FUVR_RT_DEBUG") != nullptr;
    if (kLatencyDebug) {
      static std::atomic<uint64_t> lastLogNs{0};
      uint64_t nowL = static_cast<uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              std::chrono::steady_clock::now().time_since_epoch()).count());
      uint64_t prev = lastLogNs.load(std::memory_order_relaxed);
      if (nowL - prev >= 1'000'000'000ull &&
          lastLogNs.compare_exchange_strong(prev, nowL)) {
        auto latest = s->predictor.latest();
        long long dtMs = 0;
        long long ageMs = 0;
        int capFired = 0;
        if (latest.has_value()) {
          dtMs = static_cast<long long>(
              (static_cast<int64_t>(info->displayTime) -
               static_cast<int64_t>(latest->timestampNs)) / 1'000'000);
          ageMs = static_cast<long long>(
              (static_cast<int64_t>(nowL) -
               static_cast<int64_t>(latest->timestampNs)) / 1'000'000);
          capFired = (dtMs > 60) ? 1 : 0;
        }
        std::fprintf(stderr,
                     "[fuvr-rt] [LATENCY-DEBUG] predict: dt_ms=%lld "
                     "cap_fired=%d sample_age_ms=%lld\n",
                     dtMs, capFired, ageMs);
      }
    }
  }
  // Why: the Quest reports its actual per-eye fov in every UpstreamFrame and
  // the daemon now forwards it through PoseSnapshot. Use the headset's real
  // fov so Blender renders with the same projection the headset is built
  // around — eyes fuse properly and ATW's fov_render→fov_now fall-back stays
  // an identity warp on the projection axis. Until a real sample arrives,
  // fall back to a Quest 3 approximation (outer ~64°, inner ~46°, vert ~48°).
  constexpr float kOuter = 1.117f, kInner = 0.803f, kVert = 0.838f;
  auto pickFov = [&](int eye) -> XrFovf {
    XrFovf out{};
    if (predicted.has_value()) {
      const auto& f = (eye == 0) ? predicted->leftFov : predicted->rightFov;
      if (f.angleLeft != 0.0f || f.angleRight != 0.0f) {
        out.angleLeft  = f.angleLeft;
        out.angleRight = f.angleRight;
        out.angleUp    = f.angleUp;
        out.angleDown  = f.angleDown;
        return out;
      }
    }
    if (eye == 0) { out.angleLeft = -kOuter; out.angleRight = +kInner; }
    else          { out.angleLeft = -kInner; out.angleRight = +kOuter; }
    out.angleUp   = +kVert;
    out.angleDown = -kVert;
    return out;
  };
  // Why: the Quest now drives a stereo XrCompositionLayerProjection straight
  // off this rendered swapchain, with views[i].pose = the render-time eye pose
  // and views[i].fov = this rendered fov. The OS compositor performs scan-out
  // timewarp using the +margin region; whatever margin we ship here is the
  // headroom the OS has to reproject during head motion between render-time
  // and display-time. Add a fixed margin on each of the four sides
  // (additive, not multiplicative — multiplicative blew tan() near the
  // asymmetric outer edge). Sign convention matches XrFovf: left/down
  // are negative, right/up positive, so margin pushes left/down more
  // negative and right/up more positive. Tunable via
  // FUVR_RT_FOV_MARGIN_DEG env var.
  //
  // Default 6°: just enough overscan for the Quest's OS scan-out
  // timewarp to keep motion fluid (it can rotate the rendered image at
  // 90 Hz against the current head pose and pull from the margin to
  // fill the rotated viewport). Earlier 12–18° defaults caused visible
  // bottom-edge warping that turned out to be cross-clock predictor
  // noise — that's now fixed (Mac uses predictor.latest() and pose is
  // cached at xrLocateViews) so a small honest margin is enough.
  // Set to 0 to disable reprojection headroom entirely (rotational
  // motion will visibly judder).
  // Default 0°: render the Quest's native FOV with no reprojection
  // headroom. Earlier 6°/12°/18° defaults caused visible bottom-edge
  // warping that was worse than the lack of reprojection padding it
  // bought us. Re-enable per-process via FUVR_RT_FOV_MARGIN_DEG env if
  // motion judder during high angular velocity becomes a problem.
  static const float kMarginRad = []() {
    float deg = 0.0f;
    if (const char* env = std::getenv("FUVR_RT_FOV_MARGIN_DEG")) {
      float v = static_cast<float>(std::strtof(env, nullptr));
      if (v >= 0.0f && v <= 30.0f) deg = v;
    }
    return deg * 0.017453292519943295f;
  }();
  for (uint32_t i = 0; i < 2; ++i) {
    views[i].type = XR_TYPE_VIEW;
    views[i].next = nullptr;
    XrFovf fov = pickFov(static_cast<int>(i));
    fov.angleLeft  -= kMarginRad;
    fov.angleRight += kMarginRad;
    fov.angleUp    += kMarginRad;
    fov.angleDown  -= kMarginRad;
    views[i].fov = fov;
    // Cache as session-level *pending* FOV. xrReleaseSwapchainImage will
    // copy this onto the per-image FOV slot of the released image, binding
    // the rendered FOV to the actual swapchain image being submitted (so
    // xrEndFrame stamps the correct FOV per frame even with multi-frame
    // queueing or background renders).
    Fov& cache = (i == 0) ? s->pendingLocateLeftFov : s->pendingLocateRightFov;
    cache.angleLeft  = fov.angleLeft;
    cache.angleRight = fov.angleRight;
    cache.angleUp    = fov.angleUp;
    cache.angleDown  = fov.angleDown;
    if (predicted.has_value()) {
      const Pose& p = (i == 0) ? predicted->leftEye : predicted->rightEye;
      views[i].pose.position = {p.position.x, p.position.y, p.position.z};
      views[i].pose.orientation = {p.orientation.x, p.orientation.y,
                                    p.orientation.z, p.orientation.w};
      Pose& pcache = (i == 0) ? s->pendingLocateLeftPose
                              : s->pendingLocateRightPose;
      pcache = p;
    } else {
      views[i].pose.position = {0.0f, 0.0f, 0.0f};
      views[i].pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    }
  }
  s->pendingLocatePoseValid = predicted.has_value();
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
