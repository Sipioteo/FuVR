// SPDX-License-Identifier: Apache-2.0
#include "fuvr/inprocess_surface_registry.hpp"

#import <CoreFoundation/CoreFoundation.h>
#import <IOSurface/IOSurface.h>

#include <mutex>
#include <unordered_map>

namespace fuvr {

namespace {

struct Storage {
  std::mutex mu;
  std::unordered_map<uint64_t, IOSurfaceRef> map;
};

Storage& storage() {
  static Storage s;
  return s;
}

}  // namespace

InProcessSurfaceRegistry& InProcessSurfaceRegistry::shared() {
  static InProcessSurfaceRegistry inst;
  return inst;
}

void InProcessSurfaceRegistry::put(uint64_t token, IOSurfaceRef surface) {
  if (surface == nullptr) return;
  Storage& s = storage();
  std::lock_guard<std::mutex> lk(s.mu);
  auto it = s.map.find(token);
  if (it != s.map.end()) {
    CFRelease(it->second);
    it->second = static_cast<IOSurfaceRef>(const_cast<void*>(CFRetain(surface)));
    return;
  }
  s.map.emplace(token, static_cast<IOSurfaceRef>(const_cast<void*>(CFRetain(surface))));
}

IOSurfaceRef InProcessSurfaceRegistry::take(uint64_t token) {
  Storage& s = storage();
  std::lock_guard<std::mutex> lk(s.mu);
  auto it = s.map.find(token);
  if (it == s.map.end()) return nullptr;
  IOSurfaceRef out = it->second;
  s.map.erase(it);
  return out;
}

}  // namespace fuvr
