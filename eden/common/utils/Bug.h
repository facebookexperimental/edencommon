/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fmt/core.h>
#include <folly/CppAttributes.h>
#include <folly/Likely.h>
#include <folly/futures/Future.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <tuple>

/**
 * EDEN_BUG() should be used to log logic errors that should not happen unless
 * there is a bug in the code.
 *
 * By default this macro crashes in debug builds and logs then throws in
 * production builds. setEdenBugFatal() can override that behavior at runtime.
 *
 * Use XLOG(FATAL) if you want to crash the program even in production builds.
 */
#define EDEN_BUG()                   \
  ::facebook::eden::EdenBugThrow() & \
      ::facebook::eden::EdenBug(__FILE__, __LINE__)

/**
 * EDEN_BUG_FUTURE() is similar to EDEN_BUG() but should be used in context
 * where a Future should be returned.
 *
 * When EDEN_BUG reports are fatal (see setEdenBugFatal()) this will crash;
 * otherwise this will return a folly::Future<Type> containing an exception.
 */
#define EDEN_BUG_FUTURE(Type)            \
  ::facebook::eden::EdenBugTry<Type>() & \
      ::facebook::eden::EdenBug(__FILE__, __LINE__)

/**
 * EDEN_BUG_EXCEPTION() is similar to EDEN_BUG() but returns an exception.
 */
#define EDEN_BUG_EXCEPTION()             \
  ::facebook::eden::EdenBugException() & \
      ::facebook::eden::EdenBug(__FILE__, __LINE__)

/**
 * Report an internal inconsistency without unwinding.
 *
 * The report still aborts when setEdenBugFatal(true) is in effect.
 */
#define EDEN_BUG_NONFATAL()             \
  ::facebook::eden::EdenBugNonFatal() & \
      ::facebook::eden::EdenBug(__FILE__, __LINE__)

/**
 * Report a failed expectation without throwing, unless fatal mode aborts.
 *
 * The switch guard keeps `if (x) EDEN_EXPECT(c);` free of -Wdangling-else.
 */
#define EDEN_EXPECT(cond)                        \
  switch (0)                                     \
  case 0:                                        \
  default:                                       \
    if (FOLLY_LIKELY(static_cast<bool>(cond))) { \
    } else                                       \
      EDEN_BUG_NONFATAL() << "expectation failed: " #cond " "

namespace folly {
class exception_wrapper;
}

namespace facebook::eden {

/**
 * A helper class returned by the EDEN_BUG() macro.
 *
 * toException() can be called to convert it to a folly::exception_wrapper
 * If toException() has not been called, it will throw an exception when it is
 * destroyed.
 *
 * When EDEN_BUG reports are fatal (the default in debug builds; see
 * setEdenBugFatal()), EdenBug aborts the program rather than throwing or
 * returning an exception.
 */
class EdenBug {
 public:
  [[FOLLY_ATTR_GNU_COLD]] EdenBug(const char* file, int lineNumber);
  [[FOLLY_ATTR_GNU_COLD]] EdenBug(EdenBug&& other) noexcept;
  EdenBug& operator=(EdenBug&& other) = delete;
  ~EdenBug();

  /**
   * Append to the bug message.
   */
  template <typename T>
  EdenBug&& operator<<(const T& t) && {
    fmt::format_to(std::back_inserter(message_), "{}", t);
    return std::move(*this);
  }

  /**
   * Convert this EdenBug object to a folly::exception_wrapper
   *
   * If toException() is never called on an EdenBug object, it will throw on
   * destruction.
   */
  folly::exception_wrapper toException();

  /**
   * A wrapper for toException().throw_exception(). A typical use of EDEN_BUG()
   * where the bug is captured is actually noreturn, but the compiler can't see
   * that because moved-from EdenBug doesn't throw.
   *
   * To avoid compiler warnings, write:
   *   auto bug = EDEN_BUG() << "...";
   *   bug.throwException();
   */
  [[noreturn]] void throwException();

  /**
   * Prevent EDEN_BUG() from crashing the program, even when EDEN_BUG reports
   * are fatal.
   *
   * This is intended to allow unit tests to disable crashing.
   * This generally shouldn't ever be called from normal production code.
   */
  static void acquireDisableCrashLease();
  static void releaseDisableCrashLease();

 private:
  void logError();

  const char* file_;
  int lineNumber_;
  bool processed_{false};
  std::string message_;
};

class EdenBugThrow {
 public:
  // We use operator&() here since it binds with lower precedence than the <<
  // operator used to construct the EdenBug message.
  [[noreturn]] void operator&(EdenBug&& bug) const {
    bug.throwException();
  }
};

template <typename T>
class EdenBugTry {
 public:
  [[nodiscard]]
  folly::Try<T> operator&(EdenBug&& bug) const {
    return folly::Try<T>(bug.toException());
  }
};

class EdenBugException {
 public:
  [[nodiscard]]
  folly::exception_wrapper operator&(EdenBug&& bug) const {
    return bug.toException();
  }
};

/** Converts an EdenBug report without propagating its exception. */
class EdenBugNonFatal {
 public:
  void operator&(EdenBug&& bug) const {
    std::ignore = bug.toException();
  }
};

/**
 * Control whether EDEN_BUG reports abort the process.
 *
 * The default is folly::kIsDebug, preserving the historical build-mode
 * behavior. EdenBugDisabler suppresses aborts regardless of this setting.
 */
void setEdenBugFatal(bool fatal);
bool isEdenBugFatal();

/** Return the process-wide number of EDEN_BUG reports. */
uint64_t getEdenBugCount();

/**
 * EdenBugDisabler temporarily disables crashing on EDEN_BUG as long as it
 * exists.
 */
class EdenBugDisabler {
 public:
  EdenBugDisabler();
  ~EdenBugDisabler();

  EdenBugDisabler(const EdenBugDisabler&) = delete;
  EdenBugDisabler operator=(const EdenBugDisabler&) = delete;
};
} // namespace facebook::eden
