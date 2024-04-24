/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include "eden/common/telemetry/DynamicEvent.h"
#include "eden/common/telemetry/SessionInfo.h"

namespace facebook::eden {

class StructuredLogger {
 public:
  explicit StructuredLogger(bool enabled, SessionInfo sessionInfo);
  virtual ~StructuredLogger() = default;

  template <typename Event>
  void logEvent(const Event& event) {
    // Avoid a bunch of work if it's going to be thrown away by the
    // logDynamicEvent implementation.
    if (!enabled_) {
      return;
    }

    // constexpr to ensure that the type field on the Event struct is constexpr
    // too.
    constexpr const char* type = Event::type;

    // TODO: consider moving the event to another thread and populating the
    // default fields there to reduce latency at the call site.
    DynamicEvent de{populateDefaultFields(type)};
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
