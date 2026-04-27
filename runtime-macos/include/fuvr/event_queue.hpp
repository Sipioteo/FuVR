// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <openxr/openxr.h>

#include <cstddef>
#include <deque>
#include <mutex>

namespace fuvr::runtime {

class EventQueue {
 public:
  static constexpr std::size_t kCapacity = 64;

  EventQueue() = default;

  EventQueue(const EventQueue&) = delete;
  EventQueue& operator=(const EventQueue&) = delete;

  void push(const XrEventDataBuffer& event) noexcept;

  void pushSessionStateChanged(XrSession session, XrSessionState state,
                               XrTime time = 0) noexcept;

  void pushInteractionProfileChanged(XrSession session) noexcept;

  bool pop(XrEventDataBuffer* out) noexcept;

  std::size_t size() const noexcept;

 private:
  mutable std::mutex mutex_;
  std::deque<XrEventDataBuffer> queue_;
  bool overflowPending_{false};
  uint32_t lostCount_{0};
};

}  // namespace fuvr::runtime
