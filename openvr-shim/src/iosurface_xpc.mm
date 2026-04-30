// SPDX-License-Identifier: Apache-2.0
#include "iosurface_xpc.hpp"

#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <xpc/xpc.h>

#include "log.hpp"

namespace fuvr::openvr_shim {

namespace {
constexpr const char* kServiceName = "com.fuvr.daemon.surface";
}

IOSurfaceXpc::IOSurfaceXpc() = default;

IOSurfaceXpc::~IOSurfaceXpc() { stop(); }

bool IOSurfaceXpc::start() {
  if (conn_ != nullptr) return true;
  xpc_connection_t c = xpc_connection_create_mach_service(kServiceName, NULL, 0);
  if (c == nullptr) {
    FUVR_LOG("xpc: failed to create connection to %s", kServiceName);
    return false;
  }
  xpc_connection_set_event_handler(c, ^(xpc_object_t event) {
    (void)event;
    // Daemon-side errors arrive here; we don't react synchronously — the
    // RPC socket carries Pong / WaitFrameOk failures which are easier to
    // surface to the calling game.
  });
  xpc_connection_resume(c);
  conn_ = static_cast<void*>(c);
  return true;
}

void IOSurfaceXpc::send(uint64_t token, IOSurfaceRef surface) {
  if (conn_ == nullptr || surface == nullptr) return;
  mach_port_t port = IOSurfaceCreateMachPort(surface);
  if (port == MACH_PORT_NULL) {
    FUVR_LOG("xpc: IOSurfaceCreateMachPort returned NULL for token %llu",
             (unsigned long long)token);
    return;
  }
  xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
  xpc_dictionary_set_uint64(dict, "token", token);
  xpc_dictionary_set_mach_send(dict, "surface", port);
  xpc_connection_send_message(static_cast<xpc_connection_t>(conn_), dict);
  xpc_release(dict);
  // The XPC subsystem retained the send-right for delivery; drop our copy.
  mach_port_deallocate(mach_task_self(), port);
}

void IOSurfaceXpc::stop() {
  if (conn_ != nullptr) {
    auto* c = static_cast<xpc_connection_t>(conn_);
    xpc_connection_cancel(c);
    xpc_release(c);
    conn_ = nullptr;
  }
}

}  // namespace fuvr::openvr_shim
