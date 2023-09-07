/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessInfoCache.h"

#include <folly/MapUtil.h>
#include <folly/container/EvictingCacheMap.h>
#include <folly/logging/xlog.h>
#include <folly/system/ThreadName.h>

#include "eden/common/utils/Synchronized.h"

namespace facebook::eden {

namespace detail {

class ProcessInfoNode {
 public:
  ProcessInfoNode(
      folly::SemiFuture<ProcessName> name,
      std::chrono::steady_clock::time_point d,
      ProcessInfoCache::Clock& clock)
      : name_{std::move(name)},
        lastAccess_{d.time_since_epoch()},
        clock_{clock} {}

  ProcessInfoNode(const ProcessInfoNode&) = delete;
  ProcessInfoNode& operator=(const ProcessInfoNode&) = delete;

  void recordAccess(std::chrono::steady_clock::time_point now) const {
    lastAccess_.store(now.time_since_epoch(), std::memory_order_release);
  }

  folly::SemiFuture<ProcessName> name_;
  mutable std::atomic<std::chrono::steady_clock::duration> lastAccess_;
  ProcessInfoCache::Clock& clock_;
};

} // namespace detail

namespace {

/// 256 threads
constexpr size_t kThreadLocalCacheSize = 256;

class RealThreadLocalCache : public ProcessInfoCache::ThreadLocalCache {
 public:
  bool has(pid_t pid, std::chrono::steady_clock::time_point /*now*/) override {
    // NB: Does not increment the lastAccess timestamp.
    // This is intentional: has() is called in a hot path, and this avoids
    // incrementing the NodePtr's strong refcount.
    return cache().exists(pid);
  }

  NodePtr get(pid_t pid, std::chrono::steady_clock::time_point now) override {
    auto& map = cache();
    auto iter = map.find(pid);
    NodePtr node;
    if (iter != map.end()) {
      node = iter->second.lock();
      if (node) {
        node->recordAccess(now);
      }
    }
    return node;
  }

  void put(pid_t pid, NodePtr node) override {
    cache().set(pid, std::move(node));
  }

 private:
  using Cache =
      folly::EvictingCacheMap<pid_t, std::weak_ptr<detail::ProcessInfoNode>>;

  Cache& cache() {
    if (!cache_) {
      cache_.emplace(kThreadLocalCacheSize);
    }
    return *cache_;
  }

  static thread_local std::optional<Cache> cache_;
} realThreadLocalCache;

thread_local std::optional<RealThreadLocalCache::Cache>
    RealThreadLocalCache::cache_;

class RealClock : public ProcessInfoCache::Clock {
  std::chrono::steady_clock::time_point now() override {
    return std::chrono::steady_clock::now();
  }
} realClock;

} // namespace

ProcessInfoHandle::ProcessInfoHandle(
    std::shared_ptr<detail::ProcessInfoNode> node)
    : node_{std::move(node)} {}

const ProcessName* ProcessInfoHandle::get_optional() const {
  XCHECK(node_) << "attempting to use moved-from ProcessInfoHandle";
  auto now = node_->clock_.now();
  node_->recordAccess(now);
  return node_->name_.isReady() ? &node_->name_.value() : nullptr;
}

const ProcessName& ProcessInfoHandle::get() const {
  XCHECK(node_) << "attempting to use moved-from ProcessInfoHandle";
  auto now = node_->clock_.now();
  node_->recordAccess(now);
  node_->name_.wait();
  return node_->name_.value();
}

ProcessInfoCache::ProcessInfoCache(
    std::chrono::nanoseconds expiry,
    ThreadLocalCache* threadLocalCache,
    Clock* clock,
    ProcessName (*readName)(pid_t))
    : expiry_{expiry},
      threadLocalCache_{
          threadLocalCache ? *threadLocalCache : realThreadLocalCache},
      clock_{clock ? *clock : realClock},
      readName_{readName ? readName : readProcessName} {
  workerThread_ = std::thread{[this] {
    folly::setThreadName("ProcessInfoCacheWorker");
    workerThread();
  }};
}

ProcessInfoCache::~ProcessInfoCache() {
  state_.wlock()->workerThreadShouldStop = true;
  sem_.post();
  workerThread_.join();
}

ProcessInfoHandle ProcessInfoCache::lookup(pid_t pid) {
  auto now = clock_.now();

  if (auto node = threadLocalCache_.get(pid, now)) {
    return ProcessInfoHandle{std::move(node)};
  }

  auto state = state_.wlock();
  if (auto* nodep = folly::get_ptr(state->names, pid)) {
    return ProcessInfoHandle{*nodep};
  }

  auto [p, f] = folly::makePromiseContract<ProcessName>();
  state->lookupQueue.emplace_back(pid, std::move(p));
  auto node =
      std::make_shared<detail::ProcessInfoNode>(std::move(f), now, clock_);
  state->names.emplace(pid, node);
  threadLocalCache_.put(pid, node);
  state.unlock();
  sem_.post();
  return ProcessInfoHandle{std::move(node)};
}

void ProcessInfoCache::add(pid_t pid) {
  auto now = clock_.now();

  // add() is called by very high-throughput, low-latency code, such as the
  // FUSE processing loop. It's common for a single thread to repeatedly look up
  // the same pid from the same thread, so check a thread-local cache first.
  if (threadLocalCache_.has(pid, now)) {
    return;
  }

  // To optimize for the common case where pid's name is already known, this
  // code aborts early when we can acquire a reader lock.
  //
  // When the pid's name is not known, reading the pid's name is done on a
  // background thread for two reasons:
  //
  // 1. Making a syscall in this high-throughput, low-latency path would slow
  // down the caller. Queuing work for a background worker is cheaper.
  //
  // 2. (At least on kernel (4.16.18) Reading from /proc/$pid/cmdline
  // acquires the mmap semaphore (mmap_sem) of the process in order to
  // safely probe the memory containing the command line. A page fault
  // also holds mmap_sem while it calls into the filesystem to read
  // the page. If the page is on a FUSE filesystem, the process will
  // call into FUSE while holding the mmap_sem. If the FUSE thread
  // tries to read from /proc/$pid/cmdline, it will wait for mmap_sem,
  // which won't be released because the owner is waiting for
  // FUSE. There's a small detail here that mmap_sem is a
  // reader-writer lock, so this scenario _usually_ works, since both
  // operations grab the lock for reading. However, if there is a
  // writer waiting on the lock, readers are forced to wait in order
  // to avoid starving the writer. (Thanks Omar Sandoval for the
  // analysis.)
  //
  // Thus, add() cannot ever block on the completion of reading
  // /proc/$pid/cmdline, which includes a blocking push to a bounded worker
  // queue and a read from the SharedMutex while a writer has it. The read from
  // /proc/$pid/cmdline must be done on a background thread while the state
  // lock is not held.
  //
  // The downside of placing the work on a background thread is that it's
  // possible for the process making a FUSE request to exit before its name
  // can be looked up.

  tryRlockCheckBeforeUpdate<folly::Unit>(
      state_,
      [&](const auto& state) -> std::optional<folly::Unit> {
        if (auto* nodep = folly::get_ptr(state.names, pid)) {
          (*nodep)->recordAccess(now);
          return folly::unit;
        }
        return std::nullopt;
      },
      [&](auto& wlock) -> folly::Unit {
        auto [p, f] = folly::makePromiseContract<ProcessName>();
        wlock->lookupQueue.emplace_back(pid, std::move(p));
        auto node = std::make_shared<detail::ProcessInfoNode>(
            std::move(f), now, clock_);
        wlock->names.emplace(pid, node);
        threadLocalCache_.put(pid, std::move(node));

        wlock.unlock();
        sem_.post();

        return folly::unit;
      });
}

std::map<pid_t, ProcessName> ProcessInfoCache::getAllProcessNames() {
  auto [promise, future] =
      folly::makePromiseContract<std::map<pid_t, ProcessName>>();

  state_.wlock()->getQueue.emplace_back(std::move(promise));
  sem_.post();

  return std::move(future).get();
}

void ProcessInfoCache::clearExpired(
    std::chrono::steady_clock::time_point now,
    State& state) {
  // TODO: When we can rely on C++17, it might be cheaper to move the node
  // handles into another map and deallocate them outside of the lock.
  auto iter = state.names.begin();
  while (iter != state.names.end()) {
    auto next = std::next(iter);
    if (now.time_since_epoch() -
            iter->second->lastAccess_.load(std::memory_order_seq_cst) >=
        expiry_) {
      state.names.erase(iter);
    }
    iter = next;
  }
}

void ProcessInfoCache::workerThread() {
  // Double-buffered work queues.
  std::vector<std::pair<pid_t, folly::Promise<ProcessName>>> lookupQueue;
  std::vector<folly::Promise<std::map<pid_t, ProcessName>>> getQueue;

  // Allows periodic flushing of the expired names without quadratic-time
  // insertion. waterLevel grows twice as fast as names.size() can, and when
  // it exceeds names.size(), the name set is pruned.
  size_t waterLevel = 0;

  for (;;) {
    lookupQueue.clear();
    getQueue.clear();

    sem_.wait();

    size_t currentNamesSize;

    {
      auto state = state_.wlock();
      if (state->workerThreadShouldStop) {
        // Shutdown is only initiated by the destructor and since gets
        // are blocking, this implies no gets can be pending.
        XCHECK(state->getQueue.empty())
            << "ProcessInfoCache destroyed while gets were pending!";
        return;
      }

      lookupQueue.swap(state->lookupQueue);
      getQueue.swap(state->getQueue);

      // While the lock is held, store the number of remembered names for use
      // later.
      currentNamesSize = state->names.size();
    }

    // sem_.wait() consumed one count, but we know addQueue.size() +
    // getQueue.size() + (maybe done) were added. Since we will process all
    // entries at once, rather than waking repeatedly, consume the rest.
    if (lookupQueue.size() + getQueue.size()) {
      (void)sem_.tryWait(lookupQueue.size() + getQueue.size() - 1);
    }

    // Process all additions before any gets so none are missed. It does mean
    // add(1), get(), add(2), get() processed all at once would return both
    // 1 and 2 from both get() calls.
    //
    // TODO: It might be worth skipping this during ProcessInfoCache shutdown,
    // even if it did mean any pending get() calls could miss pids added prior.
    //
    // As described in ProcessInfoCache::add() above, it is critical this work
    // be done outside of the state lock.
    for (auto& [pid, p] : lookupQueue) {
      p.setWith([this, pid = pid] { return readName_(pid); });
    }

    auto now = clock_.now();

    // Bump the water level by two so that it's guaranteed to catch up.
    // Imagine names.size() == 200 with waterLevel = 0, and add() is
    // called sequentially with new pids. We wouldn't ever catch up and
    // clear expired ones. Thus, waterLevel should grow faster than
    // names.size().
    waterLevel += 2 * lookupQueue.size();
    if (waterLevel > currentNamesSize) {
      clearExpired(now, *state_.wlock());
      waterLevel = 0;
    }

    if (!getQueue.empty()) {
      // TODO: There are a few possible optimizations here, but get() is so
      // rare that they're not worth worrying about.
      std::map<pid_t, ProcessName> allProcessNames;

      {
        auto state = state_.wlock();
        clearExpired(now, *state);
        for (const auto& [pid, name] : state->names) {
          auto& fut = name->name_;
          if (fut.isReady() && fut.hasValue()) {
            allProcessNames[pid] = fut.value();
          }
        }
      }

      for (auto& promise : getQueue) {
        promise.setValue(allProcessNames);
      }
    }
  }
}

std::optional<std::string> ProcessInfoCache::getProcessName(pid_t pid) {
  auto state = state_.rlock();
  if (auto* nodep = folly::get_ptr(state->names, pid)) {
    if ((*nodep)->name_.isReady()) {
      return (*nodep)->name_.value();
    }
  }
  return std::nullopt;
}

} // namespace facebook::eden
