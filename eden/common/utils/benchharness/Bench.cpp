/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/benchharness/Bench.h"
#include <inttypes.h>
#include <stdio.h>
#include <time.h>

namespace facebook::eden {

uint64_t getTime() noexcept {
  timespec ts;
  // CLOCK_MONOTONIC is subject in NTP adjustments. CLOCK_MONOTONIC_RAW would be
  // better but these benchmarks are short and reading CLOCK_MONOTONIC takes 20
  // ns and CLOCK_MONOTONIC_RAW takes 130 ns.
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000000000 + ts.tv_nsec;
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

} // namespace facebook::eden
