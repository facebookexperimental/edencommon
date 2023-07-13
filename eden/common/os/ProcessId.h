/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fmt/core.h>
#include <stdint.h>
#include <exception>
#include <functional>
#include <iosfwd>
#include <optional>
// This file intentionally does not include <windows.h> or <sys/types.h> to have
// no impact on compile times on otherwise platform-independent code.

namespace facebook::eden {

class InvalidProcessId final : public std::exception {
 public:
  const char* what() const noexcept override;
};

/**
 * 32-bit, cross-platform process identifier.
 *
 * Corresponds to pid_t on unix and DWORD on Windows. We're not aware of any
 * platform with process identifiers larger than 32 bits, so uint32_t should
 * suffice.
 *
 * Process ID 0 is valid. On unix (or at least FUSE), it indicates the kernel.
 * On Windows, it indicates the system idle process.
 */
class ProcessId {
 public:
  ProcessId() = delete;

  /**
   * Throws InvalidProcessId if pid is negative on unix or -1 on Windows.
   * Zero is valid.
   */
  explicit ProcessId(uint32_t pid) : pid_{pid} {
    assertValid();
  }

  static ProcessId unchecked(uint32_t pid) noexcept {
    return ProcessId(pid, Unchecked{});
  }

  uint32_t get() const noexcept {
    return pid_;
  }

  friend bool operator==(ProcessId lhs, ProcessId rhs) noexcept {
    return lhs.pid_ == rhs.pid_;
  }

  friend bool operator!=(ProcessId lhs, ProcessId rhs) noexcept {
    return lhs.pid_ != rhs.pid_;
  }

  friend bool operator<(ProcessId lhs, ProcessId rhs) noexcept {
    return lhs.pid_ < rhs.pid_;
  }

 private:
  struct Unchecked {};
  explicit ProcessId(uint32_t pid, Unchecked) : pid_{pid} {}

  void assertValid();

  uint32_t pid_;
};

/**
 * Analogous to std::optional<ProcessId>, but fits in 32 bits.
 *
 * -1 indicates unset on all platforms.
 */
class OptionalProcessId {
 public:
  /**
   * Default constructor creates an empty ProcessId.
   */
  /*implicit*/ OptionalProcessId(std::nullopt_t = std::nullopt) noexcept {}

  /*implicit*/ OptionalProcessId(ProcessId pid) noexcept : pid_{pid.get()} {}

  /**
   * Returns the underlying ProcessId, if set. Throws std::bad_optional_access
   * if unset.
   */
  ProcessId value() const {
    if (pid_ == kUnset) {
      throwBadAccess();
    }
    return ProcessId::unchecked(pid_);
  }

  /**
   * Returns a valid ProcessId, with value zero if unset.
   *
   * Note that pid zero is valid. On Windows, it's the idle
   * process. On Linux, it sometimes indicates the kernel.
   */
  ProcessId valueOrZero() const noexcept {
    return ProcessId::unchecked(pid_ == kUnset ? 0 : pid_);
  }

  uint32_t raw() const noexcept {
    return pid_;
  }

  explicit operator bool() const noexcept {
    return pid_ != kUnset;
  }

  friend bool operator==(
      OptionalProcessId lhs,
      OptionalProcessId rhs) noexcept {
    return lhs.pid_ == rhs.pid_;
  }

  friend bool operator!=(
      OptionalProcessId lhs,
      OptionalProcessId rhs) noexcept {
    return lhs.pid_ != rhs.pid_;
  }

  friend bool operator<(OptionalProcessId lhs, OptionalProcessId rhs) noexcept {
    return lhs.pid_ < rhs.pid_;
  }

 private:
  explicit OptionalProcessId(uint32_t raw) noexcept : pid_{raw} {}

  void assertSet();

  // Throws std::bad_optional_access.
  [[noreturn]] static void throwBadAccess();

  static inline constexpr uint32_t kUnset = ~uint32_t{0};
  uint32_t pid_ = kUnset;
};

std::ostream& operator<<(std::ostream& os, OptionalProcessId pid);

} // namespace facebook::eden

template <>
struct std::hash<facebook::eden::ProcessId> {
  size_t operator()(facebook::eden::ProcessId pid) const noexcept {
    return pid.get();
  }
};

template <>
struct std::hash<facebook::eden::OptionalProcessId> {
  size_t operator()(facebook::eden::OptionalProcessId pid) const noexcept {
    return pid.raw();
  }
};

template <>
struct fmt::formatter<facebook::eden::ProcessId> : formatter<uint32_t> {
  template <typename Context>
  auto format(facebook::eden::ProcessId pid, Context& ctx) const {
    return formatter<uint32_t>::format(pid.get(), ctx);
  }
};

template <>
struct fmt::formatter<facebook::eden::OptionalProcessId> : formatter<int64_t> {
  template <typename Context>
  auto format(facebook::eden::OptionalProcessId pid, Context& ctx) const {
    int64_t v = pid ? int64_t{pid.raw()} : -1;
    return formatter<int64_t>::format(v, ctx);
  }
};
