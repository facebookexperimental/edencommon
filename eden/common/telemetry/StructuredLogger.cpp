/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/telemetry/StructuredLogger.h"

#include "eden/common/telemetry/SessionId.h"

#include <ctime>

namespace {
/**
 * The log database populates the time field automatically.
 */
constexpr bool kExplicitTimeField = true;
} // namespace

namespace facebook::eden {

StructuredLogger::StructuredLogger(bool enabled, SessionInfo sessionInfo)
    : enabled_{enabled},
      sessionId_{getSessionId()},
      sessionInfo_{std::move(sessionInfo)} {}

DynamicEvent StructuredLogger::populateDefaultFields(
    std::optional<const char*> type) {
  DynamicEvent event;
  if (kExplicitTimeField) {
    event.addInt("time", ::time(nullptr));
  }
  event.addInt("session_id", sessionId_);
  if (type.has_value()) {
    event.addString("type", *type);
  }
  event.addString("user", sessionInfo_.username);
  event.addString("host", sessionInfo_.hostname);
  event.addString("os", sessionInfo_.os);
  event.addString("osver", sessionInfo_.osVersion);
#if defined(__APPLE__)
  event.addString("system_architecture", sessionInfo_.systemArchitecture);
#endif
  return event;
}

} // namespace facebook::eden
