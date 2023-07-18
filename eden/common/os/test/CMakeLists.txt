# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


add_executable(
  os_test
  ProcessIdTest.cpp
)

target_link_libraries(
  os_test
  PUBLIC
    edencommon_os
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(os_test)
