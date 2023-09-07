/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessInfoCache.h"
#include <folly/portability/GTest.h>

namespace {

using namespace std::literals;
using namespace facebook::eden;

TEST(ProcessInfoCache, getProcPidCmdLine) {
  using namespace facebook::eden::detail;
  EXPECT_EQ("/proc/0/cmdline"s, getProcPidCmdLine(0).data());
  EXPECT_EQ("/proc/1234/cmdline"s, getProcPidCmdLine(1234).data());
  EXPECT_EQ("/proc/1234/cmdline"s, getProcPidCmdLine(1234).data());

  auto longestPath = getProcPidCmdLine(std::numeric_limits<pid_t>::max());
  EXPECT_EQ(longestPath.size(), strlen(longestPath.data()) + 1);
}

TEST(ProcessInfoCache, readMyPidsName) {
  ProcessInfoCache processInfoCache;
  processInfoCache.add(getpid());
  auto results = processInfoCache.getAllProcessNames();
  EXPECT_NE("", results[getpid()]);
}

TEST(ProcessInfoCache, expireMyPidsName) {
  ProcessInfoCache processInfoCache{0ms};
  processInfoCache.add(getpid());
  auto results = processInfoCache.getAllProcessInfos();
  EXPECT_EQ(0, results.size());
}
TEST(ProcessInfoCache, addFromMultipleThreads) {
  ProcessInfoCache processInfoCache;

  size_t kThreadCount = 32;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (size_t i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&] { processInfoCache.add(getpid()); });
  }

  auto results = processInfoCache.getAllProcessInfos();
  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(1, results.size());
}

class FakeClock : public ProcessInfoCache::Clock {
 public:
  std::chrono::steady_clock::time_point now() override {
    return std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{
            now_.load(std::memory_order_acquire)}};
  }

  void advance(unsigned minutes) {
    now_.fetch_add(
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::minutes{minutes})
            .count(),
        std::memory_order_release);
  }

 private:
  // std::chrono::steady_clock::time_point cannot be atomic, so store duration
  // since epoch as a raw integer.
  std::atomic<std::chrono::steady_clock::duration::rep> now_{};
};

struct Fixture : ::testing::Test, ProcessInfoCache::ThreadLocalCache {
  Fixture() : th{this}, pic{std::chrono::minutes{5}, this, &clock, readInfo} {}

  static ProcessInfo readInfo(pid_t pid) {
    auto infos = ThisHolder::this_->infos.wlock();
    return (*infos)[pid];
  }

  // ThreadLocalCache

  bool has(pid_t, std::chrono::steady_clock::time_point) override {
    return false;
  }
  NodePtr get(pid_t, std::chrono::steady_clock::time_point) override {
    return nullptr;
  }
  void put(pid_t, NodePtr) override {}

  // Allows static functions to access the current fixture. Assumes tests are
  // single-threaded.
  struct ThisHolder {
    explicit ThisHolder(Fixture* fixture) {
      this_ = fixture;
    }
    ~ThisHolder() {
      this_ = nullptr;
    }
    static Fixture* this_;
  } th;

  FakeClock clock;
  ProcessInfoCache pic;

  folly::Synchronized<std::map<pid_t, ProcessInfo>> infos;
};

Fixture* Fixture::ThisHolder::this_ = nullptr;

TEST_F(Fixture, lookup_expires) {
  (*infos.wlock())[10] = {0, "watchman", "watchman"};
  auto lookup = pic.lookup(10);
  EXPECT_EQ("watchman", lookup.get().name);

  clock.advance(10);

  // For the info to expire, we either need to add some new pids and trip the
  // water level check, or call getAllProcessInfos.
  (*infos.wlock())[11] = {0, "new", "new"};
  (*infos.wlock())[12] = {0, "newer", "newer"};
  EXPECT_EQ("new", pic.lookup(11).get().name);
  EXPECT_EQ("newer", pic.lookup(12).get().name);

  (*infos.wlock())[10] = {0, "edenfs", "edenfs"};
  EXPECT_EQ("edenfs", pic.lookup(10).get().name);

  // But the old lookup should still have the old info.
  EXPECT_EQ("watchman", lookup.get().name);
}

} // namespace
