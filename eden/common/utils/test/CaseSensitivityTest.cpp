/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/CaseSensitivity.h"

#include <folly/portability/GTest.h>

using namespace facebook::eden;

TEST(CaseSensitivityTest, formattingInsensitive) {
  EXPECT_EQ("Insensitive", fmt::to_string(CaseSensitivity::Insensitive));
}

TEST(CaseSensitivityTest, formattingSensitive) {
  EXPECT_EQ("Sensitive", fmt::to_string(CaseSensitivity::Sensitive));
}
