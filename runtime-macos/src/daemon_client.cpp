// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon_client.hpp"

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <kj/array.h>
#include <kj/io.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "fuvr/runtime.hpp"
#include "fuvrd.capnp.h"

namespace fuvr::runtime {

namespace {

constexpr size_t kReadBufBytes = 64 * 1024;

// Read FUVR_RT_DEBUG once at startup; cached so the 1Hz hot-path branch is
// just an atomic-load + compare.
inline bool latencyDebugEnabled() noexcept {
    static const bool on = std::getenv("FUVR_RT_DEBUG") != nullptr;
    return on;
}

PoseSample poseFromSnapshot(const fuvr::daemon::PoseSnapshot::Reader& s) noexcept {
  PoseSample out{};
  out.timestampNs = s.getReceivedAtNs();
  out.leftEye.position = Vec3{s.getLeftPosX(), s.getLeftPosY(), s.getLeftPosZ()};
  out.leftEye.orientation =
      Quat{s.getLeftRotX(), s.getLeftRotY(), s.getLeftRotZ(), s.getLeftRotW()};
  out.rightEye.position =
      Vec3{s.getRightPosX(), s.getRightPosY(), s.getRightPosZ()};
  out.rightEye.orientation =
      Quat{s.getRightRotX(), s.getRightRotY(), s.getRightRotZ(),
           s.getRightRotW()};
  out.linearVelocity = Vec3{s.getLinVelX(), s.getLinVelY(), s.getLinVelZ()};
  out.angularVelocity = Vec3{s.getAngVelX(), s.getAngVelY(), s.getAngVelZ()};
  out.leftFov = Fov{s.getLeftFovAngleLeft(),  s.getLeftFovAngleRight(),
                    s.getLeftFovAngleUp(),    s.getLeftFovAngleDown()};
  out.rightFov = Fov{s.getRightFovAngleLeft(), s.getRightFovAngleRight(),
                     s.getRightFovAngleUp(),   s.getRightFovAngleDown()};

  out.leftControllerActive = s.getLeftControllerActive();
  out.leftController.position = Vec3{s.getLeftControllerPosX(),
                                     s.getLeftControllerPosY(),
                                     s.getLeftControllerPosZ()};
  out.leftController.orientation = Quat{
      s.getLeftControllerRotX(), s.getLeftControllerRotY(),
      s.getLeftControllerRotZ(), s.getLeftControllerRotW()};
  out.leftControllerLinVel = Vec3{s.getLeftControllerLinVelX(),
                                  s.getLeftControllerLinVelY(),
                                  s.getLeftControllerLinVelZ()};
  out.leftControllerAngVel = Vec3{s.getLeftControllerAngVelX(),
                                  s.getLeftControllerAngVelY(),
                                  s.getLeftControllerAngVelZ()};
  out.rightControllerActive = s.getRightControllerActive();
  out.rightController.position = Vec3{s.getRightControllerPosX(),
                                      s.getRightControllerPosY(),
                                      s.getRightControllerPosZ()};
  out.rightController.orientation = Quat{
      s.getRightControllerRotX(), s.getRightControllerRotY(),
      s.getRightControllerRotZ(), s.getRightControllerRotW()};
  out.rightControllerLinVel = Vec3{s.getRightControllerLinVelX(),
                                   s.getRightControllerLinVelY(),
                                   s.getRightControllerLinVelZ()};
  out.rightControllerAngVel = Vec3{s.getRightControllerAngVelX(),
                                   s.getRightControllerAngVelY(),
                                   s.getRightControllerAngVelZ()};
  return out;
}

// Send an envelope as a 4-byte LE length prefix + flat-array bytes. The
// `machSendRight` is encoded in-band via SubmitFrameRequest.surfaceToken
// rather than as an SCM_RIGHTS ancillary fd: macOS does not support sending
// mach ports via SCM_RIGHTS (that mechanism is fd-only). The proto comment
// referencing SCM_RIGHTS is aspirational; the daemon must translate the
// mach port name across tasks via a side channel (mach_msg). See TODO.md.
bool sendEnvelope(int fd, capnp::MessageBuilder& mb, int machSendRight,
                  std::mutex& sendMu) noexcept {
  (void)machSendRight;
  kj::Array<capnp::word> flat = capnp::messageToFlatArray(mb);
  kj::ArrayPtr<const kj::byte> bytes = flat.asBytes();

  // Frame: 4-byte LE length prefix + raw flat-array bytes. We don't pack the
  // outer framing; recipients use messageToFlatArray on the same buffer.
  const uint32_t lenLE = static_cast<uint32_t>(bytes.size());
  uint8_t hdr[4];
  hdr[0] = static_cast<uint8_t>(lenLE & 0xff);
  hdr[1] = static_cast<uint8_t>((lenLE >> 8) & 0xff);
  hdr[2] = static_cast<uint8_t>((lenLE >> 16) & 0xff);
  hdr[3] = static_cast<uint8_t>((lenLE >> 24) & 0xff);

  std::lock_guard<std::mutex> lk(sendMu);
  size_t off = 0;
  while (off < sizeof(hdr)) {
    ssize_t m = ::send(fd, hdr + off, sizeof(hdr) - off, 0);
    if (m <= 0) {
      if (m < 0 && errno == EINTR) continue;
      return false;
    }
    off += static_cast<size_t>(m);
  }
  off = 0;
  while (off < bytes.size()) {
    ssize_t m = ::send(fd, bytes.begin() + off, bytes.size() - off, 0);
    if (m <= 0) {
      if (m < 0 && errno == EINTR) continue;
      return false;
    }
    off += static_cast<size_t>(m);
  }
  return true;
}

}  // namespace

std::string resolveDaemonSocketPath() {
  if (const char* xdg = std::getenv("XDG_RUNTIME_DIR")) {
    if (xdg[0] != '\0') {
      std::string p = xdg;
      if (p.back() != '/') p.push_back('/');
      p += "fuvr/rpc.sock";
      return p;
    }
  }
  if (const char* home = std::getenv("HOME")) {
    return std::string(home) + "/Library/Caches/fuvr/rpc.sock";
  }
  passwd* pw = ::getpwuid(::getuid());
  if (pw && pw->pw_dir) {
    return std::string(pw->pw_dir) + "/Library/Caches/fuvr/rpc.sock";
  }
  return "/tmp/fuvr-rpc.sock";
}

DaemonClient::DaemonClient() : socketPath_(resolveDaemonSocketPath()) {}

DaemonClient::~DaemonClient() { shutdown(); }

void DaemonClient::setSocketPathForTesting(std::string path) noexcept {
  socketPath_ = std::move(path);
}

void DaemonClient::setPoseCallback(PoseCallback cb) noexcept {
  std::lock_guard<std::mutex> lk(cbMutex_);
  poseCb_ = std::move(cb);
}

void DaemonClient::setEncodeStatsCallback(EncodeStatsCallback cb) noexcept {
  std::lock_guard<std::mutex> lk(cbMutex_);
  statsCb_ = std::move(cb);
}

void DaemonClient::setDisconnectCallback(DisconnectCallback cb) noexcept {
  std::lock_guard<std::mutex> lk(cbMutex_);
  disconnectCb_ = std::move(cb);
  disconnectFired_ = false;
}

void DaemonClient::setInputCallback(InputCallback cb) noexcept {
  std::lock_guard<std::mutex> lk(cbMutex_);
  inputCb_ = std::move(cb);
}

void DaemonClient::setReconnectCallback(ReconnectCallback cb) noexcept {
  std::lock_guard<std::mutex> lk(cbMutex_);
  reconnectCb_ = std::move(cb);
}

bool DaemonClient::connectLocked() noexcept {
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return false;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  if (socketPath_.size() >= sizeof(addr.sun_path)) {
    ::close(fd);
    return false;
  }
  std::strncpy(addr.sun_path, socketPath_.c_str(), sizeof(addr.sun_path) - 1);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return false;
  }
  fd_.store(fd);
  stop_.store(false);
  if (!reader_.joinable()) {
    reader_ = std::thread([this] { readerLoop(); });
  }
  backoffMs_ = 20;
  return true;
}

bool DaemonClient::ensureConnected() noexcept {
  if (fd_.load() >= 0) return true;
  return connectLocked();
}

void DaemonClient::scheduleReconnect() noexcept {
  std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs_));
  // Why: 5s cap matches the spec for session-loss resilience; capped exponential
  // backoff avoids a thundering reconnect storm against a daemon under restart.
  uint32_t cap = maxBackoffMs_;
  if (cap == 0) cap = 5000;
  backoffMs_ = backoffMs_ < cap / 2 ? backoffMs_ * 2 : cap;
  reconnectAttempts_.fetch_add(1, std::memory_order_relaxed);
}

void DaemonClient::readerLoop() noexcept {
  std::vector<uint8_t> buf;
  buf.reserve(kReadBufBytes);
  uint8_t tmp[kReadBufBytes];
  while (!stop_.load()) {
    int fd = fd_.load();
    if (fd < 0) {
      if (stop_.load()) break;
      scheduleReconnect();
      if (stop_.load()) break;
      if (!ensureConnected()) continue;
      fd = fd_.load();
      if (fd < 0) continue;
      ReconnectCallback rc;
      {
        std::lock_guard<std::mutex> lk(cbMutex_);
        rc = reconnectCb_;
        disconnectFired_ = false;
      }
      if (rc) rc();
    }
    ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      // Race-safe close: shutdown() may have already taken the fd via
      // `fd_.exchange(-1)` and closed it. If we close again on the local
      // `fd` variable, the kernel can recycle that descriptor for the
      // NEXT session's socket → we silently kill a brand-new connection.
      // Only close if we win the exchange (i.e. fd_ still held our fd).
      int prev = fd_.exchange(-1);
      if (prev >= 0) {
        ::close(prev);
      }
      if (stop_.load()) break;
      DisconnectCallback dc;
      {
        std::lock_guard<std::mutex> lk(cbMutex_);
        if (!disconnectFired_) {
          dc = disconnectCb_;
          disconnectFired_ = true;
        }
      }
      if (dc) dc();
      continue;
    }
    buf.insert(buf.end(), tmp, tmp + n);
    while (buf.size() >= 4) {
      uint32_t len = static_cast<uint32_t>(buf[0]) |
                     (static_cast<uint32_t>(buf[1]) << 8) |
                     (static_cast<uint32_t>(buf[2]) << 16) |
                     (static_cast<uint32_t>(buf[3]) << 24);
      if (buf.size() < 4u + len) break;
      const size_t words = len / sizeof(capnp::word);
      kj::Array<capnp::word> aligned = kj::heapArray<capnp::word>(words);
      std::memcpy(aligned.begin(), buf.data() + 4, len);
      capnp::FlatArrayMessageReader reader(aligned.asPtr());
      auto env = reader.getRoot<fuvr::daemon::Envelope>();
      const uint64_t seq = env.getSeq();
      auto body = env.getBody();
      switch (body.which()) {
        case fuvr::daemon::Envelope::Body::START_SESSION_ACK: {
          auto ack = body.getStartSessionAck();
          std::lock_guard<std::mutex> lk(respMutex_);
          if (pendingSeq_ != 0 && pendingSeq_ == seq) {
            lastStartAck_.sessionId = ack.getSessionId();
            lastStartAck_.clockOffsetNs = ack.getClockOffsetNs();
            lastStartAck_.oneWayDelayNs = ack.getOneWayDelayNs();
            lastStartAck_.virtualDisplayId = ack.getVirtualDisplayId();
            gotResponse_ = true;
            respCv_.notify_all();
          }
          break;
        }
        case fuvr::daemon::Envelope::Body::DEVICE_CAPABILITIES_RESPONSE: {
          auto r = body.getDeviceCapabilitiesResponse();
          std::lock_guard<std::mutex> lk(respMutex_);
          if (pendingSeq_ != 0 && pendingSeq_ == seq) {
            DeviceCapabilities caps;
            caps.valid = r.getValid();
            caps.deviceModel = r.getDeviceModel().cStr();
            caps.systemVersion = r.getSystemVersion().cStr();
            caps.perEyeWidth = r.getPerEyeWidth();
            caps.perEyeHeight = r.getPerEyeHeight();
            auto rates = r.getRefreshRatesHz();
            caps.refreshRatesHz.reserve(rates.size());
            for (auto v : rates) caps.refreshRatesHz.push_back(v);
            caps.hasHandTracking = r.getHasHandTracking();
            caps.hasEyeTracking = r.getHasEyeTracking();
            lastCapsResponse_ = std::move(caps);
            gotCapsResponse_ = true;
            respCv_.notify_all();
          }
          break;
        }
        case fuvr::daemon::Envelope::Body::POSE_SNAPSHOT: {
          auto snap = body.getPoseSnapshot();
          PoseSample s = poseFromSnapshot(snap);
          PoseCallback cb;
          {
            std::lock_guard<std::mutex> lk(cbMutex_);
            cb = poseCb_;
          }
          if (cb) cb(s);
          // [LATENCY-DEBUG] Most diagnostic line for the user's "2-3s lag"
          // hypothesis: age_us is steady_clock(now) - sample.timestampNs.
          // If age_us ramps from ~10ms at session start to seconds at 30s in,
          // the dispatch chain is queueing.
          if (latencyDebugEnabled()) {
            static std::atomic<uint64_t> lastLogNs{0};
            uint64_t nowL = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            uint64_t prev = lastLogNs.load(std::memory_order_relaxed);
            if (nowL - prev >= 1'000'000'000ull &&
                lastLogNs.compare_exchange_strong(prev, nowL)) {
              long long ageUs = static_cast<long long>(
                  (static_cast<int64_t>(nowL) -
                   static_cast<int64_t>(s.timestampNs)) / 1000);
              std::fprintf(stderr,
                           "[fuvr-rt] [LATENCY-DEBUG] pose recv: "
                           "age_us=%lld bufsize=%u\n",
                           ageUs, static_cast<unsigned>(buf.size()));
            }
          }
          break;
        }
        case fuvr::daemon::Envelope::Body::ENCODE_STATS: {
          auto stats = body.getEncodeStats();
          EncodeStatSample sample{};
          sample.frameId = stats.getFrameId();
          sample.encodeDurationNs = stats.getEncodeDurationNs();
          sample.encodedSizeBytes = stats.getEncodedSizeBytes();
          sample.wasKeyframe = stats.getWasKeyframe();
          sample.arrivalNs =
              static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count());
          EncodeStatsCallback cb;
          {
            std::lock_guard<std::mutex> lk(cbMutex_);
            cb = statsCb_;
          }
          if (cb) cb(sample);
          break;
        }
        case fuvr::daemon::Envelope::Body::INPUT_SNAPSHOT: {
          auto in = body.getInputSnapshot();
          InputSnapshot snap{};
          snap.receivedAtNs = in.getReceivedAtNs();
          snap.questClockNs = in.getQuestClockNs();
          auto fillCtl = [](const fuvr::daemon::ControllerInput::Reader& r,
                            ControllerInputState& out) {
            out.active = r.getActive();
            out.trigger = r.getTrigger();
            out.squeeze = r.getSqueeze();
            out.thumbstickX = r.getThumbstickX();
            out.thumbstickY = r.getThumbstickY();
            out.thumbstickClick = r.getThumbstickClick();
            out.thumbstickTouch = r.getThumbstickTouch();
            out.triggerTouch = r.getTriggerTouch();
            out.buttonAClick = r.getButtonAClick();
            out.buttonATouch = r.getButtonATouch();
            out.buttonBClick = r.getButtonBClick();
            out.buttonBTouch = r.getButtonBTouch();
            out.systemClick = r.getSystemClick();
            out.thumbrest = r.getThumbrest();
          };
          fillCtl(in.getLeft(), snap.left);
          fillCtl(in.getRight(), snap.right);
          InputCallback cb;
          {
            std::lock_guard<std::mutex> lk(cbMutex_);
            cb = inputCb_;
          }
          if (cb) cb(snap);
          break;
        }
        case fuvr::daemon::Envelope::Body::METRICS:
        case fuvr::daemon::Envelope::Body::LOG:
        case fuvr::daemon::Envelope::Body::PONG:
        case fuvr::daemon::Envelope::Body::OK:
        case fuvr::daemon::Envelope::Body::ERROR:
        default:
          break;
      }
      buf.erase(buf.begin(), buf.begin() + 4 + len);
    }
  }
}

bool DaemonClient::startSession(const StartSessionParams& params,
                                StartSessionResult* out,
                                uint32_t timeoutMs) noexcept {
  if (!ensureConnected()) return false;
  const uint64_t seq = nextSeq_.fetch_add(1);
  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<fuvr::daemon::Envelope>();
  env.setSeq(seq);
  env.setStreamId(0);
  auto req = env.getBody().initStartSession();
  req.setPerEyeWidth(params.perEyeWidth);
  req.setPerEyeHeight(params.perEyeHeight);
  req.setRefreshRateHz(params.refreshRateHz);
  req.setVideoCodec(params.useHevc ? fuvr::daemon::VideoCodec::HEVC
                                   : fuvr::daemon::VideoCodec::H264);
  req.setVideoBitrateBps(params.videoBitrateBps);

  {
    std::lock_guard<std::mutex> lk(respMutex_);
    pendingSeq_ = seq;
    gotResponse_ = false;
  }

  if (!sendEnvelope(fd_.load(), mb, 0, sendMutex_)) return false;

  std::unique_lock<std::mutex> lk(respMutex_);
  if (!respCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                        [this] { return gotResponse_; })) {
    return false;
  }
  if (out) *out = lastStartAck_;
  pendingSeq_ = 0;
  return true;
}

bool DaemonClient::getDeviceCapabilities(DeviceCapabilities* out,
                                         uint32_t timeoutMs) noexcept {
  if (out == nullptr) return false;
  *out = {};
  if (!ensureConnected()) return false;
  const uint64_t seq = nextSeq_.fetch_add(1);
  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<fuvr::daemon::Envelope>();
  env.setSeq(seq);
  env.setStreamId(0);
  env.getBody().setGetDeviceCapabilities();

  {
    std::lock_guard<std::mutex> lk(respMutex_);
    pendingSeq_ = seq;
    gotCapsResponse_ = false;
  }

  if (!sendEnvelope(fd_.load(), mb, 0, sendMutex_)) return false;

  std::unique_lock<std::mutex> lk(respMutex_);
  if (!respCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                        [this] { return gotCapsResponse_; })) {
    pendingSeq_ = 0;
    return false;
  }
  *out = lastCapsResponse_;
  pendingSeq_ = 0;
  return true;
}

bool DaemonClient::subscribePoses(uint64_t sessionId) noexcept {
  if (!ensureConnected()) return false;
  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<fuvr::daemon::Envelope>();
  env.setSeq(nextSeq_.fetch_add(1));
  env.setStreamId(1);
  auto req = env.getBody().initStreamPoses();
  req.setSessionId(sessionId);
  return sendEnvelope(fd_.load(), mb, 0, sendMutex_);
}

bool DaemonClient::subscribeInputs(uint64_t sessionId) noexcept {
  if (!ensureConnected()) return false;
  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<fuvr::daemon::Envelope>();
  env.setSeq(nextSeq_.fetch_add(1));
  env.setStreamId(2);
  auto req = env.getBody().initStreamInputs();
  req.setSessionId(sessionId);
  return sendEnvelope(fd_.load(), mb, 0, sendMutex_);
}

bool DaemonClient::subscribeEncodeStats() noexcept {
  if (!ensureConnected()) return false;
  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<fuvr::daemon::Envelope>();
  env.setSeq(nextSeq_.fetch_add(1));
  env.setStreamId(3);
  env.getBody().setStreamEncodeStats();
  return sendEnvelope(fd_.load(), mb, 0, sendMutex_);
}

bool DaemonClient::submitFrame(uint64_t sessionId,
                               const SubmitFrameArgs& args) noexcept {
  if (!ensureConnected()) return false;
  capnp::MallocMessageBuilder mb;
  auto env = mb.initRoot<fuvr::daemon::Envelope>();
  env.setSeq(nextSeq_.fetch_add(1));
  env.setStreamId(0);
  auto req = env.getBody().initSubmitFrame();
  req.setSessionId(sessionId);
  req.setFrameId(args.frameId);
  req.setRenderStartNs(args.renderStartNs);
  req.setSurfaceToken(static_cast<uint32_t>(args.surfaceToken));
  req.setForceIdr(args.forceIdr);
  req.setRenderedLeftPosX(args.leftEye.position.x);
  req.setRenderedLeftPosY(args.leftEye.position.y);
  req.setRenderedLeftPosZ(args.leftEye.position.z);
  req.setRenderedLeftRotX(args.leftEye.orientation.x);
  req.setRenderedLeftRotY(args.leftEye.orientation.y);
  req.setRenderedLeftRotZ(args.leftEye.orientation.z);
  req.setRenderedLeftRotW(args.leftEye.orientation.w);
  req.setRenderedRightPosX(args.rightEye.position.x);
  req.setRenderedRightPosY(args.rightEye.position.y);
  req.setRenderedRightPosZ(args.rightEye.position.z);
  req.setRenderedRightRotX(args.rightEye.orientation.x);
  req.setRenderedRightRotY(args.rightEye.orientation.y);
  req.setRenderedRightRotZ(args.rightEye.orientation.z);
  req.setRenderedRightRotW(args.rightEye.orientation.w);
  req.setRenderedLeftFovAngleLeft(args.leftFov.angleLeft);
  req.setRenderedLeftFovAngleRight(args.leftFov.angleRight);
  req.setRenderedLeftFovAngleUp(args.leftFov.angleUp);
  req.setRenderedLeftFovAngleDown(args.leftFov.angleDown);
  req.setRenderedRightFovAngleLeft(args.rightFov.angleLeft);
  req.setRenderedRightFovAngleRight(args.rightFov.angleRight);
  req.setRenderedRightFovAngleUp(args.rightFov.angleUp);
  req.setRenderedRightFovAngleDown(args.rightFov.angleDown);
  return sendEnvelope(fd_.load(), mb, 0, sendMutex_);
}

void DaemonClient::shutdown() noexcept {
  stop_.store(true);
  int fd = fd_.exchange(-1);
  if (fd >= 0) {
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
  }
  if (reader_.joinable()) reader_.join();
}

}  // namespace fuvr::runtime
