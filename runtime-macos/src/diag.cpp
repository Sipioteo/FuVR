// SPDX-License-Identifier: Apache-2.0
#include "fuvr/internal/diag.hpp"

namespace fuvr::runtime::diag {

EncoderStatsSnapshot encoderStatsForSession(XrSession session) noexcept {
  Session* s = lookupSession(session);
  if (s == nullptr) return EncoderStatsSnapshot{};
  return s->encoderStatsSnapshot();
}

}  // namespace fuvr::runtime::diag
