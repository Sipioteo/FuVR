// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <openxr/openxr.h>

#include "fuvr/pose_predictor.hpp"

namespace fuvr::runtime {

struct Session;
struct Action;

enum class SpaceKind : uint8_t {
  ReferenceView = 0,
  ReferenceLocal = 1,
  ReferenceStage = 2,
  Action = 3,
};

struct Space {
  XrSpace handle{XR_NULL_HANDLE};
  Session* session{nullptr};
  SpaceKind kind{SpaceKind::ReferenceLocal};
  Pose poseInRef{};
  Action* action{nullptr};
  XrPath subactionPath{XR_NULL_PATH};
};

Space* lookupSpace(XrSpace handle) noexcept;

}  // namespace fuvr::runtime
