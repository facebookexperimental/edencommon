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

#include "eden/common/utils/UserInfo.h"

namespace facebook::eden {

struct SessionInfo {
  std::string username;
  std::string hostname;
  std::optional<uint64_t> ciInstanceId;
  std::string os;
  std::string osVersion;
  std::string appVersion;
  std::string crossEnvSessionId;
  std::string systemFingerprint;
#ifdef __APPLE__
  std::string systemArchitecture;
#endif
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

/**
 * Return the best guess of CI instance id from the environment,
 * or return empty if CI instance id is unknown.
 */
std::optional<uint64_t> getCiInstanceId();

/**
 * Returns the Cross Environment Session Id, which uniquely identifies the host.
 * This function returns an empty string if the Cross Environment Session Id is
 * not unknown.
 */
std::string getCrossEnvSessionId();

/*
 * Returns the system fingerprint (the top level digest of the system metadata)
 */
std::string getSystemFingerprint();

} // namespace facebook::eden
