/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/RefPtr.h"

#include <benchmark/benchmark.h>

#include <memory>

namespace {

using namespace facebook::eden;

struct Empty {};

struct Ref final : RefCounted {};

void make_unique_ptr(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::make_unique<Empty>());
  }
}
BENCHMARK(make_unique_ptr);

void make_shared_ptr(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(std::make_shared<Empty>());
  }
}
BENCHMARK(make_shared_ptr);

void make_ref_ptr(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(makeRefPtr<Ref>());
  }
}
BENCHMARK(make_ref_ptr);

void copy_shared_ptr(benchmark::State& state) {
  auto ptr = std::make_shared<Empty>();
  for (auto _ : state) {
    std::shared_ptr<Empty>{ptr};
  }
}
BENCHMARK(copy_shared_ptr);

void copy_ref_ptr(benchmark::State& state) {
  auto ptr = makeRefPtr<Ref>();
  for (auto _ : state) {
    ptr.copy();
  }
}
BENCHMARK(copy_ref_ptr);

} // namespace
