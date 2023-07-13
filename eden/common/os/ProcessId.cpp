/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/os/ProcessId.h"

#include <optional>
#include <ostream>
#include <type_traits>

namespace facebook::eden {

static_assert(4 == sizeof(ProcessId));
static_assert(4 == sizeof(OptionalProcessId));
static_assert(std::is_nothrow_move_constructible_v<ProcessId>);
static_assert(std::is_nothrow_move_assignable_v<ProcessId>);
static_assert(std::is_nothrow_default_constructible_v<OptionalProcessId>);
static_assert(std::is_nothrow_move_constructible_v<OptionalProcessId>);
static_assert(std::is_nothrow_move_assignable_v<OptionalProcessId>);

void ProcessId::assertValid() {
#ifndef _WIN32
  if (static_cast<int32_t>(pid_) < 0) {
    // TODO: should we include the pid? Check errno if it's -1?
    throw InvalidProcessId{};
  }
#else
  if (pid_ == ~uint32_t{}) {
    throw InvalidProcessId{};
  }
#endif
}

const char* InvalidProcessId::what() const noexcept {
  return "Invalid process ID";
}

void OptionalProcessId::throwBadAccess() {
  throw std::bad_optional_access{};
}

std::ostream& operator<<(std::ostream& os, OptionalProcessId pid) {
  return pid ? (os << pid.raw()) : (os << -1);
}

} // namespace facebook::eden
