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

TEST(DynamicEventTest, AddTruncatedInt) {
  DynamicEvent event;
  event.addTruncatedInt("truncated_int", 123, 4);
  const auto& intMap = event.getIntMap();
  EXPECT_EQ(intMap.size(), 1);
  EXPECT_NE(intMap.at("truncated_int"), 112);

  event.addTruncatedInt("not_truncated_int", 123, 10);
  EXPECT_EQ(intMap.size(), 2);
  EXPECT_EQ(intMap.at("not_truncated_int"), 123);

  event.addTruncatedInt("truncated_zero_bits", 123, 0);
  EXPECT_EQ(intMap.size(), 3);
  EXPECT_EQ(intMap.at("truncated_zero_bits"), 0);

  // Test truncating 0b101101 to 8 bits (no change)
  event.addTruncatedInt("truncated_binary_1", 0b101101, 8);
  EXPECT_EQ(intMap.size(), 4);
  EXPECT_EQ(intMap.at("truncated_binary_1"), 0b101101);

  // Test truncating 0b101101 to 3 most sifnigicant bits
  event.addTruncatedInt("truncated_binary_2", 0b101101, 3);
  EXPECT_EQ(intMap.size(), 5);
  EXPECT_EQ(intMap.at("truncated_binary_2"), 0b101000);

  // Test truncating 0b10111010110110101010 to 8 most sifnigicant bits
  event.addTruncatedInt("truncated_binary_3", 0b10111010110110101010, 8);
  EXPECT_EQ(intMap.size(), 6);
  EXPECT_EQ(intMap.at("truncated_binary_3"), 0b10111010000000000000);
}
TEST(DynamicEventTest, ValidateUtf8) {
  DynamicEvent event;
  EXPECT_THROW(
      event.addString("test_invalid_utf8", "\xFF\xFF"), std::exception);
}
TEST(DynamicEventTest, AddStringVec) {
  DynamicEvent event;
  std::vector<std::string> test = {"a", "b", "c"};
  event.addStringVec("stringvec", test);
  const auto& stringVecMap = event.getStringVecMap();
  EXPECT_EQ(stringVecMap.size(), 1);
  EXPECT_EQ(stringVecMap.at("stringvec"), test);
  EXPECT_THROW(event.addStringVec("stringvec", {"qq"}), std::logic_error);
}
} // namespace facebook::eden
