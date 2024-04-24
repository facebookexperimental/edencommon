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

DynamicEvent StructuredLogger::populateDefaultFields(const char* type) {
  DynamicEvent event;
  if (kExplicitTimeField) {
    event.addInt("time", ::time(nullptr));
  }
  event.addInt("session_id", sessionId_);
  event.addString("type", type);
  event.addString("user", sessionInfo_.username);
  event.addString("host", sessionInfo_.hostname);
  if (sessionInfo_.ciInstanceId.has_value()) {
    event.addInt("sandcastle_instance_id", *sessionInfo_.ciInstanceId);
  }
  event.addString("os", sessionInfo_.os);
  event.addString("osver", sessionInfo_.osVersion);
  event.addString("edenver", sessionInfo_.appVersion);
#if defined(__APPLE__)
  event.addString("system_architecture", sessionInfo_.systemArchitecture);
#endif
  event.addString("logged_by", "edenfs");
  return event;
}

} // namespace facebook::eden
