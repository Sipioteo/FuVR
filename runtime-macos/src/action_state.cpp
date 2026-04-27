// SPDX-License-Identifier: Apache-2.0
#include "fuvr/action_state.hpp"

namespace fuvr::runtime {

namespace {

template <typename T>
inline void track(T& prev, const T& next, XrTime now, XrTime& outTime) {
  if (prev != next) {
    outTime = now;
    prev = next;
  }
}

}  // namespace

void ActionStateCache::update(const InputSnapshot& snap) noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  const XrTime now = static_cast<XrTime>(snap.receivedAtNs);
  for (int i = 0; i < 2; ++i) {
    const ControllerInputState& nxt = (i == 0) ? snap.left : snap.right;
    ControllerInputState& cur =
        (i == 0) ? live_.left : live_.right;
    Times& t = times_[i];
    track(cur.active, nxt.active, now, t.active);
    track(cur.trigger, nxt.trigger, now, t.trigger);
    track(cur.squeeze, nxt.squeeze, now, t.squeeze);
    if (cur.thumbstickX != nxt.thumbstickX ||
        cur.thumbstickY != nxt.thumbstickY) {
      t.thumbstick = now;
      cur.thumbstickX = nxt.thumbstickX;
      cur.thumbstickY = nxt.thumbstickY;
    }
    track(cur.thumbstickClick, nxt.thumbstickClick, now, t.thumbstickClick);
    track(cur.thumbstickTouch, nxt.thumbstickTouch, now, t.thumbstickTouch);
    track(cur.triggerTouch, nxt.triggerTouch, now, t.triggerTouch);
    track(cur.buttonAClick, nxt.buttonAClick, now, t.aClick);
    track(cur.buttonATouch, nxt.buttonATouch, now, t.aTouch);
    track(cur.buttonBClick, nxt.buttonBClick, now, t.bClick);
    track(cur.buttonBTouch, nxt.buttonBTouch, now, t.bTouch);
    track(cur.systemClick, nxt.systemClick, now, t.systemClick);
    track(cur.thumbrest, nxt.thumbrest, now, t.thumbrest);
  }
  live_.receivedAtNs = snap.receivedAtNs;
  live_.questClockNs = snap.questClockNs;
}

InputSnapshot ActionStateCache::snapshot() const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return live_;
}

XrTime ActionStateCache::triggerChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].trigger;
}
XrTime ActionStateCache::squeezeChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].squeeze;
}
XrTime ActionStateCache::thumbstickChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].thumbstick;
}
XrTime ActionStateCache::thumbstickClickChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].thumbstickClick;
}
XrTime ActionStateCache::thumbstickTouchChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].thumbstickTouch;
}
XrTime ActionStateCache::triggerTouchChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].triggerTouch;
}
XrTime ActionStateCache::aClickChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].aClick;
}
XrTime ActionStateCache::aTouchChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].aTouch;
}
XrTime ActionStateCache::bClickChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].bClick;
}
XrTime ActionStateCache::bTouchChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].bTouch;
}
XrTime ActionStateCache::systemClickChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].systemClick;
}
XrTime ActionStateCache::thumbrestChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].thumbrest;
}
XrTime ActionStateCache::activeChangeTime(Hand h) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return times_[handIdx(h)].active;
}

}  // namespace fuvr::runtime
