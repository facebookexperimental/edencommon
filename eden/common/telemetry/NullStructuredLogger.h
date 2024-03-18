/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "eden/common/telemetry/StructuredLogger.h"

namespace facebook::eden {

class NullStructuredLogger final : public StructuredLogger {
 public:
  NullStructuredLogger() : StructuredLogger{false, SessionInfo{}} {}

 private:
  void logDynamicEvent(DynamicEvent) override {}
};

} // namespace facebook::eden
