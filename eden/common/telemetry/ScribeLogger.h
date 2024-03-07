/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Range.h>
#include <string>

namespace facebook::eden {

/**
 * An interface to a scribe logger implementation.
 *
 * Subclasses must override either of the log overloads.
 *
 * Messages must not contain newlines. Messages are not durable. They may be
 * dropped under load or for other reasons.
 */
class ScribeLogger {
 public:
  virtual ~ScribeLogger() = default;
  virtual void log(folly::StringPiece message) {
    return log(message.str());
  }
  virtual void log(std::string message) {
    return log(folly::StringPiece{message});
  }
};

} // namespace facebook::eden
