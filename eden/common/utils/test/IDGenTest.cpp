/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/IDGen.h"
#include <folly/portability/GTest.h>

using namespace facebook::eden;

TEST(IDGenTest, initialIDIsNonZero) {
  EXPECT_NE(0, generateUniqueID());
}

TEST(IDGenTest, producesUniqueIDs) {
  auto id1 = generateUniqueID();
  auto id2 = generateUniqueID();
  auto id3 = generateUniqueID();
  EXPECT_NE(0, id1);
  EXPECT_NE(id1, id2);
  EXPECT_NE(id2, id3);
  EXPECT_NE(id2, id3);
}

TEST(IDGenTest, monotonic) {
  auto previous = generateUniqueID();
  for (int i = 0; i < 100000; ++i) {
    auto next = generateUniqueID();
    EXPECT_EQ(previous + 1, next);
    previous = next;
  }
}
