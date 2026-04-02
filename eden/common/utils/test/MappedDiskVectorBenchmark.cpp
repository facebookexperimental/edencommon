/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifdef __linux__

#include "eden/common/utils/MappedDiskVector.h"

#include <benchmark/benchmark.h>
#include <folly/testing/TestUtil.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

using facebook::eden::MappedDiskVector;

struct Small {
  enum { VERSION = 100 };
  uint64_t val;
};

struct Realistic {
  enum { VERSION = 101 };
  uint64_t ino;
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint32_t padding;
  uint64_t atime;
  uint64_t ctime;
  uint64_t mtime;
};
// 48 bytes matches InodeTableEntry<InodeMetadata>: InodeNumber (8) +
// InodeMetadata (40)
static_assert(sizeof(Realistic) == 48);

template <typename T>
void BM_EmplaceBack(benchmark::State& state) {
  folly::test::TemporaryDirectory tmpDir{"mdv_bench_"};
  auto path = (tmpDir.path() / "test.mdv").string();
  int64_t totalItems = 0;

  for (auto _ : state) {
    state.PauseTiming();
    ::unlink(path.c_str());
    auto mdv = MappedDiskVector<T>::open(path);
    state.ResumeTiming();

    auto count = static_cast<size_t>(state.range(0));
    for (size_t i = 0; i < count; ++i) {
      mdv.emplace_back(T{});
    }
    benchmark::ClobberMemory();
    totalItems += count;
  }
  state.SetItemsProcessed(totalItems);
}

BENCHMARK(BM_EmplaceBack<Small>)
    ->Arg(1024)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_EmplaceBack<Realistic>)
    ->Arg(1024)
    ->Arg(10000)
    ->Arg(100000)
    ->Arg(1000000)
    ->Unit(benchmark::kMicrosecond);

#ifdef MADV_POPULATE_WRITE
void BM_Madvise_AlreadyFaulted(benchmark::State& state) {
  // Measure the raw cost of madvise(MADV_POPULATE_WRITE) on an
  // already-faulted page -- this is the per-page overhead of pre-faulting.
  const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
  void* page = mmap(
      nullptr,
      pageSize,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS,
      -1,
      0);
  if (page == MAP_FAILED) {
    state.SkipWithError("mmap failed");
    return;
  }
  // Fault the page by writing to it.
  *static_cast<volatile char*>(page) = 0;

  // Verify madvise works before benchmarking
  if (madvise(page, pageSize, MADV_POPULATE_WRITE) != 0) {
    state.SkipWithError("MADV_POPULATE_WRITE not supported");
    munmap(page, pageSize);
    return;
  }

  for (auto _ : state) {
    benchmark::DoNotOptimize(madvise(page, pageSize, MADV_POPULATE_WRITE));
  }
  munmap(page, pageSize);
}
BENCHMARK(BM_Madvise_AlreadyFaulted);
#endif

} // namespace

#endif // __linux__
