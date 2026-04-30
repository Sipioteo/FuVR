// SPDX-License-Identifier: Apache-2.0
#include "daemon_rpc.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "log.hpp"

namespace fuvr::openvr_shim {

namespace {

const char* socketPath() {
  if (const char* env = std::getenv("FUVR_OPENVR_SOCKET")) return env;
  return wire::kSocketPath;
}

bool writeAllBlocking(int fd, const void* buf, size_t n) {
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

bool readAllBlocking(int fd, void* buf, size_t n) {
  auto* p = static_cast<uint8_t*>(buf);
  size_t got = 0;
  while (got < n) {
    ssize_t r = ::recv(fd, p + got, n - got, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (r == 0) return false; // peer closed
    got += static_cast<size_t>(r);
  }
  return true;
}

}  // namespace

DaemonRpc::DaemonRpc() = default;

DaemonRpc::~DaemonRpc() { disconnect(); }

bool DaemonRpc::sendAll(const void* buf, size_t n) {
  return writeAllBlocking(fd_, buf, n);
}

bool DaemonRpc::recvAll(void* buf, size_t n) {
  return readAllBlocking(fd_, buf, n);
}

bool DaemonRpc::sendMessage(wire::MessageType type,
                            const void* payload,
                            uint32_t payloadLen,
                            uint32_t* outRequestId) {
  wire::WireHeader h{};
  h.magic = wire::kMagic;
  h.version = wire::kProtocolVer;
  h.type = static_cast<uint16_t>(type);
  h.payloadLen = payloadLen;
  h.requestId = nextRequestId_++;
  if (outRequestId) *outRequestId = h.requestId;
  if (!sendAll(&h, sizeof(h))) return false;
  if (payloadLen > 0 && !sendAll(payload, payloadLen)) return false;
  return true;
}

bool DaemonRpc::recvMessage(wire::MessageType expectedType,
                            uint32_t expectedRequestId,
                            std::vector<uint8_t>& payload) {
  wire::WireHeader h{};
  if (!recvAll(&h, sizeof(h))) return false;
  if (h.magic != wire::kMagic) {
    FUVR_LOG("rpc: bad magic 0x%08x — connection desynced", h.magic);
    return false;
  }
  if (h.type != static_cast<uint16_t>(expectedType)) {
    FUVR_LOG("rpc: unexpected message type 0x%04x (wanted 0x%04x)",
             h.type, static_cast<uint16_t>(expectedType));
    // Drain payload so subsequent reads stay aligned.
    payload.resize(h.payloadLen);
    if (h.payloadLen) recvAll(payload.data(), h.payloadLen);
    return false;
  }
  if (expectedRequestId != 0 && h.requestId != expectedRequestId) {
    FUVR_LOG("rpc: request id mismatch (got %u, wanted %u)",
             h.requestId, expectedRequestId);
  }
  payload.resize(h.payloadLen);
  if (h.payloadLen > 0 && !recvAll(payload.data(), h.payloadLen)) return false;
  return true;
}

bool DaemonRpc::connect(const std::string& appKey,
                        uint32_t appType,
                        DeviceCaps& caps) {
  std::lock_guard<std::mutex> lk(io_);
  disconnect();

  fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) {
    FUVR_LOG("rpc: socket() failed: %s", std::strerror(errno));
    return false;
  }
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  const char* path = socketPath();
  // sun_path is 104 bytes on macOS — truncate quietly if longer.
  std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
  if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    FUVR_LOG("rpc: connect(%s) failed: %s — is fuvrd running?",
             path, std::strerror(errno));
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  wire::HelloPayload hello{};
  hello.pid = static_cast<uint32_t>(::getpid());
  hello.appType = appType;
  std::strncpy(hello.appKey, appKey.c_str(), sizeof(hello.appKey) - 1);
  uint32_t reqId = 0;
  if (!sendMessage(wire::MessageType::Hello, &hello, sizeof(hello), &reqId)) {
    FUVR_LOG("rpc: send Hello failed");
    disconnect();
    return false;
  }

  std::vector<uint8_t> reply;
  if (!recvMessage(wire::MessageType::HelloOk, reqId, reply)) {
    FUVR_LOG("rpc: HelloOk recv failed");
    disconnect();
    return false;
  }
  if (reply.size() != sizeof(wire::HelloOkPayload)) {
    FUVR_LOG("rpc: HelloOk size mismatch: got %zu wanted %zu",
             reply.size(), sizeof(wire::HelloOkPayload));
    disconnect();
    return false;
  }
  const auto* ok = reinterpret_cast<const wire::HelloOkPayload*>(reply.data());
  caps.perEyeWidth = ok->perEyeWidth;
  caps.perEyeHeight = ok->perEyeHeight;
  caps.refreshRateHz = ok->refreshRateHz;
  std::memcpy(caps.leftFov, ok->leftFov, sizeof(caps.leftFov));
  std::memcpy(caps.rightFov, ok->rightFov, sizeof(caps.rightFov));
  std::memcpy(caps.eyeFromHeadLeft, ok->eyeFromHeadLeft,
              sizeof(caps.eyeFromHeadLeft));
  std::memcpy(caps.eyeFromHeadRight, ok->eyeFromHeadRight,
              sizeof(caps.eyeFromHeadRight));
  caps.controllerMask = ok->controllerMask;
  FUVR_LOG("rpc: connected — %ux%u @ %u Hz, controllers=0x%x",
           caps.perEyeWidth, caps.perEyeHeight, caps.refreshRateHz,
           caps.controllerMask);
  return true;
}

void DaemonRpc::disconnect() {
  if (fd_ >= 0) {
    ::shutdown(fd_, SHUT_RDWR);
    ::close(fd_);
    fd_ = -1;
  }
}

bool DaemonRpc::queryPoses(float predictedSecondsFromNow,
                           uint32_t universeOrigin,
                           PoseSet& out) {
  std::lock_guard<std::mutex> lk(io_);
  if (fd_ < 0) return false;
  wire::PoseQueryPayload q{};
  q.predictedSecondsFromNow = predictedSecondsFromNow;
  q.universeOrigin = universeOrigin;
  uint32_t reqId = 0;
  if (!sendMessage(wire::MessageType::PoseQuery, &q, sizeof(q), &reqId)) return false;

  std::vector<uint8_t> reply;
  if (!recvMessage(wire::MessageType::PoseSnapshot, reqId, reply)) return false;
  if (reply.size() != sizeof(wire::PosePayload)) return false;
  const auto* p = reinterpret_cast<const wire::PosePayload*>(reply.data());
  out.validMask = p->validMask;
  std::memcpy(out.hmd, p->hmd, sizeof(out.hmd));
  std::memcpy(out.leftCtrl, p->leftCtrl, sizeof(out.leftCtrl));
  std::memcpy(out.rightCtrl, p->rightCtrl, sizeof(out.rightCtrl));
  return true;
}

bool DaemonRpc::waitFrame(WaitFrameInfo& info) {
  std::lock_guard<std::mutex> lk(io_);
  if (fd_ < 0) return false;
  uint32_t reqId = 0;
  if (!sendMessage(wire::MessageType::WaitFrame, nullptr, 0, &reqId)) return false;
  std::vector<uint8_t> reply;
  if (!recvMessage(wire::MessageType::WaitFrameOk, reqId, reply)) return false;
  if (reply.size() != sizeof(wire::WaitFrameOkPayload)) return false;
  const auto* p = reinterpret_cast<const wire::WaitFrameOkPayload*>(reply.data());
  info.targetDisplayTimeNs = p->targetDisplayTimeNs;
  info.cpuFrameStartNs = p->cpuFrameStartNs;
  info.frameIndex = p->frameIndex;
  return true;
}

bool DaemonRpc::submitFrame(uint32_t eye,
                            uint64_t surfaceToken,
                            uint32_t flags,
                            const float boundsUVMinMax[4],
                            const float renderPose[12],
                            const float renderPoseLeft[7],
                            const float renderPoseRight[7],
                            const float leftFov[4],
                            const float rightFov[4]) {
  std::lock_guard<std::mutex> lk(io_);
  if (fd_ < 0) return false;
  wire::SubmitFramePayload p{};
  p.eye = eye;
  p.surfaceToken = surfaceToken;
  p.flags = flags;
  p.boundsUMin = boundsUVMinMax[0];
  p.boundsUMax = boundsUVMinMax[1];
  p.boundsVMin = boundsUVMinMax[2];
  p.boundsVMax = boundsUVMinMax[3];
  std::memcpy(p.renderPoseHmd, renderPose, sizeof(p.renderPoseHmd));
  if (renderPoseLeft)  std::memcpy(p.renderPoseLeft,  renderPoseLeft,  sizeof(p.renderPoseLeft));
  if (renderPoseRight) std::memcpy(p.renderPoseRight, renderPoseRight, sizeof(p.renderPoseRight));
  if (leftFov)  std::memcpy(p.leftFov,  leftFov,  sizeof(p.leftFov));
  if (rightFov) std::memcpy(p.rightFov, rightFov, sizeof(p.rightFov));
  // Submit is fire-and-forget; daemon does not ack to keep the GPU thread
  // off the synchronous critical path.
  return sendMessage(wire::MessageType::SubmitFrame, &p, sizeof(p));
}

bool DaemonRpc::updateActions(const std::vector<uint64_t>& handles,
                              std::vector<wire::ActionStateEntry>& out) {
  std::lock_guard<std::mutex> lk(io_);
  if (fd_ < 0) return false;

  std::vector<uint8_t> req;
  req.resize(sizeof(wire::ActionUpdatePayload) + handles.size() * sizeof(uint64_t));
  auto* hdr = reinterpret_cast<wire::ActionUpdatePayload*>(req.data());
  hdr->handleCount = static_cast<uint32_t>(handles.size());
  if (!handles.empty()) {
    std::memcpy(req.data() + sizeof(*hdr), handles.data(),
                handles.size() * sizeof(uint64_t));
  }
  uint32_t reqId = 0;
  if (!sendMessage(wire::MessageType::ActionUpdate, req.data(),
                   static_cast<uint32_t>(req.size()), &reqId)) return false;

  std::vector<uint8_t> reply;
  if (!recvMessage(wire::MessageType::ActionState, reqId, reply)) return false;
  if (reply.size() < sizeof(wire::ActionStatePayload)) return false;
  const auto* ph = reinterpret_cast<const wire::ActionStatePayload*>(reply.data());
  size_t expected = sizeof(*ph) + ph->entryCount * sizeof(wire::ActionStateEntry);
  if (reply.size() != expected) return false;
  out.assign(ph->entryCount, {});
  if (ph->entryCount) {
    std::memcpy(out.data(),
                reply.data() + sizeof(*ph),
                ph->entryCount * sizeof(wire::ActionStateEntry));
  }
  return true;
}

bool DaemonRpc::triggerHaptic(uint64_t deviceHandle,
                              float startSecondsFromNow,
                              float durationSeconds,
                              float frequency,
                              float amplitude) {
  std::lock_guard<std::mutex> lk(io_);
  if (fd_ < 0) return false;
  wire::HapticPayload p{};
  p.deviceHandle = deviceHandle;
  p.startSecondsFromNow = startSecondsFromNow;
  p.durationSeconds = durationSeconds;
  p.frequency = frequency;
  p.amplitude = amplitude;
  return sendMessage(wire::MessageType::Haptic, &p, sizeof(p));
}

bool DaemonRpc::ping() {
  std::lock_guard<std::mutex> lk(io_);
  if (fd_ < 0) return false;
  uint32_t reqId = 0;
  if (!sendMessage(wire::MessageType::Ping, nullptr, 0, &reqId)) return false;
  std::vector<uint8_t> reply;
  return recvMessage(wire::MessageType::Pong, reqId, reply);
}

}  // namespace fuvr::openvr_shim
