/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/os/ProcessId.h"

#include <map>
#include <unordered_map>

#include <fmt/format.h>
#include <folly/portability/GTest.h>

namespace {
using namespace facebook::eden;

TEST(OptionalProcessId, default_constructor_is_empty) {
  OptionalProcessId pid;
  EXPECT_FALSE(pid);
}

TEST(OptionalProcessId, conversion_from_zero_is_safe) {
  OptionalProcessId pid = ProcessId{0};
  EXPECT_TRUE(pid);
}

TEST(OptionalProcessId, value_throws_bad_optional_access) {
  OptionalProcessId pid;
  EXPECT_THROW(pid.value(), std::bad_optional_access);
}

#ifdef _WIN32

TEST(ProcessId, conversion_from_large_numbers_is_safe_on_windows) {
  // Raymond Chen says he's seen process IDs in the four billions.
  // https://stackoverflow.com/questions/17868218/what-is-the-maximum-process-id-on-windows
  OptionalProcessId pid = ProcessId{~uint32_t{0} - 4};
  EXPECT_TRUE(pid);
}

#else

TEST(ProcessId, negative_process_IDs_are_disallowed_on_unix) {
  // pid_t is signed, but only to represent error results from functions.
  EXPECT_THROW(ProcessId{uint32_t(-2)}, InvalidProcessId);
}

#endif

TEST(ProcessId, zero_complement_is_invalid_process_id) {
  EXPECT_THROW(ProcessId{~uint32_t{}}, InvalidProcessId);
}

TEST(ProcessId, can_be_key_in_map) {
  std::map<ProcessId, std::string> map;
  map[ProcessId{10}] = "10";
  map[ProcessId{11}] = "11";
  EXPECT_EQ(2, map.size());
}

TEST(ProcessId, can_be_key_in_unordered_map) {
  std::unordered_map<ProcessId, std::string> map;
  map[ProcessId{10}] = "10";
  map[ProcessId{11}] = "11";
  EXPECT_EQ(2, map.size());
}

TEST(OptionalProcessId, can_be_key_in_map) {
  std::map<OptionalProcessId, std::string> map;
  map[ProcessId{10}] = "10";
  map[ProcessId{11}] = "11";
  EXPECT_EQ(2, map.size());
}

TEST(OptionalProcessId, can_be_key_in_unordered_map) {
  std::unordered_map<OptionalProcessId, std::string> map;
  map[ProcessId{10}] = "10";
  map[ProcessId{11}] = "11";
  EXPECT_EQ(2, map.size());
}

TEST(OptionalProcessId, ostream_format_empty) {
  std::ostringstream os;
  os << OptionalProcessId{};
  EXPECT_EQ("-1", os.str());
}

TEST(OptionalProcessId, ostream_format) {
  std::ostringstream os;
  os << OptionalProcessId{ProcessId{1000}};
  EXPECT_EQ("1000", os.str());
}

TEST(OptionalProcessId, fmt_format) {
  EXPECT_EQ("0", fmt::to_string(ProcessId{0}));
  EXPECT_EQ("1000", fmt::to_string(ProcessId{1000}));

  EXPECT_EQ("-1", fmt::to_string(OptionalProcessId{}));
  EXPECT_EQ("1000", fmt::to_string(OptionalProcessId{ProcessId{1000}}));
}

} // namespace
