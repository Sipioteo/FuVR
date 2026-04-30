// SPDX-License-Identifier: Apache-2.0
#pragma once
//
// Listens on `/tmp/fuvr_openvr.sock` for connections from the mock
// `libopenvr_api.dylib` shim, translates wire-protocol messages into
// the existing daemon session/pose/input plumbing, and ships responses
// back. Decoupled from `Daemon` so the listener can be swapped out for
// in-process testing.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fuvr::daemon {

class Daemon;  // forward decl: we only need to call `submit IOSurface` etc.

class OpenVrListener {
 public:
  OpenVrListener();
  ~OpenVrListener();

  /// Bind+listen on `socketPath`. The listener spawns one accept thread
  /// and one I/O thread per connected client. Returns false if the bind
  /// failed (already in use, no permission, etc.).
  bool start(const std::string& socketPath, Daemon* daemon);
  void stop();

 private:
  void acceptLoop();
  void clientLoop(int fd);

  std::atomic<bool> running_{false};
  int listenFd_{-1};
  std::string path_;
  Daemon* daemon_{nullptr};
  std::thread acceptThread_;
  std::vector<std::thread> clientThreads_;
};

}  // namespace fuvr::daemon
