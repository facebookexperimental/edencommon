/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/telemetry/SubprocessScribeLogger.h"

#include <folly/Exception.h>
#include <folly/FileUtil.h>
#include <folly/portability/GTest.h>
#include <folly/testing/TestUtil.h>

using namespace facebook::eden;
using namespace folly::string_piece_literals;

TEST(ScribeLogger, log_messages_are_written_with_newlines) {
  folly::test::TemporaryFile output;

  {
    SubprocessScribeLogger logger{
        std::vector<std::string>{"/bin/cat"},
        FileDescriptor(
            ::dup(output.fd()), "dup", FileDescriptor::FDType::Generic)};
    logger.log("foo"_sp);
    logger.log("bar"_sp);
  }

  folly::checkUnixError(lseek(output.fd(), 0, SEEK_SET));
  std::string contents;
  folly::readFile(output.fd(), contents);
  EXPECT_EQ("foo\nbar\n", contents);
}
