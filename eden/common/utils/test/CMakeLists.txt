# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

add_executable(
  utils_test
    FileDescriptorTest.cpp
    FileUtilsTest.cpp
    OptionSetTest.cpp
    ImmediateFutureTest.cpp
    IoFutureTest.cpp
    MemoryTest.cpp
    PathFuncsTest.cpp
    ProcessInfoCacheTest.cpp
    RefPtrTest.cpp
    ScopedEnvVar.cpp
    SpawnedProcessTest.cpp
    StringConvTest.cpp
    StringTest.cpp
    ThrowTest.cpp
    UnixSocketTest.cpp
    UserInfoTest.cpp
    Utf8Test.cpp
)

target_link_libraries(
  utils_test
  PUBLIC
    edencommon_utils
    edencommon_testharness
    Folly::folly
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(utils_test)
