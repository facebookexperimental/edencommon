/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/benchharness/Bench.h"
#include <fmt/core.h>
#include <folly/ExceptionString.h>
#include <folly/init/Init.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <chrono>

namespace facebook::eden {

uint64_t getTime() noexcept {
#ifdef _WIN32
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
#else
  timespec ts;
  // CLOCK_MONOTONIC is subject in NTP adjustments. CLOCK_MONOTONIC_RAW would be
  // better but these benchmarks are short and reading CLOCK_MONOTONIC takes 20
  // ns and CLOCK_MONOTONIC_RAW takes 130 ns.
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

StatAccumulator measureClockOverhead() noexcept {
  constexpr int N = 10000;

  StatAccumulator accum;

  uint64_t last = getTime();
  for (int i = 0; i < N; ++i) {
    uint64_t next = getTime();
    uint64_t elapsed = next - last;
    accum.add(elapsed);
    last = next;
  }

  return accum;
}

int runBenchmarkMain(int argc, char** argv) {
  ::benchmark::Initialize(&argc, argv);
  folly::init(&argc, &argv);
  if (::benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }
  try {
    ::benchmark::RunSpecifiedBenchmarks();
  } catch (const std::exception& e) {
    fmt::print(
        stderr,
        "uncaught exception from benchmarks: {}\n",
        folly::exceptionStr(e));
    throw;
  }
  return 0;
}

} // namespace facebook::eden
