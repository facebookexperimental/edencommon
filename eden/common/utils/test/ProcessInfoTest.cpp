/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessInfo.h"

#include <folly/portability/GTest.h>
#include <folly/portability/Unistd.h>
#include <sys/types.h>

namespace facebook::eden {

class ProcessInfoTest : public ::testing::Test {};
#ifndef _WIN32
#ifndef __APPLE__

TEST_F(ProcessInfoTest, readUserInfoForCurrentProcess) {
  // Test reading user info for the current process
  auto config = ReadUserInfoConfig{.resolveRootUser = true};
  auto userInfo = readUserInfo(getpid(), config);

  EXPECT_TRUE(userInfo.has_value());
  // Verify that the UID matches the current user's UID
  EXPECT_EQ(userInfo->ruid, getuid());
  EXPECT_EQ(userInfo->euid, getuid());

  // For a normal user process, sudoUser should be nullopt
  auto username = getlogin();
  if (username != nullptr) {
    EXPECT_EQ(userInfo->getRealUsername(), username);
  }
  if (getuid() == 0) {
    EXPECT_NE(userInfo->ruid, userInfo->euid);
  } else {
    EXPECT_EQ(userInfo->ruid, userInfo->euid);
  }
}

TEST_F(ProcessInfoTest, readUserInfoForNonExistentProcess) {
  // Use a very large PID that's unlikely to exist
  pid_t nonExistentPid = 999999999;

  // When reading a non-existent process, the function should return
  // a UserInfo with default values
  auto userInfo = readUserInfo(nonExistentPid, ReadUserInfoConfig());

  EXPECT_FALSE(userInfo.has_value());
}

TEST_F(ProcessInfoTest, testUidToUsername) {
  auto username = getlogin();
  if (username != nullptr) {
    EXPECT_EQ(ProcessUserInfo::uidToUsername(getuid()), username);
  }
}

#endif
#endif

} // namespace facebook::eden
