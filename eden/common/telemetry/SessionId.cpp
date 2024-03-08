/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/telemetry/SessionId.h"
#include <random>

namespace {

uint32_t generateSessionId() {
  std::random_device rd;
  std::uniform_int_distribution<uint32_t> u;
  return u(rd);
}

} // namespace

namespace facebook::eden {

uint32_t getSessionId() {
  static auto sessionId = generateSessionId();
  return sessionId;
}

} // namespace facebook::eden
