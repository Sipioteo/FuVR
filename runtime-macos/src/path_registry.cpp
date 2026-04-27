// SPDX-License-Identifier: Apache-2.0
#include "fuvr/path_registry.hpp"

namespace fuvr::runtime {

namespace {

constexpr const char* kSeedPaths[] = {
    "/user/hand/left",
    "/user/hand/right",
    "/user/head",
    "/user/gamepad",
    "/user/eyes_ext",
    "/interaction_profiles/oculus/touch_controller",
    "/interaction_profiles/oculus/touch_plus_controller",
    "/interaction_profiles/khr/simple_controller",
    "/interaction_profiles/ext/eye_gaze_interaction",
    "/user/hand/left/input/select/click",
    "/user/hand/right/input/select/click",
    "/user/hand/left/input/menu/click",
    "/user/hand/right/input/menu/click",
    "/user/hand/left/input/system/click",
    "/user/hand/right/input/system/click",
    "/user/hand/left/input/trigger/value",
    "/user/hand/right/invoke/trigger/value",
    "/user/hand/right/input/trigger/value",
    "/user/hand/left/input/trigger/click",
    "/user/hand/right/input/trigger/click",
    "/user/hand/left/input/trigger/touch",
    "/user/hand/right/input/trigger/touch",
    "/user/hand/left/input/squeeze/value",
    "/user/hand/right/input/squeeze/value",
    "/user/hand/left/input/squeeze/click",
    "/user/hand/right/input/squeeze/click",
    "/user/hand/left/input/thumbstick",
    "/user/hand/right/input/thumbstick",
    "/user/hand/left/input/thumbstick/x",
    "/user/hand/right/input/thumbstick/x",
    "/user/hand/left/input/thumbstick/y",
    "/user/hand/right/input/thumbstick/y",
    "/user/hand/left/input/thumbstick/click",
    "/user/hand/right/input/thumbstick/click",
    "/user/hand/left/input/thumbstick/touch",
    "/user/hand/right/input/thumbstick/touch",
    "/user/hand/left/input/thumbrest/touch",
    "/user/hand/right/input/thumbrest/touch",
    "/user/hand/left/input/a/click",
    "/user/hand/right/input/a/click",
    "/user/hand/left/input/a/touch",
    "/user/hand/right/input/a/touch",
    "/user/hand/left/input/b/click",
    "/user/hand/right/input/b/click",
    "/user/hand/left/input/b/touch",
    "/user/hand/right/input/b/touch",
    "/user/hand/left/input/x/click",
    "/user/hand/left/input/x/touch",
    "/user/hand/left/input/y/click",
    "/user/hand/left/input/y/touch",
    "/user/hand/left/input/grip/pose",
    "/user/hand/right/input/grip/pose",
    "/user/hand/left/input/aim/pose",
    "/user/hand/right/input/aim/pose",
    "/user/hand/left/output/haptic",
    "/user/hand/right/output/haptic",
    "/user/eyes_ext/input/gaze_ext/pose",
};

}  // namespace

PathRegistry::PathRegistry() {
  atomToString_.reserve(256);
  for (const char* s : kSeedPaths) {
    seed(s);
  }
}

void PathRegistry::seed(std::string_view s) {
  std::string key(s);
  atomToString_.push_back(key);
  XrPath atom = static_cast<XrPath>(atomToString_.size());
  stringToAtom_.emplace(std::move(key), atom);
}

XrPath PathRegistry::intern(std::string_view str) noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  std::string key(str);
  auto it = stringToAtom_.find(key);
  if (it != stringToAtom_.end()) return it->second;
  atomToString_.push_back(key);
  XrPath atom = static_cast<XrPath>(atomToString_.size());
  stringToAtom_.emplace(std::move(key), atom);
  return atom;
}

const std::string* PathRegistry::lookup(XrPath path) const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  if (path == 0 || path > atomToString_.size()) return nullptr;
  return &atomToString_[static_cast<std::size_t>(path) - 1];
}

std::size_t PathRegistry::size() const noexcept {
  std::lock_guard<std::mutex> lk(mutex_);
  return atomToString_.size();
}

PathRegistry& pathRegistry() noexcept {
  static PathRegistry r;
  return r;
}

}  // namespace fuvr::runtime
