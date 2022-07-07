/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Synchronized.h>
#include <folly/futures/Promise.h>
#include <folly/synchronization/LifoSem.h>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace facebook::eden {

namespace detail {
class ProcessNameNode;
}

/**
 * Process names are arbitrary bytes on POSIX, including embedded zeroes when
 * fetching full command lines, and some UTF-8-ish encoding on Windows.
 */
using ProcessName = std::string;

/**
 * Represents strong interest in a process name. The name will be available as
 * long as the ProcessNameHandle is held.
 *
 * ProcessNameHandle does not guarantee the name won't be evicted from the
 * ProcessNameCache, but for any given ProcessNameHandle, the name will be
 * available and will not change.
 */
class ProcessNameHandle {
 public:
  // Private in spirit. Not actually usable outside of the .cpp file.
  explicit ProcessNameHandle(std::shared_ptr<detail::ProcessNameNode> node);

  ProcessNameHandle(const ProcessNameHandle&) = default;
  ProcessNameHandle(ProcessNameHandle&&) = default;

  ProcessNameHandle& operator=(const ProcessNameHandle&) = default;
  ProcessNameHandle& operator=(ProcessNameHandle&&) = default;

  /**
   * Name lookups are asynchronous. Returns nullptr if it's not available yet,
   * and the name if it is.
   */
  const ProcessName* get_optional() const;

  /**
   * Blocks until the process name is available.
   *
   * Be careful only to use this function from threads that aren't reentrant
   * with the process of retrieving a process name or command line, such as a
   * FUSE request handler.
   *
   * May throw, notably if the ProcessNameCache is destroyed before it could
   * read the process name.
   */
  const ProcessName& get() const;

 private:
  std::shared_ptr<detail::ProcessNameNode> node_;
};

class ProcessNameCache {
 public:
  class ThreadLocalCache {
   public:
    using NodePtr = std::shared_ptr<detail::ProcessNameNode>;

    virtual ~ThreadLocalCache() = default;
    /// Returns whether this thread has recently seen a node for this pid. Does
    /// not imply get() will return a non-null NodePtr.
    /// has() is an optimization that, if true, prevents the ProcessNameCache
    /// from queuing a lookup.
    virtual bool has(pid_t pid, std::chrono::steady_clock::time_point now) = 0;
    /// Returns a reference to a node if it exists in the thread-local cache.
    virtual NodePtr get(
        pid_t pid,
        std::chrono::steady_clock::time_point now) = 0;
    /// Inserts a node into the thread-local cache. Assumes caller has set the
    /// last-access time.
    virtual void put(pid_t pid, NodePtr node) = 0;
  };

  /**
   * Create a cache that maintains process names until `expiry` has elapsed
   * without them being referenced or observed.
   */
  explicit ProcessNameCache(
      std::chrono::nanoseconds expiry = std::chrono::minutes{5},
      ThreadLocalCache* threadLocalCache = nullptr);

  ~ProcessNameCache();

  /**
   * Performs a non-blocking lookup request for a pid's name.
   */
  ProcessNameHandle lookup(pid_t pid);

  /**
   * Records a reference to a pid. This is called by performance-critical code.
   * Refreshes the expiry on the given pid. The process name is read
   * asynchronously on a background thread.
   *
   * If possible, the caller should avoid calling add() with a series of
   * redundant pids.
   */
  void add(pid_t pid);

  /**
   * Called rarely to produce a map of all non-expired pids to their executable
   * names.
   */
  std::map<pid_t, ProcessName> getAllProcessNames();

  /**
   * Called occassionally to produce the command line name of the pid. If the
   * name has already been resolved this returns that name. Otherwise this will
   * return nullopt. In the future it may wait for the name to be resolved.
   */
  std::optional<ProcessName> getProcessName(pid_t pid);

 private:
  struct State {
    std::unordered_map<pid_t, std::shared_ptr<detail::ProcessNameNode>> names;

    bool workerThreadShouldStop = false;
    // The following queues are intentionally unbounded. add() cannot block.
    // TODO: We could set a high limit on the length of the queue and drop
    // requests if necessary.
    std::vector<std::pair<pid_t, folly::Promise<ProcessName>>> lookupQueue;
    std::vector<folly::Promise<std::map<pid_t, ProcessName>>> getQueue;
  };

  void clearExpired(std::chrono::steady_clock::time_point now, State& state);
  void workerThread();

  const std::chrono::nanoseconds expiry_;
  ThreadLocalCache& threadLocalCache_;
  folly::Synchronized<State> state_;
  folly::LifoSem sem_;
  std::thread workerThread_;
};

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

/**
 * Given a pid, returns its executable name or <err:###> with the appropriate
 * errno.
 *
 * readPidName only allocates if the resulting executable name does not fit in
 * std::string's small string optimization, which is relatively rare for
 * programs in /usr/bin.
 */
ProcessName readPidName(pid_t pid);

} // namespace detail

} // namespace facebook::eden
