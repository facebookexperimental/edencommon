/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcMountInfo.h"

#include <cerrno>

#include <folly/portability/GTest.h>

namespace facebook::eden {
namespace {

constexpr MountInfoOptions kAllFields{
    .includeMountSource = true,
    .includeMountOptions = true};

TEST(ProcMountInfoTest, parsesDevicesAndOwnersForMultipleUsers) {
  auto result = parseProcMountInfo(
      "101 1 0:344 / /data/users/gzuo/fbsource rw shared:2 - fuse edenfs: "
      "rw,user_id=229400,group_id=100,allow_other\n"
      "102 1 0:101 / /data/users/jerryliang/fbsource rw - fuse edenfs: "
      "rw,user_id=215776,group_id=100,allow_other\n",
      kAllFields);
  ASSERT_TRUE(result.hasValue());
  ASSERT_EQ(2, result->size());
  const auto& first = result->at(0);
  EXPECT_EQ(0, first.devMajor);
  EXPECT_EQ(344, first.devMinor);
  EXPECT_EQ("/", first.mountRoot);
  EXPECT_EQ("/data/users/gzuo/fbsource", first.mountPoint);
  EXPECT_EQ("fuse", first.fsType);
  EXPECT_EQ("edenfs:", first.mountSource);
  EXPECT_EQ("rw,user_id=229400,group_id=100,allow_other", first.mountOptions);
  const auto& second = result->at(1);
  EXPECT_EQ(101, second.devMinor);
  EXPECT_EQ("rw,user_id=215776,group_id=100,allow_other", second.mountOptions);
}

TEST(ProcMountInfoTest, decodesEscapedPathsAndSource) {
  constexpr auto contents =
      "101 1 8:2 /delegated\\040root /space\\040tab\\011newline\\012slash\\134040 rw - "
      "ext4 source\\040name rw,option=escaped\\054comma\n";
  auto result = parseProcMountInfo(contents, kAllFields);
  ASSERT_TRUE(result.hasValue());
  ASSERT_EQ(1, result->size());
  EXPECT_EQ("/space tab\tnewline\nslash\\040", result->at(0).mountPoint);
  EXPECT_EQ("/delegated root", result->at(0).mountRoot);
  EXPECT_EQ("source name", result->at(0).mountSource);
  EXPECT_EQ("rw,option=escaped\\054comma", result->at(0).mountOptions);

  auto basic = parseProcMountInfo(contents, {});
  ASSERT_TRUE(basic.hasValue());
  EXPECT_TRUE(basic->at(0).mountSource.empty());
  EXPECT_TRUE(basic->at(0).mountOptions.empty());
}

TEST(ProcMountInfoTest, rejectsIncompleteOrInvalidMountInfo) {
  for (const auto* contents : {
           "101 1 0:344 / /checkout rw fuse edenfs: rw",
           "101 1 0:344 / /checkout - fuse edenfs: rw",
           "101 1 0:344 / /checkout rw - fuse edenfs:",
           "101 1 0 / /checkout rw - fuse edenfs: rw",
           "101 1 x:344 / /checkout rw - fuse edenfs: rw",
           "101 1 0:4294967296 / /checkout rw - fuse edenfs: rw",
           "101 1 0:344 / /checkout\\ rw - fuse edenfs: rw",
       }) {
    auto result = parseProcMountInfo(contents, kAllFields);
    ASSERT_TRUE(result.hasError()) << contents;
    EXPECT_EQ(EINVAL, result.error());
  }
}

} // namespace
} // namespace facebook::eden
