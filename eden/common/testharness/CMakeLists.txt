# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

file(GLOB testharness_headers CONFIGURE_DEPENDS *.h)
file(GLOB testharness_sources CONFIGURE_DEPENDS *.cpp)

add_library(
  edencommon_testharness
  STATIC
  ${testharness_headers}
  ${testharness_sources}
)

target_link_libraries(
  edencommon_testharness
  PUBLIC
    edencommon_utils
    ${BOOST_LIBRARIES}
    Folly::folly_test_util
    ${LIBGMOCK_LIBRARIES}
    ${GLOG_LIBRARY}
)

target_include_directories(
  edencommon_testharness
  SYSTEM
  PUBLIC
    ${LIBGMOCK_INCLUDE_DIR}
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

install(
  TARGETS edencommon_testharness
  EXPORT edencommon-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
)

install(
  FILES ${testharness_headers}
  DESTINATION ${INCLUDE_INSTALL_DIR}/eden/common/testharness
)
