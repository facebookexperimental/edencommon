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
