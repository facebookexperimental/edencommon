# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


# We use a conglomerate utils target because external dependents will have to
# build the whole eden package, even if they only need one piece from utils.
# Since there is not really a notion of partial builds, we just use one target
# for all of the utility libraries for edencommon because it makes things a lot
# easier.

set(utils_headers "Synchronized.h")

add_library(utils INTERFACE ${utils_headers})

target_link_libraries(utils
  INTERFACE
    Folly::folly
)

target_include_directories(utils INTERFACE
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

install(
  TARGETS utils
  EXPORT edencommon-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
)

install(
  FILES ${utils_headers}
  DESTINATION ${INCLUDE_INSTALL_DIR}
)
