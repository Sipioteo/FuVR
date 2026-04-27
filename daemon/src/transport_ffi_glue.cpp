// SPDX-License-Identifier: Apache-2.0
#include <cstddef>
#include <cstdint>

#include "fuvr_transport.h"

namespace fuvr::daemon {

#ifdef FUVR_DAEMON_NO_TRANSPORT
// Stubs so the daemon links without the Rust dylib. Why: lets reviewers and
// CI build the C++ side before the Rust transport is compiled.
extern "C" FuvrTransport* fuvr_transport_create(FuvrTransportKind, const char*) { return nullptr; }
extern "C" int32_t fuvr_transport_send(FuvrTransport*, FuvrChannel, const uint8_t*, size_t) { return 0; }
extern "C" void fuvr_transport_set_recv_callback(FuvrTransport*, FuvrRecvCallback, void*) {}
extern "C" void fuvr_transport_destroy(FuvrTransport*) {}
#endif

int32_t transportSend(FuvrTransport* h, FuvrChannel ch, const uint8_t* data, size_t len) {
    return fuvr_transport_send(h, ch, data, len);
}

void transportSetRecvCallback(FuvrTransport* h, FuvrRecvCallback cb, void* user) {
    fuvr_transport_set_recv_callback(h, cb, user);
}

FuvrTransport* transportCreate(FuvrTransportKind k, const char* endpoint) {
    return fuvr_transport_create(k, endpoint);
}

void transportDestroy(FuvrTransport* h) {
    fuvr_transport_destroy(h);
}

} // namespace fuvr::daemon
