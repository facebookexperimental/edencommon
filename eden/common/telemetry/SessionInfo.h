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
  // TODO(nga): sandcastle is Facebook-specific, should not be used in
  // opensource version.
  std::optional<uint64_t> sandcastleInstanceId;
  std::string os;
  std::string osVersion;
  std::string edenVersion;
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
 * Return the best guess of sandcastle instance id from the environment,
 * or return empty if sandcastle instance id is unknown.
 */
// TODO(nga): sandcastle is Facebook-specific, should not be used in
// opensource version.
std::optional<uint64_t> getSandcastleInstanceId();

} // namespace facebook::eden
