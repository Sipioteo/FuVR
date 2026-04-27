// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon_client.hpp"

#include <capnp/message.h>
#include <capnp/serialize.h>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "fuvrd.capnp.h"

namespace {

std::string mktempSocketPath() {
  std::string p = std::filesystem::temp_directory_path().string();
  p += "/fuvr-test-";
  p += std::to_string(::getpid());
  p += "-";
  p += std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  p += ".sock";
  return p;
}

int makeListener(const std::string& path) {
  ::unlink(path.c_str());
  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return -1;
  }
  if (::listen(fd, 1) < 0) {
    ::close(fd);
    return -1;
  }
  return fd;
}

bool readN(int fd, void* buf, size_t n) {
  uint8_t* p = static_cast<uint8_t*>(buf);
  while (n > 0) {
    ssize_t r = ::recv(fd, p, n, 0);
    if (r <= 0) return false;
    p += r;
    n -= r;
  }
  return true;
}

}  // namespace

TEST(DaemonClient, StartSessionRequestIsWellFormed) {
  std::string path = mktempSocketPath();
  int listener = makeListener(path);
  ASSERT_GE(listener, 0);

  std::atomic<bool> serverDone{false};
  std::atomic<uint32_t> seenWidth{0};
  std::atomic<uint64_t> seenSeq{0};

  std::thread server([&] {
    int client = ::accept(listener, nullptr, nullptr);
    ASSERT_GE(client, 0);
    uint8_t hdr[4];
    ASSERT_TRUE(readN(client, hdr, 4));
    uint32_t len = uint32_t(hdr[0]) | (uint32_t(hdr[1]) << 8) |
                   (uint32_t(hdr[2]) << 16) | (uint32_t(hdr[3]) << 24);
    std::vector<uint8_t> body(len);
    ASSERT_TRUE(readN(client, body.data(), len));

    const size_t words = len / sizeof(capnp::word);
    kj::Array<capnp::word> aligned = kj::heapArray<capnp::word>(words);
    std::memcpy(aligned.begin(), body.data(), len);
    capnp::FlatArrayMessageReader reader(aligned.asPtr());
    auto env = reader.getRoot<fuvr::daemon::Envelope>();
    seenSeq.store(env.getSeq());
    auto b = env.getBody();
    ASSERT_EQ(b.which(), fuvr::daemon::Envelope::Body::START_SESSION);
    auto req = b.getStartSession();
    seenWidth.store(req.getPerEyeWidth());

    // Send back ack with the same seq.
    capnp::MallocMessageBuilder mb;
    auto e = mb.initRoot<fuvr::daemon::Envelope>();
    e.setSeq(env.getSeq());
    auto ack = e.getBody().initStartSessionAck();
    ack.setSessionId(0xCAFEBABE);
    ack.setClockOffsetNs(123);
    ack.setOneWayDelayNs(7);
    ack.setVirtualDisplayId(0);
    auto flat = capnp::messageToFlatArray(mb);
    auto bytes = flat.asBytes();
    uint32_t l = static_cast<uint32_t>(bytes.size());
    uint8_t h[4] = {uint8_t(l), uint8_t(l >> 8), uint8_t(l >> 16),
                    uint8_t(l >> 24)};
    ::send(client, h, 4, 0);
    ::send(client, bytes.begin(), bytes.size(), 0);
    serverDone.store(true);
    ::close(client);
  });

  fuvr::runtime::DaemonClient dc;
  dc.setSocketPathForTesting(path);
  ASSERT_TRUE(dc.ensureConnected());

  fuvr::runtime::StartSessionParams params{};
  params.perEyeWidth = 2064;
  params.perEyeHeight = 2208;
  params.refreshRateHz = 90;
  params.videoBitrateBps = 50'000'000;
  fuvr::runtime::StartSessionResult result{};
  ASSERT_TRUE(dc.startSession(params, &result, 2000));
  EXPECT_EQ(result.sessionId, 0xCAFEBABEu);
  EXPECT_EQ(result.clockOffsetNs, 123);

  server.join();
  EXPECT_EQ(seenWidth.load(), 2064u);
  EXPECT_NE(seenSeq.load(), 0u);

  dc.shutdown();
  ::close(listener);
  ::unlink(path.c_str());
}
