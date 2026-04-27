// SPDX-License-Identifier: Apache-2.0
#include "fuvr/daemon_client.hpp"

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <thread>

namespace {

std::string tmpSock() {
  std::string p = std::filesystem::temp_directory_path().string();
  p += "/fuvr-resil-";
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

}  // namespace

TEST(SessionResilience, DisconnectAndReconnectFireCallbacks) {
  std::string path = tmpSock();
  int listener = makeListener(path);
  ASSERT_GE(listener, 0);

  std::atomic<bool> stopServer{false};
  std::thread server([&] {
    int c = ::accept(listener, nullptr, nullptr);
    if (c >= 0) {
      // Drop the connection immediately.
      ::shutdown(c, SHUT_RDWR);
      ::close(c);
    }
    while (!stopServer.load()) {
      // Re-accept a second connection (the reconnect attempt).
      int c2 = ::accept(listener, nullptr, nullptr);
      if (c2 >= 0) {
        // Hold open briefly so the client observes the reconnect.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ::shutdown(c2, SHUT_RDWR);
        ::close(c2);
        break;
      }
    }
  });

  fuvr::runtime::DaemonClient dc;
  dc.setSocketPathForTesting(path);
  dc.setMaxBackoffMs(200);

  std::atomic<int> disconnects{0};
  std::atomic<int> reconnects{0};
  dc.setDisconnectCallback([&]() { disconnects.fetch_add(1); });
  dc.setReconnectCallback([&]() { reconnects.fetch_add(1); });

  ASSERT_TRUE(dc.ensureConnected());

  // Wait up to 3s for the callbacks to fire.
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (std::chrono::steady_clock::now() < deadline) {
    if (disconnects.load() >= 1 && reconnects.load() >= 1) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  EXPECT_GE(disconnects.load(), 1);
  EXPECT_GE(reconnects.load(), 1);

  stopServer.store(true);
  dc.shutdown();
  // Best-effort wakeup for accept().
  int wake = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (wake >= 0) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    ::connect(wake, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::close(wake);
  }
  if (server.joinable()) server.join();
  ::close(listener);
  ::unlink(path.c_str());
}
