/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Bug.h"

#include <folly/ExceptionWrapper.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

using namespace facebook::eden;

namespace {
void buggyFunction() {
  EDEN_BUG() << "oh noes";
}
} // namespace

TEST(EdenBug, throws) {
  EdenBugDisabler noCrash;
  EXPECT_THROW_RE(buggyFunction(), std::runtime_error, "oh noes");
  EXPECT_THROW_RE(EDEN_BUG() << "doh", std::runtime_error, "doh");
}

TEST(EdenBug, toException) {
  EdenBugDisabler noCrash;
  auto ew = EDEN_BUG_EXCEPTION() << "whoops";
  EXPECT_THROW_RE(ew.throw_exception(), std::runtime_error, "whoops");
}
