# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

file(GLOB telemetry_headers CONFIGURE_DEPENDS *.h)
file(GLOB telemetry_sources CONFIGURE_DEPENDS *.cpp)

add_library(
  edencommon_telemetry
  ${telemetry_headers}
  ${telemetry_sources})

target_link_libraries(
  edencommon_telemetry
  PUBLIC
    edencommon_utils
    FBThrift::thriftcpp2
    fb303::fb303
    Folly::folly
)

target_include_directories(
  edencommon_telemetry
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

install(
  TARGETS edencommon_telemetry
  EXPORT edencommon-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
)

install(
  FILES ${telemetry_headers}
  DESTINATION ${INCLUDE_INSTALL_DIR}/eden/common/telemetry
)
