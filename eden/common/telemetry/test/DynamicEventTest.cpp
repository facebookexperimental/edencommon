/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/telemetry/DynamicEvent.h"
#include <folly/portability/GTest.h>

namespace facebook::eden {

TEST(DynamicEventTest, AddInt) {
  DynamicEvent event;
  event.addInt("test_int", 123);
  const auto& intMap = event.getIntMap();
  EXPECT_EQ(intMap.size(), 1);
  EXPECT_EQ(intMap.at("test_int"), 123);
  // Attempting to add a duplicate key should throw an exception.
  EXPECT_THROW(event.addInt("test_int", 456), std::logic_error);
}
TEST(DynamicEventTest, AddString) {
  DynamicEvent event;
  event.addString("test_string", "hello");
  const auto& stringMap = event.getStringMap();
  EXPECT_EQ(stringMap.size(), 1);
  EXPECT_EQ(stringMap.at("test_string"), "hello");
  // Attempting to add a duplicate key should throw an exception.
  EXPECT_THROW(event.addString("test_string", "world"), std::logic_error);
}
TEST(DynamicEventTest, AddDouble) {
  DynamicEvent event;
  event.addDouble("test_double", 3.14);
  const auto& doubleMap = event.getDoubleMap();
  EXPECT_EQ(doubleMap.size(), 1);
  EXPECT_DOUBLE_EQ(doubleMap.at("test_double"), 3.14);
  // Attempting to add a duplicate key should throw an exception.
  EXPECT_THROW(event.addDouble("test_double", 2.71), std::logic_error);
}
TEST(DynamicEventTest, AddBool) {
  DynamicEvent event;
  event.addBool("test_bool", true);
  const auto& intMap = event.getIntMap();
  EXPECT_EQ(intMap.size(), 1);
  EXPECT_EQ(intMap.at("test_bool"), 1);
  event.addBool("test_bool_false", false);
  EXPECT_EQ(intMap.size(), 2);
  EXPECT_EQ(intMap.at("test_bool_false"), 0);
}
TEST(DynamicEventTest, ValidateUtf8) {
  DynamicEvent event;
  EXPECT_THROW(
      event.addString("test_invalid_utf8", "\xFF\xFF"), std::exception);
}
} // namespace facebook::eden
