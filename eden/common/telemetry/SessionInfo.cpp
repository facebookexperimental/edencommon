/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/telemetry/SessionInfo.h"
#include <folly/Exception.h>
#include "eden/common/eden-config.h"
#ifdef LOGGER_FB_SESSION_INFO
#include "eden/common/telemetry/facebook/FacebookSessionInfo.h"
#endif

#if defined(__linux__) || defined(__APPLE__)
#include <folly/portability/Unistd.h>
#include <sys/utsname.h>
#endif

#if defined(_WIN32)
#include <winsock.h> // @manual
#endif

#include <cstdlib>
#include "eden/common/utils/SysctlUtil.h"

namespace {
/**
 * Windows limits hostnames to 256 bytes. Linux provides HOST_NAME_MAX
 * and MAXHOSTNAMELEN constants, defined as 64. Both Linux and macOS
 * define _POSIX_HOST_NAME_MAX as 256.  Both Linux and macOS allow
 * reading the host name limit at runtime with
 * sysconf(_SC_HOST_NAME_MAX).
 *
 * RFC 1034 limits complete domain names to 255:
 * https://tools.ietf.org/html/rfc1034#section-3.1
 * > To simplify implementations, the total number of octets that represent a
 * > domain name (i.e., the sum of all label octets and label lengths) is
 * > limited to 255.
 *
 * Rather than querying dynamically or selecting a constant based on platform,
 * assume 256 is sufficient everywhere.
 */
constexpr size_t kHostNameMax = 256;
} // namespace

namespace facebook::eden {

SessionInfo makeSessionInfo(
    const UserInfo& userInfo,
    std::string hostname,
    std::string appVersion) {
  SessionInfo env;
  env.username = userInfo.getUsername();
  env.hostname = std::move(hostname);
  env.os = getOperatingSystemName();
  env.osVersion = getOperatingSystemVersion();
  env.appVersion = std::move(appVersion);
#if defined(__APPLE__)
  env.systemArchitecture = getOperatingSystemArchitecture();
#endif
#ifdef LOGGER_FB_SESSION_INFO
  env.fbInfo = getFbInfo();
#endif
  return env;
}

std::string getOperatingSystemName() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__linux__)
  return "Linux";
#elif defined(__APPLE__)
  // Presuming EdenFS doesn't run on iOS, watchOS, or tvOS. :)
  return "macOS";
#else
  return "unknown";
#endif
}

std::string getOperatingSystemVersion() {
#if defined(_WIN32)
  // TODO: Implement build version lookup, e.g. 1903
  // reg query "HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion" /v releaseid
  return "10";
#elif defined(__linux__) || defined(__APPLE__)
  struct utsname uts;
  if (uname(&uts)) {
    return "error";
  }
  return uts.release;
#else
  return "unknown";
#endif
}

#if defined(__APPLE__)
std::string getOperatingSystemArchitecture() {
  // the c_str strips the trailing null bytes.
  return getSysCtlByName("machdep.cpu.brand_string", 64).c_str();
}
#endif

std::string getHostname() {
  char hostname[kHostNameMax + 1];
  folly::checkUnixError(
      gethostname(hostname, sizeof(hostname)),
      "gethostname() failed, errno: ",
      errno);

  // POSIX does not require implementations of gethostname to
  // null-terminate. Ensure null-termination after the call.
  hostname[kHostNameMax] = 0;

  return hostname;
}

} // namespace facebook::eden
