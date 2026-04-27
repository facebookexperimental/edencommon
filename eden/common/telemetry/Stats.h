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
  Counter xplatMessagesEnqueued{"telemetry.xplat_messages_enqueued"};
  Counter xplatMessagesWritten{"telemetry.xplat_messages_written"};
  Counter xplatMessagesDroppedQueueFull{
      "telemetry.xplat_messages_dropped_queue_full"};
  Counter xplatMessagesDroppedShutdown{
      "telemetry.xplat_messages_dropped_shutdown"};
  Counter xplatWriteZeroOkRecords{"telemetry.xplat_write_zero_ok_records"};
  Counter xplatWriteFailures{"telemetry.xplat_write_failures"};
};

} // namespace facebook::eden
