// SPDX-License-Identifier: Apache-2.0
#include "fuvr/iosurface_xpc_service.hpp"

#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <xpc/xpc.h>

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace fuvr::daemon {

namespace {

uint64_t nowNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

class XpcServiceImpl final : public IOSurfaceXpcService {
 public:
  bool start(const char* serviceName) {
    listener_ = xpc_connection_create_mach_service(
        serviceName, NULL, XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if (listener_ == nullptr) return false;

    xpc_connection_set_event_handler(listener_, ^(xpc_object_t peer) {
      xpc_type_t t = xpc_get_type(peer);
      if (t != XPC_TYPE_CONNECTION) return;
      xpc_connection_t conn = peer;
      xpc_connection_set_event_handler(conn, ^(xpc_object_t msg) {
        handleMessage(msg);
      });
      xpc_connection_resume(conn);
    });
    xpc_connection_resume(listener_);
    return true;
  }

  ~XpcServiceImpl() override {
    if (listener_) {
      xpc_connection_cancel(listener_);
      xpc_release(listener_);
      listener_ = nullptr;
    }
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [_, e] : map_) {
      if (e.surface) CFRelease(e.surface);
    }
    map_.clear();
  }

  IOSurfaceRef takeSurface(uint64_t token) override {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(token);
    if (it == map_.end()) return nullptr;
    IOSurfaceRef out = it->second.surface;
    map_.erase(it);
    return out;
  }

  void evict(uint64_t maxAgeNs) override {
    const uint64_t now = nowNs();
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = map_.begin(); it != map_.end();) {
      if (now - it->second.receivedAtNs > maxAgeNs) {
        if (it->second.surface) CFRelease(it->second.surface);
        it = map_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  struct Entry {
    IOSurfaceRef surface;
    uint64_t receivedAtNs;
  };

  void handleMessage(xpc_object_t msg) {
    if (xpc_get_type(msg) != XPC_TYPE_DICTIONARY) return;
    uint64_t token = xpc_dictionary_get_uint64(msg, "token");
    mach_port_t port = xpc_dictionary_copy_mach_send(msg, "surface");
    if (port == MACH_PORT_NULL) return;
    IOSurfaceRef surface = IOSurfaceLookupFromMachPort(port);
    mach_port_deallocate(mach_task_self(), port);
    if (surface == nullptr) return;
    // IOSurfaceLookupFromMachPort returns a +1 retained reference; keep it.
    std::lock_guard<std::mutex> lk(mu_);
    auto it = map_.find(token);
    if (it != map_.end()) {
      if (it->second.surface) CFRelease(it->second.surface);
      it->second = Entry{surface, nowNs()};
      return;
    }
    map_.emplace(token, Entry{surface, nowNs()});
  }

  xpc_connection_t listener_ = nullptr;
  std::mutex mu_;
  std::unordered_map<uint64_t, Entry> map_;
};

}  // namespace

std::unique_ptr<IOSurfaceXpcService> IOSurfaceXpcService::create(
    const char* serviceName) {
  auto impl = std::make_unique<XpcServiceImpl>();
  if (!impl->start(serviceName)) return nullptr;
  return impl;
}

}  // namespace fuvr::daemon
