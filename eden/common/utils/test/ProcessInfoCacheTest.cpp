/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessInfoCache.h"

#include <folly/portability/GTest.h>
#include <folly/system/ThreadName.h>

#include "eden/common/utils/FaultInjector.h"

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

// these tests have to be in the same namespace as ProcessInfoCache so that
// the FRIEND_TEST macro works to allow them to access private members of
// the classes there.
namespace facebook::eden {
TEST(ProcessInfoCache, faultinjector) {
  FaultInjector faultInjector = FaultInjector{/*enabled=*/true};
  ProcessInfoCache processInfoCache = ProcessInfoCache{
      /*expiry=*/std::chrono::minutes{5},
      /*threadLocalCache=*/nullptr,
      /*clock=*/nullptr,
      /*readInfo=*/nullptr,
      /*faultInjector=*/&faultInjector};

  // prevent the process info cache from making any progress on looking up pids
  faultInjector.injectBlock("ProcessInfoCache::workerThread", ".*");

  auto info = processInfoCache.lookup(getpid());
  ASSERT_TRUE(
      faultInjector.waitUntilBlocked("ProcessInfoCache::workerThread", 1s));

  ASSERT_FALSE(info.future().isReady());

  // now the worker thread can get to work looking up the pids.
  faultInjector.removeFault("ProcessInfoCache::workerThread", ".*");
  faultInjector.unblock("ProcessInfoCache::workerThread", ".*");

  // anything except timing out is fine, might as well check that the name
  // is something legit.
  EXPECT_NE("", info.get().name);
}

TEST(ProcessInfoCache, multipleLookups) {
  FaultInjector faultInjector = FaultInjector{/*enabled=*/true};
  ProcessInfoCache processInfoCache = ProcessInfoCache{
      /*expiry=*/std::chrono::minutes{5},
      /*threadLocalCache=*/nullptr,
      /*clock=*/nullptr,
      /*readInfo=*/nullptr,
      /*faultInjector=*/&faultInjector};

  // prevent the process info cache from making any progress on looking up pids
  faultInjector.injectBlock("ProcessInfoCache::workerThread", ".*");

  auto info1 = processInfoCache.lookup(getpid());
  auto info2 = processInfoCache.lookup(getpid());
  ASSERT_TRUE(
      faultInjector.waitUntilBlocked("ProcessInfoCache::workerThread", 1s));

  // Assumption: these should share the same node since they are cached and
  // worker could not have made any progress yet.
  ASSERT_EQ(info1.node_.get(), info2.node_.get());

  auto thread1 = std::thread{[info = std::move(info1)] {
    folly::setThreadName("info1");
    EXPECT_NE("", info.get().name);
  }};

  auto thread2 = std::thread{[info = std::move(info2)] {
    folly::setThreadName("info2");
    EXPECT_NE("", info.get().name);
  }};

  faultInjector.removeFault("ProcessInfoCache::workerThread", ".*");
  faultInjector.unblock("ProcessInfoCache::workerThread", ".*");

  thread1.join();
  thread2.join();
}

TEST(ProcessInfoCache, testSlCommandlineCleaning) {
  // Sapling does some commandline manipulation to name the background processes
  // something like pfc[worker/XXXXXXXX]. But this causes the commanline to be
  // full of null bytes. This is something we want to be filtered out in
  // telemetry.

  // note to editor: we use the char *, size_t variant of the string constructor
  // o.w the \0 will be interpreted as the end of the string!
  auto sl_worker_raw_cmdline = std::string{
      "pfc[worker/663504]\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000\u0000",
      112};

  EXPECT_EQ(
      "pfc[worker/663504]",
      ProcessInfoCache::cleanProcessCommandline(sl_worker_raw_cmdline));
}

TEST(ProcessInfoCache, testBuckCommandlineCleaning) {
  // Commandlines are \0 byte separated. We should turn those null bytes into
  // spaces to make the commandlines easier to read in telemetry.

  // note to editor: we use the char *, size_t variant of the string constructor
  // o.w the \0 will be interpreted as the end of the string!
  auto buck2_raw_cmdline = std::string{
      "buck2d[fbsource]\u0000--isolation-dir\u0000v2\u0000daemon\u0000{\"buck_config\":\"somevalue\"}\u0000",
      70};

  EXPECT_EQ(
      "buck2d[fbsource] --isolation-dir v2 daemon {\"buck_config\":\"somevalue\"}",
      ProcessInfoCache::cleanProcessCommandline(buck2_raw_cmdline));
}

} // namespace facebook::eden
