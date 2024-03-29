/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/testharness/TempFile.h"

#include <folly/logging/xlog.h>
#include <folly/portability/GTest.h>

using namespace facebook::eden;

TEST(TempFile, mktemp) {
  // This mainly just verifies that makeTempFile() and makeTempDir() succeeds
  auto tempfile = makeTempFile();
  XLOG(INFO) << "temporary file is " << tempfile.path();
  auto tempdir = makeTempDir();
  XLOG(INFO) << "temporary dir is " << tempfile.path();
}
