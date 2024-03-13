/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <benchmark/benchmark.h>
#include <folly/init/Init.h>
#include <folly/synchronization/test/Barrier.h>

#include "eden/common/telemetry/Tracing.h"
#include "eden/common/utils/benchharness/Bench.h"

using namespace facebook::eden;

static void Tracer_repeatedly_create_trace_points(benchmark::State& state) {
  enableTracing();
  for (auto _ : state) {
    TraceBlock block{"foo"};
  }
}
BENCHMARK(Tracer_repeatedly_create_trace_points);

static void Tracer_repeatedly_create_trace_points_from_multiple_threads(
    benchmark::State& state) {
  enableTracing();

  for (auto _ : state) {
    TraceBlock block{"foo"};
  }
}
BENCHMARK(Tracer_repeatedly_create_trace_points_from_multiple_threads)
    ->Threads(8);

static void Tracer_repeatedly_create_trace_points_disabled(
    benchmark::State& state) {
  disableTracing();
  for (auto _ : state) {
    TraceBlock block{"foo"};
  }
}
BENCHMARK(Tracer_repeatedly_create_trace_points_disabled);

BENCHMARK_MAIN();
