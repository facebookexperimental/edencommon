/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <benchmark/benchmark.h>
#include <fmt/core.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "eden/common/utils/PathMap.h"

namespace {

using namespace facebook::eden;

/**
 * PathMap is the storage for a directory's entries, so these benchmarks
 * cover the shapes directories actually take: a handful of entries, a few
 * hundred, and the very large generated directories that exist in large
 * repositories.
 *
 * Insertion and removal are measured in three orders, because the cost of
 * a sorted-vector representation depends entirely on where in the vector
 * an operation lands:
 *   - sorted:   ascending key order, which is what readdir and checkout
 *               produce, and which touches the end (insert) or the
 *               beginning (erase) of the map.
 *   - reverse:  descending key order, the mirror image.
 *   - random:   a shuffle, landing in the middle on average.
 */

constexpr size_t kSmall = 8;
constexpr size_t kMedium = 512;
constexpr size_t kLarge = 20000;

enum class Order { Sorted, Reverse, Random };

using Clock = std::chrono::steady_clock;

double elapsedSeconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

/** Zero-padded names, so lexicographic order matches numeric order. */
std::vector<PathComponent> makeNames(size_t count) {
  std::vector<PathComponent> names;
  names.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    names.emplace_back(fmt::format("file-{:08d}.txt", i));
  }
  return names;
}

std::vector<size_t> makeOrder(size_t count, Order order) {
  std::vector<size_t> indexes(count);
  for (size_t i = 0; i < count; ++i) {
    indexes[i] = i;
  }
  switch (order) {
    case Order::Sorted:
      break;
    case Order::Reverse:
      std::reverse(indexes.begin(), indexes.end());
      break;
    case Order::Random:
      // Fixed seed: the point is to compare representations, not to
      // sample different shuffles.
      std::shuffle(indexes.begin(), indexes.end(), std::mt19937{0x5eed});
      break;
  }
  return indexes;
}

PathMap<int> makeMap(const std::vector<PathComponent>& names) {
  PathMap<int> map{CaseSensitivity::Sensitive};
  for (size_t i = 0; i < names.size(); ++i) {
    map.insert(std::make_pair(names[i], static_cast<int>(i)));
  }
  return map;
}

/** Build a map of `count` entries, inserting in the given order. */
void insertBench(benchmark::State& state, Order order) {
  const auto count = static_cast<size_t>(state.range(0));
  const auto names = makeNames(count);
  const auto indexes = makeOrder(count, order);

  for (auto _ : state) {
    const auto start = Clock::now();
    PathMap<int> map{CaseSensitivity::Sensitive};
    for (size_t i : indexes) {
      map.insert(std::make_pair(names[i], static_cast<int>(i)));
    }
    benchmark::DoNotOptimize(map);
    state.SetIterationTime(elapsedSeconds(start));
  }
  state.SetItemsProcessed(state.iterations() * count);
}

/** Empty a full map of `count` entries, erasing in the given order. */
void eraseBench(benchmark::State& state, Order order) {
  const auto count = static_cast<size_t>(state.range(0));
  const auto names = makeNames(count);
  const auto indexes = makeOrder(count, order);

  for (auto _ : state) {
    auto map = makeMap(names);

    const auto start = Clock::now();
    for (size_t i : indexes) {
      map.erase(names[i].piece());
    }
    benchmark::DoNotOptimize(map);
    state.SetIterationTime(elapsedSeconds(start));
  }
  state.SetItemsProcessed(state.iterations() * count);
}

/** Look up every entry of a full map. */
void findBench(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  const auto names = makeNames(count);
  const auto map = makeMap(names);

  for (auto _ : state) {
    for (const auto& name : names) {
      benchmark::DoNotOptimize(map.find(name.piece()));
    }
  }
  state.SetItemsProcessed(state.iterations() * count);
}

/** Walk every entry in order. */
void iterateBench(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  const auto map = makeMap(makeNames(count));

  for (auto _ : state) {
    for (const auto& entry : map) {
      benchmark::DoNotOptimize(entry.second);
    }
  }
  state.SetItemsProcessed(state.iterations() * count);
}

/** Names that interleave between the base map's names, so the extra
 * inserts land throughout the map rather than at one end. A single extra
 * name lands in the middle; the trailing index keeps the names distinct
 * when they outnumber the base entries. */
std::vector<PathComponent> makeExtraNames(size_t count, size_t extra) {
  std::vector<PathComponent> names;
  names.reserve(extra);
  for (size_t j = 0; j < extra; ++j) {
    names.emplace_back(
        fmt::format(
            "file-{:08d}.txt.new{:04d}", (2 * j + 1) * count / (2 * extra), j));
  }
  return names;
}

/** Walk every entry of a map that has had `extra` out-of-order inserts
 * since it was last fully sorted. */
void iterateAfterInsertsBench(benchmark::State& state, size_t extra) {
  const auto count = static_cast<size_t>(state.range(0));
  auto map = makeMap(makeNames(count));
  for (const auto& name : makeExtraNames(count, extra)) {
    map.insert(std::make_pair(name, 0));
  }

  for (auto _ : state) {
    for (const auto& entry : map) {
      benchmark::DoNotOptimize(entry.second);
    }
  }
  state.SetItemsProcessed(state.iterations() * (count + extra));
}

void iterateAfterOneInsert(benchmark::State& state) {
  iterateAfterInsertsBench(state, 1);
}

/** As many inserts as the map absorbs before it compacts. */
void iterateAfterManyInserts(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  iterateAfterInsertsBench(state, count >= kLarge ? 141 : 32);
}

/** Walk every entry of a map that has had a quarter of its entries erased
 * in random order since it was last fully sorted. */
void iterateAfterErases(benchmark::State& state) {
  const auto count = static_cast<size_t>(state.range(0));
  const auto names = makeNames(count);
  auto map = makeMap(names);
  const auto indexes = makeOrder(count, Order::Random);
  for (size_t i = 0; i < count / 4; ++i) {
    map.erase(names[indexes[i]].piece());
  }

  for (auto _ : state) {
    for (const auto& entry : map) {
      benchmark::DoNotOptimize(entry.second);
    }
  }
  state.SetItemsProcessed(state.iterations() * (count - count / 4));
}

void insertSorted(benchmark::State& state) {
  insertBench(state, Order::Sorted);
}
void insertReverse(benchmark::State& state) {
  insertBench(state, Order::Reverse);
}
void insertRandom(benchmark::State& state) {
  insertBench(state, Order::Random);
}
void eraseSorted(benchmark::State& state) {
  eraseBench(state, Order::Sorted);
}
void eraseReverse(benchmark::State& state) {
  eraseBench(state, Order::Reverse);
}
void eraseRandom(benchmark::State& state) {
  eraseBench(state, Order::Random);
}

BENCHMARK(insertSorted)
    ->Arg(kSmall)
    ->Arg(kMedium)
    ->Arg(kLarge)
    ->UseManualTime();
BENCHMARK(insertReverse)
    ->Arg(kSmall)
    ->Arg(kMedium)
    ->Arg(kLarge)
    ->UseManualTime();
BENCHMARK(insertRandom)
    ->Arg(kSmall)
    ->Arg(kMedium)
    ->Arg(kLarge)
    ->UseManualTime();
BENCHMARK(eraseSorted)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge)->UseManualTime();
BENCHMARK(eraseReverse)
    ->Arg(kSmall)
    ->Arg(kMedium)
    ->Arg(kLarge)
    ->UseManualTime();
BENCHMARK(eraseRandom)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge)->UseManualTime();
BENCHMARK(findBench)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge);
BENCHMARK(iterateBench)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge);
BENCHMARK(iterateAfterOneInsert)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge);
BENCHMARK(iterateAfterManyInserts)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge);
BENCHMARK(iterateAfterErases)->Arg(kSmall)->Arg(kMedium)->Arg(kLarge);

} // namespace
