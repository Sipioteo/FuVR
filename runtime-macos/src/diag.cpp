// SPDX-License-Identifier: Apache-2.0
#include "fuvr/internal/diag.hpp"

#include "fuvr/daemon_client.hpp"

namespace fuvr::runtime::diag {

EncoderStatsSnapshot encoderStatsForSession(XrSession session) noexcept {
  Session* s = lookupSession(session);
  if (s == nullptr) return EncoderStatsSnapshot{};
  return s->encoderStatsSnapshot();
}

InternalDiagState internalDiagState(XrSession session) noexcept {
  InternalDiagState out{};
  Session* s = lookupSession(session);
  if (s == nullptr) return out;
  out.daemonAlive = s->daemonAlive.load();
  out.reconnectCount = s->reconnectCount.load();
  if (s->daemon) out.reconnectAttempts = s->daemon->reconnectAttempts();
  return out;
}

}  // namespace fuvr::runtime::diag
