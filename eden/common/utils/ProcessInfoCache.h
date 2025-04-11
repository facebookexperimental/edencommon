/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest_prod.h>

#include <folly/Synchronized.h>
#include <folly/futures/Promise.h>
#include <folly/synchronization/LifoSem.h>

#include "eden/common/utils/ProcessInfo.h"

namespace folly {
template <class T>
class SharedPromise;
}

namespace facebook::eden {

class FaultInjector;

namespace detail {
constexpr std::chrono::nanoseconds PROCESS_INFO_CACHE_DEFAULT_EXPIRY =
    std::chrono::minutes{5};
class ProcessInfoNode;
} // namespace detail

/**
 * Represents strong interest in a process info. The info will be available as
 * long as the ProcessInfoHandle is held.
 *
 * ProcessInfoHandle does not guarantee the info won't be evicted from the
 * ProcessInfoCache, but for any given ProcessInfoHandle, the info will be
 * available and will not change.
 */
class ProcessInfoHandle {
 public:
  // Private in spirit. Not actually usable outside of the .cpp file.
  explicit ProcessInfoHandle(std::shared_ptr<detail::ProcessInfoNode> node);

  ProcessInfoHandle(const ProcessInfoHandle&) = default;
  ProcessInfoHandle(ProcessInfoHandle&&) = default;

  ProcessInfoHandle& operator=(const ProcessInfoHandle&) = default;
  ProcessInfoHandle& operator=(ProcessInfoHandle&&) = default;

  /**
   * Info lookups are asynchronous. Returns nullptr if it's not available yet,
   * and the info if it is.
   */
  const ProcessInfo* get_optional() const;

  /**
   * Blocks until the process info is available.
   *
   * Be careful only to use this function from threads that aren't reentrant
   * with the process of retrieving a process info, such as a  FUSE request
   * handler.
   *
   * May throw, notably if the ProcessInfoCache is destroyed before it could
   * read the process info.
   */
  ProcessInfo get() const;

 private:
  FRIEND_TEST(ProcessInfoCache, faultinjector);
  FRIEND_TEST(ProcessInfoCache, multipleLookups);

  const folly::SemiFuture<ProcessInfo>& future() const;

  std::shared_ptr<detail::ProcessInfoNode> node_;
};

class ProcessInfoCache {
 public:
  class ThreadLocalCache {
   public:
    using NodePtr = std::shared_ptr<detail::ProcessInfoNode>;

    virtual ~ThreadLocalCache() = default;
    /// Returns whether this thread has recently seen a node for this pid. Does
    /// not imply get() will return a non-null NodePtr.
    /// has() is an optimization that, if true, prevents the ProcessInfoCache
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

  class Clock {
   public:
    virtual ~Clock() = default;
    virtual std::chrono::steady_clock::time_point now() = 0;
  };

  /**
   * Create a cache that maintains process infos until `expiry` has elapsed
   * without them being referenced or observed.
   */
  explicit ProcessInfoCache(
      std::chrono::nanoseconds expiry =
          detail::PROCESS_INFO_CACHE_DEFAULT_EXPIRY,
      // For testing:
      ThreadLocalCache* threadLocalCache = nullptr,
      Clock* clock = nullptr,
      const std::function<ProcessInfo(pid_t)>& readInfo = nullptr,
      FaultInjector* faultInjector = nullptr);

  /**
   * Config options passed to makeReadProcessInfoFunc() to customize the
   * information retrieved from the process by the worker thread.
   */
  struct ReadFuncConfig {
    // Whether to fetch the user info for the process.
    bool fetchUserInfo;
    ReadUserInfoConfig readUserInfoConfig;
    ReadFuncConfig(
        bool fetchUserInfo = false,
        ReadUserInfoConfig readUserInfoConfig = ReadUserInfoConfig())
        : fetchUserInfo(fetchUserInfo),
          readUserInfoConfig(readUserInfoConfig) {}
  };

  /**
   * Ctor that allows customizing the data captured for the process info.
   */
  explicit ProcessInfoCache(
      ReadFuncConfig config,
      std::chrono::nanoseconds expiry =
          detail::PROCESS_INFO_CACHE_DEFAULT_EXPIRY)
      : ProcessInfoCache(
            expiry,
            nullptr,
            nullptr,
            makeReadProcessInfoFunc(config),
            nullptr) {}

  ~ProcessInfoCache();

  /**
   * Performs a non-blocking lookup request for a pid's info.
   */
  ProcessInfoHandle lookup(pid_t pid);

  /**
   * Records a reference to a pid. This is called by performance-critical code.
   * Refreshes the expiry on the given pid. The process info is read
   * asynchronously on a background thread.
   *
   * If possible, the caller should avoid calling add() with a series of
   * redundant pids.
   */
  void add(pid_t pid);

  /**
   * Called rarely to produce a map of all non-expired pids to their executable
   * infos.
   */
  std::map<pid_t, ProcessInfo> getAllProcessInfos();

  /**
   * Called rarely to produce a map of all non-expired pids to their executable
   * names.
   */
  std::map<pid_t, ProcessName> getAllProcessNames();

  /**
   * Called occasionally to produce the info of the pid. If the info has
   * already been resolved this returns that info. Otherwise this will return
   * nullopt. In the future it may wait for the info to be resolved.
   */
  std::optional<ProcessInfo> getProcessInfo(pid_t pid);

  /**
   * Called occasionally to produce the name of the pid. If the info has
   * already been resolved this returns that info's name. Otherwise this will
   * return nullopt.
   */
  std::optional<ProcessName> getProcessName(pid_t pid);

  /**
   * Commandlines (on linux anyways) use \0 instead of spaces to separate
   * arguments. sl is often a command we are interested in. and sl also
   * does some funky commandline manipulation that causes a bunch of \0 to be
   * on the end of their commandline. This will clean those off.
   */
  static std::string cleanProcessCommandline(std::string process);

  /**
   * Allows customizing the information retrieved from the process by the
   * ProcessInfoCache on add/lookup.
   */
  static std::function<ProcessInfo(pid_t)> makeReadProcessInfoFunc(
      ReadFuncConfig config = ReadFuncConfig{});

 private:
  struct State {
    std::unordered_map<pid_t, std::shared_ptr<detail::ProcessInfoNode>> infos;

    bool workerThreadShouldStop = false;
    // The following queues are intentionally unbounded. add() cannot block.
    // TODO: We could set a high limit on the length of the queue and drop
    // requests if necessary.
    std::vector<
        std::pair<pid_t, std::shared_ptr<folly::SharedPromise<ProcessInfo>>>>
        lookupQueue;
    std::vector<folly::Promise<std::map<pid_t, ProcessInfo>>> getAllQueue;
  };

  void clearExpired(std::chrono::steady_clock::time_point now, State& state);
  void workerThread();

  const std::chrono::nanoseconds expiry_;
  ThreadLocalCache& threadLocalCache_;
  Clock& clock_;
  std::function<ProcessInfo(pid_t)> readInfo_;
  folly::Synchronized<State> state_;
  folly::LifoSem sem_;
  std::thread workerThread_;

  // For testing various race conditions.
  // Note: unlike other things that depend on FaultInjector, this pointer
  // can be null. We only set this in unit tests currently, we will need to
  // thread through the injector if we want to use it in production EdenFS.
  FaultInjector* faultInjector_;
};

} // namespace facebook::eden
