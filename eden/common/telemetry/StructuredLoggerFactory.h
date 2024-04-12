/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/logging/xlog.h>
#include <memory>
#include <string>

#include "eden/common/telemetry/NullStructuredLogger.h"
#include "eden/common/telemetry/ScubaStructuredLogger.h"
#include "eden/common/telemetry/Stats.h"
#include "eden/common/telemetry/SubprocessScribeLogger.h"
#include "eden/common/utils/RefPtr.h"

namespace facebook::eden {

class StructuredLogger;
struct SessionInfo;

/**
 * Returns a StructuredLogger appropriate for this platform and
 * configuration.
 */
template <typename StatsPtr>
std::shared_ptr<StructuredLogger> makeDefaultStructuredLogger(
    const std::string& binary,
    const std::string& category,
    SessionInfo sessionInfo,
    StatsPtr stats) {
  if (binary.empty()) {
    return std::make_shared<NullStructuredLogger>();
  }

  if (category.empty()) {
    XLOGF(
        WARN,
        "Scribe binary '{}' specified, but no category specified. Structured logging is disabled.",
        binary);
    return std::make_shared<NullStructuredLogger>();
  }

  try {
    auto logger =
        std::make_unique<SubprocessScribeLogger>(binary.c_str(), category);
    return std::make_shared<ScubaStructuredLogger>(
        std::move(logger), std::move(sessionInfo));
  } catch (const std::exception& ex) {
    stats->increment(&TelemetryStats::subprocessLoggerFailure, 1);
    XLOGF(
        ERR,
        "Failed to create SubprocessScribeLogger: {}. Structured logging is disabled.",
        folly::exceptionStr(ex));
    return std::make_shared<NullStructuredLogger>();
  }
}

} // namespace facebook::eden
