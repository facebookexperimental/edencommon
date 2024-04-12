/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "eden/common/telemetry/StatsGroup.h"

namespace facebook::eden {

struct TelemetryStats : StatsGroup<TelemetryStats> {
  Counter subprocessLoggerFailure{"telemetry.subprocess_logger_failure"};
};

} // namespace facebook::eden
