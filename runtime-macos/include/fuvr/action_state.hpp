// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <openxr/openxr.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace fuvr::runtime {

enum class Hand : uint8_t {
  Left = 0,
  Right = 1,
};

struct ControllerInputState {
  bool active{false};
  float trigger{0.0f};
  float squeeze{0.0f};
  float thumbstickX{0.0f};
  float thumbstickY{0.0f};
  bool thumbstickClick{false};
  bool thumbstickTouch{false};
  bool triggerTouch{false};
  bool buttonAClick{false};
  bool buttonATouch{false};
  bool buttonBClick{false};
  bool buttonBTouch{false};
  bool systemClick{false};
  float thumbrest{0.0f};
};

struct InputSnapshot {
  uint64_t receivedAtNs{0};
  uint64_t questClockNs{0};
  ControllerInputState left{};
  ControllerInputState right{};
};

// Stores the most recent InputSnapshot from the daemon and tracks per-field
// "last change time" so XrActionStateBase::lastChangeTime is honest.
class ActionStateCache {
 public:
  ActionStateCache() = default;

  // Atomically swap the live snapshot. Updates per-field changeTime when a
  // value differs from the previous live snapshot.
  void update(const InputSnapshot& snap) noexcept;

  // Read the current snapshot. Cheap; thread-safe.
  InputSnapshot snapshot() const noexcept;

  // Per-field change time accessors. Returns 0 if never changed.
  XrTime triggerChangeTime(Hand h) const noexcept;
  XrTime squeezeChangeTime(Hand h) const noexcept;
  XrTime thumbstickChangeTime(Hand h) const noexcept;
  XrTime thumbstickClickChangeTime(Hand h) const noexcept;
  XrTime thumbstickTouchChangeTime(Hand h) const noexcept;
  XrTime triggerTouchChangeTime(Hand h) const noexcept;
  XrTime aClickChangeTime(Hand h) const noexcept;
  XrTime aTouchChangeTime(Hand h) const noexcept;
  XrTime bClickChangeTime(Hand h) const noexcept;
  XrTime bTouchChangeTime(Hand h) const noexcept;
  XrTime systemClickChangeTime(Hand h) const noexcept;
  XrTime thumbrestChangeTime(Hand h) const noexcept;
  XrTime activeChangeTime(Hand h) const noexcept;

 private:
  struct Times {
    XrTime trigger{0};
    XrTime squeeze{0};
    XrTime thumbstick{0};
    XrTime thumbstickClick{0};
    XrTime thumbstickTouch{0};
    XrTime triggerTouch{0};
    XrTime aClick{0};
    XrTime aTouch{0};
    XrTime bClick{0};
    XrTime bTouch{0};
    XrTime systemClick{0};
    XrTime thumbrest{0};
    XrTime active{0};
  };

  static int handIdx(Hand h) noexcept { return h == Hand::Left ? 0 : 1; }

  mutable std::mutex mutex_;
  InputSnapshot live_{};
  Times times_[2]{};
};

}  // namespace fuvr::runtime
