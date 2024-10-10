/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/SysTypes.h>

#include "eden/common/telemetry/DynamicEvent.h"

namespace facebook::eden {

struct TypedEvent {
  virtual ~TypedEvent() = default;
  virtual void populate(DynamicEvent&) const = 0;
  virtual const char* getType() const = 0;
};

// Used for unit testing
struct TestEvent : public TypedEvent {
  // Keep populate() and getType() pure virtual to force subclasses
  // to implement them
  virtual void populate(DynamicEvent&) const override = 0;
  virtual const char* getType() const override = 0;
};

} // namespace facebook::eden
