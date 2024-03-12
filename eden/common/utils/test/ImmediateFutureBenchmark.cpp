/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/benchharness/Bench.h"

#include <folly/CPortability.h>

#include "eden/common/utils/ImmediateFuture.h"

namespace {

using namespace facebook::eden;

void ImmediateFuture_thenValue_with_int(benchmark::State& state) {
  ImmediateFuture<uint64_t> fut = 0;

  for (auto _ : state) {
    auto newFut = std::move(fut).thenValue([](uint64_t v) { return v + 1; });
    fut = std::move(newFut);
  }
  state.SetItemsProcessed(std::move(fut).get());
}
BENCHMARK(ImmediateFuture_thenValue_with_int);

// One byte storage, but every ctor and dtor is a function call.
struct ExpensiveMove {
  static volatile uint64_t count;

  FOLLY_NOINLINE ExpensiveMove() {
    count = count + 1;
  }
  FOLLY_NOINLINE ~ExpensiveMove() {
    count = count + 1;
  }
  FOLLY_NOINLINE ExpensiveMove(ExpensiveMove&&) noexcept {
    count = count + 1;
  }

  ExpensiveMove& operator=(ExpensiveMove&&) noexcept = default;
};

volatile uint64_t ExpensiveMove::count;

void ImmediateFuture_move_with_expensive_move(benchmark::State& state) {
  ImmediateFuture<ExpensiveMove> fut = ExpensiveMove{};
  uint64_t processed = 0;
  for (auto _ : state) {
    // Move construction.
    ImmediateFuture<ExpensiveMove> newFut{std::move(fut)};
    // Move assignment.
    fut = std::move(newFut);
    processed++;
  }
  benchmark::DoNotOptimize(fut);
  state.SetItemsProcessed(processed);
}
BENCHMARK(ImmediateFuture_move_with_expensive_move);

void ImmediateFuture_thenValue_with_exc(benchmark::State& state) {
  ImmediateFuture<uint64_t> fut{folly::Try<uint64_t>{std::logic_error("Foo")}};

  uint64_t processed = 0;
  for (auto _ : state) {
    auto newFut = std::move(fut).thenValue([](uint64_t v) { return v + 1; });
    fut = std::move(newFut);
    processed++;
  }
  benchmark::DoNotOptimize(fut);
  state.SetItemsProcessed(processed);
}
BENCHMARK(ImmediateFuture_thenValue_with_exc);

void folly_Future_thenValue_with_int(benchmark::State& state) {
  folly::Future<int> fut{0};
  for (auto _ : state) {
    auto newFut = std::move(fut).thenValue([](int v) { return v + 1; });
    fut = std::move(newFut);
  }
  state.SetItemsProcessed(std::move(fut).get());
}
BENCHMARK(folly_Future_thenValue_with_int);

} // namespace
