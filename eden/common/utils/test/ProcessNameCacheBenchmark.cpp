/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcessNameCache.h"

#include <benchmark/benchmark.h>
#include "eden/common/utils/benchharness/Bench.h"

using namespace facebook::eden;

struct ProcessNameCacheFixture : benchmark::Fixture {
  ProcessNameCache processNameCache;
};

/**
 * A high but realistic amount of contention.
 */
constexpr size_t kThreadCount = 4;

BENCHMARK_DEFINE_F(ProcessNameCacheFixture, add_self)(benchmark::State& state) {
  auto myPid = getpid();
  for (auto _ : state) {
    processNameCache.add(myPid);
  }
}

BENCHMARK_REGISTER_F(ProcessNameCacheFixture, add_self)->Threads(kThreadCount);

BENCHMARK_MAIN();
