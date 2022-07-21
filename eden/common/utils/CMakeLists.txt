# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


# We use a conglomerate utils target because external dependents will have to
# build the whole eden package, even if they only need one piece from utils.
# Since there is not really a notion of partial builds, we just use one target
# for all of the utility libraries for edencommon because it makes things a lot
# easier.

set(utils_headers
  ${CMAKE_CURRENT_SOURCE_DIR}/Synchronized.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Handle.h
  ${CMAKE_CURRENT_SOURCE_DIR}/WinError.h
  ${CMAKE_CURRENT_SOURCE_DIR}/StringConv.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ProcessName.h
  ${CMAKE_CURRENT_SOURCE_DIR}/ProcessNameCache.h
  ${CMAKE_CURRENT_SOURCE_DIR}/OptionSet.h
)

set(utils_srcs
  ${CMAKE_CURRENT_SOURCE_DIR}/WinError.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/StringConv.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ProcessName.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ProcessNameCache.cpp
)

add_library(utils ${utils_headers} ${utils_srcs})

target_link_libraries(utils
    Folly::folly
    glog::glog
)

target_include_directories(utils PUBLIC
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
  DESTINATION ${INCLUDE_INSTALL_DIR}/eden/common/utils
)

add_subdirectory(test)
