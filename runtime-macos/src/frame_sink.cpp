// SPDX-License-Identifier: Apache-2.0
#include "fuvr/frame_sink.hpp"

#include <atomic>

#include "fuvr/daemon_client.hpp"
#include "fuvr/iosurface_swapchain.hpp"
#include "fuvr/iosurface_xpc_client.hpp"

namespace fuvr::runtime {

namespace {
std::atomic<uint64_t> g_nextToken{1};
}

void DaemonFrameSink::submit(const SubmittedFrame& frame) noexcept {
  if (client_ == nullptr || frame.ioSurface == nullptr) return;
  const uint64_t token = g_nextToken.fetch_add(1, std::memory_order_relaxed);

  // Order matters per ADR-0007: ship the IOSurface first so the daemon's
  // index is populated when the matching SubmitFrameRequest lands.
  if (auto* xpc = client_->xpcClient()) {
    xpc->sendSurface(token, frame.ioSurface);
  }

  SubmitFrameArgs args{};
  args.frameId = frame.frameId;
  args.renderStartNs = frame.renderStartNs;
  args.surfaceToken = token;
  args.forceIdr = frame.forceIdr;
  args.leftEye = frame.renderedLeft;
  args.rightEye = frame.renderedRight;
  client_->submitFrame(frame.sessionId, args);
}

std::unique_ptr<FrameSink> makeDefaultFrameSink() {
  return std::unique_ptr<FrameSink>(new NullFrameSink());
}

std::unique_ptr<FrameSink> makeDaemonFrameSink(DaemonClient* client) {
  return std::unique_ptr<FrameSink>(new DaemonFrameSink(client));
}

}  // namespace fuvr::runtime
