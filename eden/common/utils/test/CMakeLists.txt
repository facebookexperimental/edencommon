# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


add_executable(
  string_conv_test
    "StringConvTest.cpp"
)

target_link_libraries(
  string_conv_test
  PUBLIC
    utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(string_conv_test)
