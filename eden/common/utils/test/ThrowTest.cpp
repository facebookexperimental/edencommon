/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Throw.h"
#include <folly/portability/GTest.h>

namespace {
using namespace facebook::eden;

// fmt 9 fixes a bug that allows this to compile.
// available everywhere.
#if FMT_VERSION >= 90000

TEST(ThrowTest, throw__takes_fmt_views) {
  std::vector<std::string_view> v = {"world"};
  try {
    throw_<std::runtime_error>("hello ", fmt::join(v, ", "));
    FAIL();
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ("hello world", e.what());
  }
}

#endif

TEST(ThrowTest, throwf_takes_fmt_views) {
  std::vector<std::string_view> v = {"world"};
  try {
    throwf<std::runtime_error>("hello {}", fmt::join(v, ", "));
    FAIL();
  } catch (const std::runtime_error& e) {
    EXPECT_STREQ("hello world", e.what());
  }
}

} // namespace
