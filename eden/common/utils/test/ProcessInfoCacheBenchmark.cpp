/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessInfoCache.h"

#include <benchmark/benchmark.h>
#include <folly/logging/LoggerDB.h>

using namespace facebook::eden;

struct ProcessInfoCacheFixture : benchmark::Fixture {
  ProcessInfoCacheFixture() {
    // Initializer the logger singleton so it doesn't get initialized during
    // global teardown. This is the classic race between atexit() and static
    // initialization on MSVCRT.
    folly::LoggerDB::get();
  }

  ProcessInfoCache processInfoCache;
};

/**
 * A high but realistic amount of contention.
 */
constexpr size_t kThreadCount = 4;

BENCHMARK_DEFINE_F(ProcessInfoCacheFixture, add_self)(benchmark::State& state) {
  auto myPid = getpid();
  for (auto _ : state) {
    processInfoCache.add(myPid);
  }
}

BENCHMARK_REGISTER_F(ProcessInfoCacheFixture, add_self)->Threads(kThreadCount);

BENCHMARK_MAIN();
