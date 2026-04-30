// SPDX-License-Identifier: Apache-2.0
#include "mock_state.hpp"

#include "log.hpp"

namespace fuvr::openvr_shim {

MockState& MockState::shared() {
  static MockState s;
  return s;
}

bool MockState::initialize(const std::string& appKey, uint32_t appType) {
  if (initialized_.load(std::memory_order_acquire)) return true;
  if (!rpc_.connect(appKey, appType, caps_)) return false;
  if (!xpc_.start()) {
    FUVR_LOG("xpc service unavailable — frame submission will be no-op");
  }
  initialized_.store(true, std::memory_order_release);
  return true;
}

void MockState::shutdown() {
  if (!initialized_.exchange(false)) return;
  rpc_.disconnect();
  xpc_.stop();
  std::lock_guard<std::mutex> lk(bridgeMu_);
  glBridge_.reset();
  metalBridge_.reset();
}

TextureBridge* MockState::getOrCreateBridge(uint32_t textureType) {
  std::lock_guard<std::mutex> lk(bridgeMu_);
  // textureType values from `vr::ETextureType`:
  //   0 = DirectX, 1 = OpenGL, 2 = Vulkan, 3 = IOSurface, 4 = DXGISharedHandle, 5 = Metal
  if (textureType == 1) {  // OpenGL
    if (!glBridge_) glBridge_ = makeGLBridge();
    return glBridge_.get();
  }
  if (textureType == 5 || textureType == 3) {  // Metal or IOSurface (forward)
    if (!metalBridge_) metalBridge_ = makeMetalBridge();
    return metalBridge_.get();
  }
  return nullptr;
}

uint64_t MockState::internActionHandle(const std::string& path) {
  std::lock_guard<std::mutex> lk(handleMu_);
  auto it = actionHandles_.find(path);
  if (it != actionHandles_.end()) return it->second;
  uint64_t h = nextActionHandle_++;
  actionHandles_.emplace(path, h);
  return h;
}

uint64_t MockState::internInputSourceHandle(const std::string& path) {
  std::lock_guard<std::mutex> lk(handleMu_);
  auto it = sourceHandles_.find(path);
  if (it != sourceHandles_.end()) return it->second;
  uint64_t h = (1ull << 32) | nextSourceHandle_++;  // disjoint from action handles
  sourceHandles_.emplace(path, h);
  return h;
}

}  // namespace fuvr::openvr_shim
