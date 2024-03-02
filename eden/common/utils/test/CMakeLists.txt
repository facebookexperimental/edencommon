# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


add_executable(
  process_info_cache_test
    "ProcessInfoCacheTest.cpp"
)

target_link_libraries(
  process_info_cache_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(process_info_cache_test)

add_executable(
  string_conv_test
    "StringConvTest.cpp"
)

target_link_libraries(
  string_conv_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(string_conv_test)

add_executable(
  option_set_test
    "OptionSetTest.cpp"
)

target_link_libraries(
  option_set_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(option_set_test)

add_executable(
  throw_test
    "ThrowTest.cpp"
)

target_link_libraries(
  throw_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(throw_test)

add_executable(
  immediate_future_test
    "ImmediateFutureTest.cpp"
)

target_link_libraries(
  immediate_future_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(immediate_future_test)

add_executable(
  memory_test
    "MemoryTest.cpp"
)

target_link_libraries(
  memory_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(memory_test)

add_executable(
  string_test
    "StringTest.cpp"
)

target_link_libraries(
  string_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(string_test)

add_executable(
  utf8_test
    "Utf8Test.cpp"
)

target_link_libraries(
  utf8_test
  PUBLIC
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(utf8_test)

add_executable(
  file_utils_test
    "FileUtilsTest.cpp"
)

target_link_libraries(
  file_utils_test
  PUBLIC
    edencommon_testharness
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(file_utils_test)

add_executable(
  path_funcs_test
    "PathFuncsTest.cpp"
)

target_link_libraries(
  path_funcs_test
  PUBLIC
    edencommon_testharness
    edencommon_utils
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
)

gtest_discover_tests(path_funcs_test)
