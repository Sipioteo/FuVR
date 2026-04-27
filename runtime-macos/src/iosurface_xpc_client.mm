// SPDX-License-Identifier: Apache-2.0
#include "fuvr/iosurface_xpc_client.hpp"

#import <Foundation/Foundation.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOSurface/IOSurface.h>
#include <mach/mach.h>
#include <xpc/xpc.h>

#include <cstdlib>
#include <cstring>

#include "fuvr/inprocess_surface_registry.hpp"

namespace fuvr::runtime {

namespace {

bool inprocessMode() {
  const char* v = std::getenv("FUVR_INPROCESS_HANDOFF");
  return v && std::strcmp(v, "0") != 0 && v[0] != '\0';
}

class XpcClientImpl final : public IOSurfaceXpcClient {
 public:
  bool start(const char* serviceName) {
    if (inprocessMode()) return true;
    conn_ = xpc_connection_create_mach_service(serviceName, NULL, 0);
    if (conn_ == nullptr) return false;
    xpc_connection_set_event_handler(conn_, ^(xpc_object_t event) {
      (void)event;
    });
    xpc_connection_resume(conn_);
    return true;
  }

  ~XpcClientImpl() override {
    if (conn_) {
      xpc_connection_cancel(conn_);
      xpc_release(conn_);
      conn_ = nullptr;
    }
  }

  void sendSurface(uint64_t token, IOSurfaceRef surface) override {
    if (surface == nullptr) return;
    if (inprocessMode()) {
      fuvr::InProcessSurfaceRegistry::shared().put(token, surface);
      return;
    }
    if (conn_ == nullptr) return;
    mach_port_t port = IOSurfaceCreateMachPort(surface);
    if (port == MACH_PORT_NULL) return;
    xpc_object_t dict = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_uint64(dict, "token", token);
    xpc_dictionary_set_mach_send(dict, "surface", port);
    xpc_connection_send_message(conn_, dict);
    xpc_release(dict);
    // Why: xpc_dictionary_set_mach_send internalises the send-right for
    // delivery, so dropping our local copy here is safe and avoids a leak.
    mach_port_deallocate(mach_task_self(), port);
  }

 private:
  xpc_connection_t conn_ = nullptr;
};

}  // namespace

std::unique_ptr<IOSurfaceXpcClient> IOSurfaceXpcClient::create(
    const char* serviceName) {
  auto impl = std::make_unique<XpcClientImpl>();
  if (!impl->start(serviceName)) return nullptr;
  return impl;
}

}  // namespace fuvr::runtime
