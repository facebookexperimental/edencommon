/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include "eden/common/telemetry/DynamicEvent.h"
#include "eden/common/telemetry/LogEvent.h"
#include "eden/common/telemetry/SessionInfo.h"

namespace facebook::eden {

class StructuredLogger {
 public:
  explicit StructuredLogger(bool enabled, SessionInfo sessionInfo);
  virtual ~StructuredLogger() = default;

  void logEvent(const TypedEvent& event) {
    // Avoid a bunch of work if it's going to be thrown away by the
    // logDynamicEvent implementation.
    if (!enabled_) {
      return;
    }

    DynamicEvent de{populateDefaultFields(event.getType())};
    event.populate(de);
    logDynamicEvent(std::move(de));
  }

 protected:
  virtual void logDynamicEvent(DynamicEvent event) = 0;

  virtual DynamicEvent populateDefaultFields(const char* type);

  bool enabled_;
  uint32_t sessionId_;
  SessionInfo sessionInfo_;
};

} // namespace facebook::eden
