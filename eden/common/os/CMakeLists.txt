# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

file(GLOB os_headers CONFIGURE_DEPENDS *.h)
file(GLOB os_sources CONFIGURE_DEPENDS *.cpp)

add_library(edencommon_os
  ${os_headers}
  ${os_sources}
)

set(os_libs
  fmt::fmt
)

if (NOT WIN32)
  list(APPEND os_libs
    Folly::folly
  )
endif()

target_link_libraries(edencommon_os
  ${os_libs}
)

target_include_directories(edencommon_os PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

install(
  TARGETS edencommon_os
  EXPORT edencommon-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
)

install(
  FILES ${os_headers}
  DESTINATION ${INCLUDE_INSTALL_DIR}/eden/common/os
)

add_subdirectory(test)
