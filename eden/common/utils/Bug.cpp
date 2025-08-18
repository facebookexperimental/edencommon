/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Bug.h"

#include <folly/ExceptionWrapper.h>
#include <folly/logging/xlog.h>

namespace {
static std::atomic<int> edenBugDisabledCount{0};
}

namespace facebook::eden {
EdenBug::EdenBug(const char* file, int lineNumber)
    : file_(file), lineNumber_(lineNumber), message_("!!BUG!! ") {}

EdenBug::EdenBug(EdenBug&& other) noexcept
    : file_(other.file_),
      lineNumber_(other.lineNumber_),
      message_(std::move(other.message_)) {
  other.processed_ = true;
}

EdenBug::~EdenBug() {
  XCHECK(processed_);
}

folly::exception_wrapper EdenBug::toException() {
  logError();
  processed_ = true;
  return folly::exception_wrapper(std::runtime_error(message_));
}

void EdenBug::throwException() {
  toException().throw_exception();
}

void EdenBug::logError() {
  XLOGF(CRITICAL, "EDEN_BUG at {}:{}: {}", file_, lineNumber_, message_);

#ifndef NDEBUG
  // Crash in debug builds.
  // However, allow test code to disable crashing so that we can exercise
  // EDEN_BUG() code paths in tests.
  if (edenBugDisabledCount.load() == 0) {
    XLOG(FATAL, "crashing due to EDEN_BUG");
  }
#endif
}

EdenBugDisabler::EdenBugDisabler() {
  ++edenBugDisabledCount;
}

EdenBugDisabler::~EdenBugDisabler() {
  --edenBugDisabledCount;
}
} // namespace facebook::eden
