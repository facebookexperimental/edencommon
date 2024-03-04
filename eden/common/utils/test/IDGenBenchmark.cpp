/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/IDGen.h"

#include <benchmark/benchmark.h>

using namespace facebook::eden;

static void BM_generateUniqueID(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(generateUniqueID());
  }
}
BENCHMARK(BM_generateUniqueID);
