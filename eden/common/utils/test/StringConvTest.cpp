/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifdef _WIN32

#include "eden/common/utils/StringConv.h"
#include <folly/portability/GTest.h>
#include <string>

using namespace facebook::eden;

TEST(StringConvTest, multibyteToWideString) {
  EXPECT_EQ(L"", multibyteToWideString(""));
  EXPECT_EQ(L"foobar", multibyteToWideString("foobar"));
  EXPECT_EQ(
      L"\u0138\u00F9\u0150\U00029136",
      multibyteToWideString(u8"\u0138\u00F9\u0150\U00029136"));
}

TEST(StringConvTest, wideToMultibyteString) {
  EXPECT_EQ(wideToMultibyteString<std::string>(L""), "");
  EXPECT_EQ(wideToMultibyteString<std::string>(L"foobar"), "foobar");
  EXPECT_EQ(
      wideToMultibyteString<std::string>(L"\u0138\u00F9\u0150\U00029136"),
      u8"\u0138\u00F9\u0150\U00029136");
}

#endif
