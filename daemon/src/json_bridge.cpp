// SPDX-License-Identifier: Apache-2.0
//
// Minimal JSON-over-UDS shim that mirrors the small subset of control verbs
// used by mac-app/Sources/FuVRControl. The mac-app stays on JSON for now;
// this bridge forwards translated requests into the Cap'n Proto rpc socket.
//
// Phase-1 stub: a real implementation will live alongside the rpc_server
// once the mac-app contract stabilises. See daemon/TODO.md.

#include <string>

namespace fuvr::daemon {

bool startJsonBridgeStub(const std::string& /*controlSockPath*/,
                        const std::string& /*rpcSockPath*/) {
    return false;
}

} // namespace fuvr::daemon
