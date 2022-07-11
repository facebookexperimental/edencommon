/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessNameCache.h"
#include <folly/portability/GTest.h>

namespace {

using namespace std::literals;
using namespace facebook::eden;

TEST(ProcessNameCache, getProcPidCmdLine) {
  using namespace facebook::eden::detail;
  EXPECT_EQ("/proc/0/cmdline"s, getProcPidCmdLine(0).data());
  EXPECT_EQ("/proc/1234/cmdline"s, getProcPidCmdLine(1234).data());
  EXPECT_EQ("/proc/1234/cmdline"s, getProcPidCmdLine(1234).data());

  auto longestPath = getProcPidCmdLine(std::numeric_limits<pid_t>::max());
  EXPECT_EQ(longestPath.size(), strlen(longestPath.data()) + 1);
}

TEST(ProcessNameCache, readMyPidsName) {
  ProcessNameCache processNameCache;
  processNameCache.add(getpid());
  auto results = processNameCache.getAllProcessNames();
  EXPECT_NE("", results[getpid()]);
}

TEST(ProcessNameCache, expireMyPidsName) {
  ProcessNameCache processNameCache{0ms};
  processNameCache.add(getpid());
  auto results = processNameCache.getAllProcessNames();
  EXPECT_EQ(0, results.size());
}

TEST(ProcessNameCache, addFromMultipleThreads) {
  ProcessNameCache processNameCache;

  size_t kThreadCount = 32;
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (size_t i = 0; i < kThreadCount; ++i) {
    threads.emplace_back([&] { processNameCache.add(getpid()); });
  }

  auto results = processNameCache.getAllProcessNames();

  for (auto& thread : threads) {
    thread.join();
  }
  EXPECT_EQ(1, results.size());
}

class FakeClock : public ProcessNameCache::Clock {
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

struct Fixture : ::testing::Test, ProcessNameCache::ThreadLocalCache {
  Fixture() : th{this}, pnc{std::chrono::minutes{5}, this, &clock, readName} {}

  static ProcessName readName(pid_t pid) {
    auto names = ThisHolder::this_->names.wlock();
    return (*names)[pid];
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
  ProcessNameCache pnc;

  folly::Synchronized<std::map<pid_t, ProcessName>> names;
};

Fixture* Fixture::ThisHolder::this_ = nullptr;

TEST_F(Fixture, lookup_expires) {
  (*names.wlock())[10] = "watchman";
  auto lookup = pnc.lookup(10);
  EXPECT_EQ("watchman", lookup.get());

  clock.advance(10);

  // For the name to expire, we either need to add some new pids and trip the
  // water level check, or call getAllProcessNames.
  (*names.wlock())[11] = "new";
  (*names.wlock())[12] = "newer";
  EXPECT_EQ("new", pnc.lookup(11).get());
  EXPECT_EQ("newer", pnc.lookup(12).get());

  (*names.wlock())[10] = "edenfs";
  EXPECT_EQ("edenfs", pnc.lookup(10).get());

  // But the old lookup should still have the old name.
  EXPECT_EQ("watchman", lookup.get());
}

} // namespace
