/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef _WIN32

#include "eden/common/utils/XAttr.h"

#include <algorithm>
#include <cerrno>
#include <system_error>

#include <folly/portability/GTest.h>
#include <folly/testing/TestUtil.h>

using namespace facebook::eden;

TEST(XAttrTest, listxattrOfFileWithoutXattrsIsEmpty) {
  folly::test::TemporaryFile file;
  auto path = file.path().string();
  if (::listxattr(
          path.c_str(),
          nullptr,
          0
#ifdef __APPLE__
          ,
          XATTR_NOFOLLOW
#endif
          ) != 0) {
    GTEST_SKIP() << "the temporary directory adds xattrs to new files";
  }

  EXPECT_EQ(std::vector<std::string>{}, listxattr(path));
}

TEST(XAttrTest, listxattrRetriesWhenNamesExceedInitialBuffer) {
  folly::test::TemporaryFile file;
  // Together these names are longer than the 128 bytes listxattr reads on its
  // first attempt.
  const std::string prefix = "user.eden_xattr_test_name_";
  constexpr int kNumNames = 8;
  std::vector<std::string> names;
  names.reserve(kNumNames);
  for (int i = 0; i < kNumNames; ++i) {
    names.push_back(prefix + std::to_string(i));
  }
  try {
    for (const auto& name : names) {
      fsetxattr(file.fd(), name, "v");
    }
  } catch (const std::system_error& ex) {
    if (ex.code().value() == ENOTSUP || ex.code().value() == EOPNOTSUPP) {
      GTEST_SKIP() << "the temporary directory does not support user xattrs";
    }
    throw;
  }

  std::vector<std::string> listed;
  for (auto& name : listxattr(file.path().string())) {
    if (name.starts_with(prefix)) {
      listed.push_back(std::move(name));
    }
  }
  std::sort(listed.begin(), listed.end());
  EXPECT_EQ(names, listed);
}

#endif
