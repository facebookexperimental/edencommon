/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include "eden/common/telemetry/StructuredLogger.h"

namespace facebook::eden {

class ScribeLogger;

class ScubaStructuredLogger : public StructuredLogger {
 public:
  ScubaStructuredLogger(
      std::shared_ptr<ScribeLogger> scribeLogger,
      SessionInfo sessionInfo);
  virtual ~ScubaStructuredLogger() override = default;

 protected:
  void logDynamicEvent(DynamicEvent event) override;

 private:
  std::shared_ptr<ScribeLogger> scribeLogger_;
};

} // namespace facebook::eden
