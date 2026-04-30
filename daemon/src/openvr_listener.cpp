// SPDX-License-Identifier: Apache-2.0
#include "fuvr/openvr_listener.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>

#include "fuvr/daemon.hpp"
#include "fuvr/logger.hpp"

namespace fuvr::daemon {

namespace {

// ---- Wire protocol mirrors openvr-shim/src/wire.hpp ----
//
// We deliberately re-declare these structs here (rather than `#include`-ing
// the shim's header) so the daemon target keeps zero source-level coupling
// to the shim. The shim is licensed/built differently (universal binary,
// vendored Valve header) and lives in its own subdirectory; sharing the
// header would force the daemon to honour those constraints too.

constexpr uint32_t kMagic       = 0x46565244;  // 'FVRD'
// Bumped to 2 alongside openvr-shim/src/wire.hpp: SubmitFramePayload now
// carries per-eye render poses + FOV so we can populate
// XrCompositionLayerProjectionView correctly without doubling IPD.
constexpr uint16_t kProtocolVer = 2;

#pragma pack(push, 1)
struct WireHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t type;
  uint32_t payloadLen;
  uint32_t requestId;
};
struct HelloPayload {
  uint32_t pid;
  uint32_t appType;
  char     appKey[64];
};
struct HelloOkPayload {
  uint32_t perEyeWidth;
  uint32_t perEyeHeight;
  uint32_t refreshRateHz;
  float    leftFov[4];
  float    rightFov[4];
  float    eyeFromHeadLeft[12];
  float    eyeFromHeadRight[12];
  uint32_t controllerMask;
};
struct PoseQueryPayload {
  float    predictedSecondsFromNow;
  uint32_t universeOrigin;
};
struct PosePayload {
  uint32_t validMask;
  uint32_t reserved;
  float    hmd[13];
  float    leftCtrl[13];
  float    rightCtrl[13];
};
struct WaitFrameOkPayload {
  uint64_t targetDisplayTimeNs;
  uint64_t cpuFrameStartNs;
  uint64_t frameIndex;
};
struct SubmitFramePayload {
  uint64_t surfaceToken;
  uint32_t eye;
  uint32_t flags;
  float    boundsUMin, boundsUMax, boundsVMin, boundsVMax;
  float    renderPoseHmd[12];
  // v2 additions: per-eye render pose (pos.xyz + quat.xyzw, world ← eye)
  // and per-eye FOV tangents { left, right, up, down }.
  float    renderPoseLeft[7];
  float    renderPoseRight[7];
  float    leftFov[4];
  float    rightFov[4];
};
struct ActionUpdatePayload { uint32_t handleCount; };
struct ActionStateEntry {
  uint64_t handle;
  uint8_t  kind;
  uint8_t  active;
  uint8_t  changed;
  uint8_t  _pad;
  float    body[8];
  uint64_t timestampNs;
};
struct ActionStatePayload { uint32_t entryCount; };
struct HapticPayload {
  uint64_t deviceHandle;
  float    startSecondsFromNow;
  float    durationSeconds;
  float    frequency;
  float    amplitude;
};
#pragma pack(pop)

// Why prefixed names: the daemon already has a `PoseSnapshot` capnp struct
// in `fuvr::daemon::`; we'd otherwise shadow it.
enum WireMsg : uint16_t {
  WM_Hello        = 0x0001,
  WM_HelloOk      = 0x0002,
  WM_Goodbye      = 0x0003,
  WM_PoseQuery    = 0x0010,
  WM_PoseSnapshot = 0x0011,
  WM_WaitFrame    = 0x0012,
  WM_WaitFrameOk  = 0x0013,
  WM_SubmitFrame  = 0x0020,
  WM_ActionUpdate = 0x0030,
  WM_ActionState  = 0x0031,
  WM_Haptic       = 0x0032,
  WM_Ping         = 0x00f0,
  WM_Pong         = 0x00f1,
};

bool readAll(int fd, void* buf, size_t n) {
  auto* p = static_cast<uint8_t*>(buf);
  size_t got = 0;
  while (got < n) {
    ssize_t r = ::recv(fd, p + got, n - got, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (r == 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

bool writeAll(int fd, const void* buf, size_t n) {
  const auto* p = static_cast<const uint8_t*>(buf);
  size_t sent = 0;
  while (sent < n) {
    ssize_t r = ::send(fd, p + sent, n - sent, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (r == 0) return false;
    sent += static_cast<size_t>(r);
  }
  return true;
}

bool sendReply(int fd, uint16_t type, uint32_t reqId,
               const void* payload, uint32_t len) {
  WireHeader h{kMagic, kProtocolVer, type, len, reqId};
  if (!writeAll(fd, &h, sizeof(h))) return false;
  if (len && !writeAll(fd, payload, len)) return false;
  return true;
}

uint64_t nowNs() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

OpenVrListener::OpenVrListener() = default;

OpenVrListener::~OpenVrListener() { stop(); }

bool OpenVrListener::start(const std::string& socketPath, Daemon* daemon) {
  daemon_ = daemon;
  path_ = socketPath;

  // Pre-clean: the previous run may have left an orphan path.
  ::unlink(path_.c_str());

  listenFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listenFd_ < 0) {
    FUVR_LOG_ERROR("openvr-listener", "socket() failed: %s", std::strerror(errno));
    return false;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
  if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    FUVR_LOG_ERROR("openvr-listener", "bind(%s) failed: %s",
                   path_.c_str(), std::strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  // World-readable so any user-launched game can dlopen-and-connect.
  // (The socket lives in /tmp; the inode itself is the only secret.)
  ::chmod(path_.c_str(), 0666);

  if (::listen(listenFd_, /*backlog*/ 4) < 0) {
    FUVR_LOG_ERROR("openvr-listener", "listen() failed: %s", std::strerror(errno));
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }

  running_.store(true);
  acceptThread_ = std::thread([this] { acceptLoop(); });
  FUVR_LOG_INFO("openvr-listener", "listening on %s", path_.c_str());
  return true;
}

void OpenVrListener::stop() {
  if (!running_.exchange(false)) return;
  if (listenFd_ >= 0) {
    ::shutdown(listenFd_, SHUT_RDWR);
    ::close(listenFd_);
    listenFd_ = -1;
  }
  if (acceptThread_.joinable()) acceptThread_.join();
  for (auto& t : clientThreads_) {
    if (t.joinable()) t.join();
  }
  clientThreads_.clear();
  if (!path_.empty()) ::unlink(path_.c_str());
}

void OpenVrListener::acceptLoop() {
  while (running_.load()) {
    sockaddr_un peer{};
    socklen_t plen = sizeof(peer);
    int cfd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&peer), &plen);
    if (cfd < 0) {
      if (!running_.load()) break;
      if (errno == EINTR) continue;
      FUVR_LOG_WARN("openvr-listener", "accept() failed: %s", std::strerror(errno));
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    FUVR_LOG_INFO("openvr-listener", "shim connected fd=%d", cfd);
    clientThreads_.emplace_back([this, cfd]() { clientLoop(cfd); });
  }
}

void OpenVrListener::clientLoop(int fd) {
  bool helloDone = false;
  uint64_t sessionId = 0;
  // Per-client running counter so the encoder sees monotonic frameIds even if
  // the shim restarts mid-session. Starts at the connection's accept time.
  uint64_t clientFrameSeq = 0;
  uint32_t clientPerEyeW = 0;
  uint32_t clientPerEyeH = 0;
  uint32_t clientRateHz = 0;
  // Frame-pacing accumulator: we pretend a 90 Hz vsync edge so games
  // running through Vivecraft → Submit → WaitGetPoses get a steady tick
  // even when the daemon's transport-side pacing isn't fully wired yet.
  // The real value will come from the encoder's frame-clock in a later
  // iteration.
  uint64_t frameIndex = 0;
  const uint64_t frameIntervalNs = 1'000'000'000ull / 90ull;
  uint64_t nextFrameAtNs = nowNs() + frameIntervalNs;

  while (running_.load()) {
    WireHeader h{};
    if (!readAll(fd, &h, sizeof(h))) break;
    if (h.magic != kMagic || h.version != kProtocolVer) {
      FUVR_LOG_WARN("openvr-listener", "bad magic/version on fd=%d", fd);
      break;
    }

    std::vector<uint8_t> payload(h.payloadLen);
    if (h.payloadLen && !readAll(fd, payload.data(), h.payloadLen)) break;

    switch (h.type) {
      case WM_Hello: {
        if (helloDone) { /* one Hello per connection — ignore reissue */ }
        if (payload.size() != sizeof(HelloPayload)) {
          FUVR_LOG_WARN("openvr-listener", "bad Hello size %zu", payload.size());
          goto done;
        }
        const auto* hello = reinterpret_cast<const HelloPayload*>(payload.data());
        std::string appKey(hello->appKey,
                           ::strnlen(hello->appKey, sizeof(hello->appKey)));
        HelloOkPayload ok{};
        // Sensible Quest-shaped defaults; will be replaced once the
        // listener is wired into the live session capabilities.
        ok.perEyeWidth = 2064;
        ok.perEyeHeight = 2208;
        ok.refreshRateHz = 90;
        // Tangents at 1m for a Quest 3-ish FoV (l, r, u, d) — note OpenVR
        // sign convention: left & down are negative.
        ok.leftFov[0]  = -0.94f; ok.leftFov[1]  =  0.78f;
        ok.leftFov[2]  =  0.92f; ok.leftFov[3]  = -0.92f;
        ok.rightFov[0] = -0.78f; ok.rightFov[1] =  0.94f;
        ok.rightFov[2] =  0.92f; ok.rightFov[3] = -0.92f;
        // Eye-from-head: ±32 mm IPD baseline.
        for (int i = 0; i < 12; ++i) ok.eyeFromHeadLeft[i] = ok.eyeFromHeadRight[i] = 0.0f;
        ok.eyeFromHeadLeft[0]  = ok.eyeFromHeadLeft[5]  = ok.eyeFromHeadLeft[10]  = 1.0f;
        ok.eyeFromHeadRight[0] = ok.eyeFromHeadRight[5] = ok.eyeFromHeadRight[10] = 1.0f;
        ok.eyeFromHeadLeft[3]  = -0.032f;
        ok.eyeFromHeadRight[3] =  0.032f;
        ok.controllerMask = 0x3;  // both hands assumed present
        if (!sendReply(fd, WM_HelloOk, h.requestId, &ok, sizeof(ok))) goto done;
        helloDone = true;
        clientPerEyeW = ok.perEyeWidth;
        clientPerEyeH = ok.perEyeHeight;
        clientRateHz  = ok.refreshRateHz;
        if (daemon_) {
          sessionId = daemon_->openOpenVrSession(clientPerEyeW, clientPerEyeH,
                                                 clientRateHz, appKey);
          if (sessionId == 0) {
            FUVR_LOG_WARN("openvr-listener",
                          "failed to open session for appKey=%s; frames will be dropped",
                          appKey.c_str());
          }
        }
        break;
      }
      case WM_PoseQuery: {
        // Live pose: pull the freshest snapshot the PoseRouter has seen
        // from the Quest's 1 kHz upstream and ship it back to the shim.
        // Falls back to a sensible identity if no pose has arrived yet
        // (game launches before headset starts streaming).
        PosePayload p{};
        bool have = false;
        if (daemon_) {
          PoseRouter::LatestPoseSet ls{};
          if (daemon_->poseRouter().latestPoseSet(ls)) {
            p.validMask = ls.validMask;
            std::memcpy(p.hmd,       ls.hmd,       sizeof(p.hmd));
            std::memcpy(p.leftCtrl,  ls.leftCtrl,  sizeof(p.leftCtrl));
            std::memcpy(p.rightCtrl, ls.rightCtrl, sizeof(p.rightCtrl));
            have = true;
          }
        }
        if (!have) {
          // Pre-stream: identity HMD, hands hanging in front so the
          // shim's controller indices stay populated for IsConnected.
          p.validMask = 0x7;
          p.hmd[6] = 1.0f;
          p.leftCtrl[6]  = 1.0f; p.leftCtrl[0]  = -0.20f; p.leftCtrl[1]  = -0.10f; p.leftCtrl[2]  = -0.40f;
          p.rightCtrl[6] = 1.0f; p.rightCtrl[0] =  0.20f; p.rightCtrl[1] = -0.10f; p.rightCtrl[2] = -0.40f;
        }
        if (!sendReply(fd, WM_PoseSnapshot, h.requestId, &p, sizeof(p))) goto done;
        // 1 Hz throttled debug so we can see pose forwarding without
        // drowning the log at WaitGetPoses cadence.
        {
          static thread_local uint64_t s_lastLogNs = 0;
          uint64_t nowL = nowNs();
          if (have && nowL - s_lastLogNs >= 1'000'000'000ull) {
            s_lastLogNs = nowL;
            FUVR_LOG_INFO("openvr-listener",
                          "[POSE-FWD] WM_PoseQuery -> hmd pos=(%.3f,%.3f,%.3f) "
                          "quat=(%.3f,%.3f,%.3f,%.3f) validMask=0x%x",
                          p.hmd[0], p.hmd[1], p.hmd[2],
                          p.hmd[3], p.hmd[4], p.hmd[5], p.hmd[6],
                          (unsigned)p.validMask);
          }
        }
        break;
      }
      case WM_WaitFrame: {
        uint64_t now = nowNs();
        if (now < nextFrameAtNs) {
          std::this_thread::sleep_for(std::chrono::nanoseconds(nextFrameAtNs - now));
        }
        WaitFrameOkPayload wf{};
        wf.cpuFrameStartNs = nowNs();
        wf.targetDisplayTimeNs = wf.cpuFrameStartNs + frameIntervalNs;
        wf.frameIndex = ++frameIndex;
        nextFrameAtNs += frameIntervalNs;
        if (!sendReply(fd, WM_WaitFrameOk, h.requestId, &wf, sizeof(wf))) goto done;
        break;
      }
      case WM_SubmitFrame: {
        if (payload.size() != sizeof(SubmitFramePayload)) goto done;
        const auto* sf = reinterpret_cast<const SubmitFramePayload*>(payload.data());
        // SubmitFrame is fire-and-forget by design — never reply.
        if (!daemon_ || sessionId == 0) {
          static thread_local uint64_t s_dropped = 0;
          if ((s_dropped % 90) == 0) {
            FUVR_LOG_WARN("openvr-listener",
                          "submitframe dropped (no session) eye=%u token=0x%llx",
                          sf->eye, (unsigned long long)sf->surfaceToken);
          }
          ++s_dropped;
          break;
        }
        CVPixelBufferRef pb = daemon_->resolveSurfaceToken(sf->surfaceToken);
        if (!pb) {
          static thread_local uint64_t s_miss = 0;
          if ((s_miss % 60) == 0) {
            FUVR_LOG_WARN("openvr-listener",
                          "submitframe iosurface lookup miss eye=%u token=0x%llx",
                          sf->eye, (unsigned long long)sf->surfaceToken);
          }
          ++s_miss;
          break;
        }
        // The shim now ships per-eye render poses (pos+quat) and per-eye
        // FOV tangents directly — composed shim-side as world ← head ← eye.
        // We forward them straight into the OpenXR projection layer so the
        // Quest's scan-out reprojection sees the actual render camera and
        // doesn't add a second ±IPD/2 delta on top of Vivecraft's already-
        // baked stereo offset.
        const float* leftPose  = sf->renderPoseLeft;
        const float* rightPose = sf->renderPoseRight;
        // FOV passes through if any tangent is non-zero — all-zeros is the
        // "uninitialised" sentinel that asks the Quest to fall back to the
        // current xrLocateViews fov.
        auto fovValid = [](const float f[4]) {
          return f[0] != 0.0f || f[1] != 0.0f || f[2] != 0.0f || f[3] != 0.0f;
        };
        const float* leftFov  = fovValid(sf->leftFov)  ? sf->leftFov  : nullptr;
        const float* rightFov = fovValid(sf->rightFov) ? sf->rightFov : nullptr;

        uint64_t frameId = ++clientFrameSeq;
        bool ok = daemon_->submitOpenVrFrame(sessionId, pb, frameId,
                                             nowNs(),
                                             leftPose, rightPose,
                                             leftFov, rightFov);
        CFRelease(pb);
        if (ok) daemon_->noteOpenVrFrameSubmitted();
        {
          // Shim composites L+R into a single SBS frame and tags it
          // eye=2 ("combined"). The daemon encoder always sees the SBS
          // surface — eye is informational only.
          const char* eyeLabel = (sf->eye == 2) ? "combined"
                              : (sf->eye == 0) ? "left"
                              : (sf->eye == 1) ? "right" : "?";
          static thread_local uint64_t s_count = 0;
          if ((s_count % 60) == 0 || !ok) {
            FUVR_LOG_INFO("openvr-listener",
                          "wm_submitframe routed: eye=%s token=0x%llx frameId=%llu -> encoder %s",
                          eyeLabel, (unsigned long long)sf->surfaceToken,
                          (unsigned long long)frameId,
                          ok ? "ok" : "ERR");
          }
          ++s_count;
        }
        break;
      }
      case WM_ActionUpdate: {
        if (payload.size() < sizeof(ActionUpdatePayload)) goto done;
        // Echo an empty action-state list — Vivecraft tolerates this and
        // falls back to legacy controller-state queries while we plumb
        // the proper action mapping into PoseRouter.
        ActionStatePayload reply{};
        reply.entryCount = 0;
        if (!sendReply(fd, WM_ActionState, h.requestId, &reply, sizeof(reply))) goto done;
        break;
      }
      case WM_Haptic: {
        // Fire-and-forget. Will be routed into the daemon's haptics path
        // (StreamInputs response) in a follow-up.
        break;
      }
      case WM_Ping: {
        if (!sendReply(fd, WM_Pong, h.requestId, nullptr, 0)) goto done;
        break;
      }
      case WM_Goodbye:
        goto done;
      default:
        FUVR_LOG_WARN("openvr-listener", "unknown message type 0x%04x", h.type);
        break;
    }
  }
done:
  if (daemon_ && sessionId != 0) {
    daemon_->closeOpenVrSession(sessionId);
  }
  ::close(fd);
  FUVR_LOG_INFO("openvr-listener", "shim disconnected fd=%d", fd);
}

}  // namespace fuvr::daemon
