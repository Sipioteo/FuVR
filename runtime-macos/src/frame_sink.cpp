// SPDX-License-Identifier: Apache-2.0
#include "fuvr/frame_sink.hpp"

#include "fuvr/daemon_client.hpp"
#include "fuvr/iosurface_swapchain.hpp"

namespace fuvr::runtime {

void DaemonFrameSink::submit(const SubmittedFrame& frame) noexcept {
  if (client_ == nullptr || frame.ioSurface == nullptr) return;
  uint32_t machRight = iosurfaceCreateMachSendRight(frame.ioSurface);
  if (machRight == 0) return;
  SubmitFrameArgs args{};
  args.frameId = frame.frameId;
  args.renderStartNs = frame.renderStartNs;
  args.machSendRight = machRight;
  args.forceIdr = frame.forceIdr;
  args.leftEye = frame.renderedLeft;
  args.rightEye = frame.renderedRight;
  client_->submitFrame(frame.sessionId, args);
  // Why: SCM_RIGHTS duplicates the right into the receiver's task; our local
  // copy still owns its own name and must be released to avoid leaking ports.
  iosurfaceReleaseMachSendRight(machRight);
}

std::unique_ptr<FrameSink> makeDefaultFrameSink() {
  return std::unique_ptr<FrameSink>(new NullFrameSink());
}

std::unique_ptr<FrameSink> makeDaemonFrameSink(DaemonClient* client) {
  return std::unique_ptr<FrameSink>(new DaemonFrameSink(client));
}

}  // namespace fuvr::runtime
