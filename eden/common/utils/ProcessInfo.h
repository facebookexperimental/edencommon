/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/SysTypes.h>
#include <array>
#include <optional>
#include <string>

namespace facebook::eden {

/**
 * Stores a human-readable process name or command line for use in diagnostic
 * tools.
 *
 * Process names are arbitrary bytes on POSIX, including embedded zeroes when
 * fetching full command lines, and some UTF-8-ish encoding on Windows.
 *
 * May be truncated for performance, or contain unexpected or arbitrary data, as
 * when a process calls `pthread_setname_np` on the main thread.
 */
using ProcessName = std::string;

/**
 * Stores a simple, humand-readable, name of the process. This is in
 * contrast to ProcessName which stores the process command line minus the
 * process path.
 */
using ProcessSimpleName = std::string;

/**
 * Allows configuring how ProcessUserInfo is read in readUserInfo function
 */
struct ReadUserInfoConfig {
  // Attempt to find the 'real user' if effective user is root
  bool resolveRootUser = false;
  // Fetch usernames for uid/euid during readUserInfo
  bool fetchUsernames = false;
};
/**
 * Information collected about the user running the process.
 */
class ProcessUserInfo {
 public:
  uid_t ruid;
  uid_t euid;
  ProcessUserInfo(
      uid_t ruid,
      uid_t euid,
      // For testing
      std::string realUsername = "",
      std::string effectiveUsername = "")
      : ruid(ruid),
        euid(euid),
        realUsername_(std::move(realUsername)),
        effectiveUsername_(std::move(effectiveUsername)) {}

  static std::string uidToUsername(uid_t uid);

  std::string getRealUsername() {
    if (realUsername_.empty()) {
      realUsername_ = uidToUsername(ruid);
    }
    return realUsername_;
  }

  std::string getEffectiveUsername() {
    if (effectiveUsername_.empty()) {
      if (ruid == euid) {
        effectiveUsername_ = getRealUsername();
      } else {
        effectiveUsername_ = uidToUsername(euid);
      }
    }
    return effectiveUsername_;
  }

 private:
  std::string realUsername_;
  std::string effectiveUsername_;
};

/**
 * Information collected about a process. Used for diagnostic tools and logging.
 */
struct ProcessInfo {
  pid_t ppid;
  ProcessName name;
  ProcessSimpleName simpleName;
  std::optional<ProcessUserInfo> userInfo;
};

/**
 * Looks up a process name corresponding to the specified process ID.
 *
 * May throw an exception. May also return a synthesized process name including
 * an error code or message.
 *
 * Attempts to avoid allocation when the process name fits in std::string's SSO.
 */
ProcessName readProcessName(pid_t pid);

/**
 * Fetches the process name for the specified process ID. If the pid is invalid
 * or an error occurs while fetching, returns "<unknown>".
 */
ProcessSimpleName readProcessSimpleName(pid_t pid);

/*
 * Fetches the ProcessUserInfo from a pid
 * If the pid is invalid or an error occurs while fetching then we return
 * ProcessUserInfo with default values.
 */
std::optional<ProcessUserInfo> readUserInfo(
    pid_t pid,
    ReadUserInfoConfig config);

/**
 * Get the parent process ID of the specified process ID, if one exists.
 */
std::optional<pid_t> getParentProcessId(pid_t pid);

namespace detail {

/**
 * The number of digits required for a decimal representation of a pid.
 */
constexpr size_t kMaxDecimalPidLength = 10;
static_assert(sizeof(pid_t) <= 4);

/**
 * A stack-allocated string with the contents /proc/<pid>/cmdline for any pid.
 */
using ProcPidCmdLine = std::array<
    char,
    6 /* /proc/ */ + kMaxDecimalPidLength + 8 /* /cmdline */ + 1 /* null */>;

/**
 * Returns the ProcPidCmdLine for a given pid. The result is always
 * null-terminated.
 */
ProcPidCmdLine getProcPidCmdLine(pid_t pid);

} // namespace detail

} // namespace facebook::eden
