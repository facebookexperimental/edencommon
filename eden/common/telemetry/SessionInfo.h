/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace facebook::eden {

struct SessionInfo {
  std::string username;
  std::string hostname;
  std::optional<uint64_t> ciInstanceId;
  std::string os;
  std::string osVersion;
  std::string appVersion;
#ifdef __APPLE__
  std::string systemArchitecture;
#endif
};

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

/**
 * Return the best guess of CI instance id from the environment,
 * or return empty if CI instance id is unknown.
 */
std::optional<uint64_t> getCiInstanceId();

} // namespace facebook::eden
