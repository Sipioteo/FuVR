// SPDX-License-Identifier: Apache-2.0
#include "fuvr/event_queue.hpp"

#include <cstring>

namespace fuvr::runtime {

void EventQueue::push(const XrEventDataBuffer& event) noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  if (queue_.size() >= kCapacity) {
    queue_.pop_front();
    overflowPending_ = true;
    ++lostCount_;
  }
  queue_.push_back(event);
}

void EventQueue::pushSessionStateChanged(XrSession session,
                                         XrSessionState state,
                                         XrTime time) noexcept {
  XrEventDataBuffer ev{};
  ev.type = XR_TYPE_EVENT_DATA_BUFFER;
  auto* data = reinterpret_cast<XrEventDataSessionStateChanged*>(&ev);
  data->type = XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED;
  data->next = nullptr;
  data->session = session;
  data->state = state;
  data->time = time;
  push(ev);
}

void EventQueue::pushInteractionProfileChanged(XrSession session) noexcept {
  XrEventDataBuffer ev{};
  ev.type = XR_TYPE_EVENT_DATA_BUFFER;
  auto* data = reinterpret_cast<XrEventDataInteractionProfileChanged*>(&ev);
  data->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
  data->next = nullptr;
  data->session = session;
  push(ev);
}

bool EventQueue::pop(XrEventDataBuffer* out) noexcept {
  if (out == nullptr) return false;
  std::lock_guard<std::mutex> lk(mutex_);
  if (overflowPending_) {
    overflowPending_ = false;
    auto* lost = reinterpret_cast<XrEventDataEventsLost*>(out);
    lost->type = XR_TYPE_EVENT_DATA_EVENTS_LOST;
    lost->next = nullptr;
    lost->lostEventCount = lostCount_;
    lostCount_ = 0;
    return true;
  }
  if (queue_.empty()) return false;
  *out = queue_.front();
  queue_.pop_front();
  return true;
}

std::size_t EventQueue::size() const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return queue_.size() + (overflowPending_ ? 1 : 0);
}

}  // namespace fuvr::runtime
