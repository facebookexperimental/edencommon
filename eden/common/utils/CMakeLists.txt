# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


# We use a conglomerate utils target because external dependents will have to
# build the whole eden package, even if they only need one piece from utils.
# Since there is not really a notion of partial builds, we just use one target
# for all of the utility libraries for edencommon because it makes things a lot
# easier.

file(GLOB utils_headers CONFIGURE_DEPENDS *.h)
file(GLOB utils_sources CONFIGURE_DEPENDS *.cpp)

if (WIN32)
  list(
    REMOVE_ITEM utils_headers
    ${CMAKE_CURRENT_SOURCE_DIR}/XAttr.h
  )
  list(
    REMOVE_ITEM utils_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/XAttr.cpp
  )
endif()

add_library(
  edencommon_utils
    ${utils_headers}
    ${utils_sources})

target_link_libraries(
  edencommon_utils
  PUBLIC
    Folly::folly
    glog::glog
)

if (WIN32)
  target_link_libraries(
    edencommon_utils
    PUBLIC
      Userenv.lib
      Rpcrt4.lib
      dbghelp.lib
  )
endif()

target_include_directories(
  edencommon_utils
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${INCLUDE_INSTALL_DIR}>
)

install(
  TARGETS edencommon_utils
  EXPORT edencommon-exports
  LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
  ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
)

install(
  FILES ${utils_headers}
  DESTINATION ${INCLUDE_INSTALL_DIR}/eden/common/utils
)

add_subdirectory(test)
