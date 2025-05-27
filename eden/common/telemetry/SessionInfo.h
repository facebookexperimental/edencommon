/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdint>
#include <string>

#include "eden/common/utils/UserInfo.h"

namespace facebook::eden {

struct SessionInfo {
  std::string username;
  std::string hostname;
  std::string os;
  std::string osVersion;
  std::string appVersion;
#ifdef __APPLE__
  std::string systemArchitecture;
#endif
  std::unordered_map<std::string, std::variant<std::string, uint64_t>> fbInfo;
};

SessionInfo makeSessionInfo(
    const UserInfo& userInfo,
    std::string hostname,
    std::string edenVersion);

std::string getOperatingSystemName();
std::string getOperatingSystemVersion();
#if defined(__APPLE__)
std::string getOperatingSystemArchitecture();
#endif

/**
 * Returns the result of calling gethostname() in a std::string. Throws an
 * exception on failure.
 */
std::string getHostname();
} // namespace facebook::eden
