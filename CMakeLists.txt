# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.


# Cmake Project setup
cmake_minimum_required(VERSION 3.8)
set_property(GLOBAL PROPERTY GLOBAL_DEPENDS_NO_CYCLES ON)

project("edencommon" LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (WIN32)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DSTRICT")
endif()

# Setup directories for build and install
set(INCLUDE_INSTALL_DIR include CACHE STRING
    "The subdirectory where header files should be installed")
set(LIB_INSTALL_DIR lib CACHE STRING
    "The subdirectory where libraries should be installed")
set(CMAKE_INSTALL_DIR lib/cmake/edencommon CACHE STRING
    "The subdirectory where CMake package config files should be installed")


# CMake include directories
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
  # For shipit-transformed builds
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

include(FBCompilerSettings)

# Setup all the CMake include directories so we can access all the
# dependenciew we need.
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake"
  "${CMAKE_CURRENT_SOURCE_DIR}/build/fbcode_builder/CMake"
  ${CMAKE_MODULE_PATH})

find_package(Glog MODULE REQUIRED)
include_directories(${GLOG_INCLUDE_DIR})

find_package(Gflags REQUIRED)
include_directories(${GFLAGS_INCLUDE_DIR})

find_package(folly CONFIG REQUIRED)
include_directories(${FOLLY_INCLUDE_DIR})

find_package(GMock MODULE REQUIRED)
include_directories(${GMOCK_INCLUDEDIR} ${LIBGMOCK_INCLUDE_DIR})
include(GoogleTest)
enable_testing()

add_subdirectory(eden/common/os)
add_subdirectory(eden/common/utils)

# Install our own CMake package files for dependent projects.
include(CMakePackageConfigHelpers)
configure_package_config_file(
  CMake/edencommon-config.cmake.in
  edencommon-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_DIR}
  PATH_VARS
    CMAKE_INSTALL_DIR
)
install(
  FILES ${CMAKE_CURRENT_BINARY_DIR}/edencommon-config.cmake
  DESTINATION ${CMAKE_INSTALL_DIR}
)

install(
  EXPORT edencommon-exports
  FILE edencommon-targets.cmake
  NAMESPACE edencommon::
  DESTINATION ${CMAKE_INSTALL_DIR}
)
