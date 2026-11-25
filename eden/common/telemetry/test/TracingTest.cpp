/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GTest.h>

#include <atomic>
#include <barrier>
#include <cstdlib>
#include <memory>
#include <set>
#include <thread>
#include <vector>

#include <folly/executors/ThreadedExecutor.h>
#include <folly/futures/Future.h>
#include <folly/io/async/Request.h>

#include "eden/common/telemetry/Tracing.h"

using namespace facebook::eden;

namespace {
void ensureValidTracePoint(const CompactTracePoint& point) {
  EXPECT_NE(point.timestamp.count(), 0);
  EXPECT_NE(point.traceId, 0u);
  EXPECT_NE(point.blockId, 0u);
  if (point.start) {
    EXPECT_FALSE(point.stop);
    EXPECT_NE(point.name, nullptr);
  }
  if (point.stop) {
    EXPECT_FALSE(point.start);
    EXPECT_EQ(point.name, nullptr);
  }
}
void ensureValidTracePoints(
    const std::vector<CompactTracePoint>& points,
    size_t num) {
  ASSERT_EQ(points.size(), num);
  for (const auto& point : points) {
    ensureValidTracePoint(point);
  }
}

void ensureValidBlock() {
  auto points = getAllTracepoints();
  ensureValidTracePoints(points, 2);
  EXPECT_TRUE(points[0].start);
  EXPECT_TRUE(points[1].stop);
  EXPECT_EQ(points[0].traceId, points[1].traceId);
  EXPECT_EQ(points[0].blockId, points[1].blockId);
  EXPECT_STREQ(points[0].name, "my_block");
}
} // namespace

TEST(Tracing, records_block) {
  enableTracing();
  {
    TraceBlock block{"my_block"};
  }

  ensureValidBlock();
}

TEST(Tracing, records_block_from_exited_thread) {
  // Zeroes out all pending tracepoints from previous tests.
  (void)getAllTracepoints();

  enableTracing();
  std::thread worker{[] { TraceBlock block{"my_block"}; }};
  worker.join();

  ensureValidBlock();
}

TEST(Tracing, records_block_explicit_close) {
  enableTracing();
  {
    TraceBlock block{"my_block"};
    block.close();

    ensureValidBlock();
  }
}

TEST(Tracing, records_block_explicit_close_and_destroy) {
  enableTracing();
  {
    TraceBlock block{"my_block"};
    block.close();
  }

  ensureValidBlock();
}

TEST(Tracing, records_nested_block) {
  enableTracing();
  {
    TraceBlock block{"my_block"};
    TraceBlock block2{"my_block2"};
  }

  auto points = getAllTracepoints();
  ensureValidTracePoints(points, 4);
  EXPECT_TRUE(points[0].start);
  EXPECT_TRUE(points[1].start);
  EXPECT_TRUE(points[2].stop);
  EXPECT_TRUE(points[3].stop);
  for (auto i = 1; i < 4; ++i) {
    EXPECT_EQ(points[0].traceId, points[i].traceId);
  }
  EXPECT_EQ(points[0].blockId, points[3].blockId);
  EXPECT_EQ(points[1].blockId, points[2].blockId);
  EXPECT_NE(points[0].blockId, points[1].blockId);
  EXPECT_STREQ(points[0].name, "my_block");
  EXPECT_STREQ(points[1].name, "my_block2");
}

// Two threads that construct the first TraceBlock of a shared RequestContext
// at the same time must not lose its trace data, and must agree on one trace
// id. The race window is small, so the test repeats it with fresh contexts, in
// batches that fit in each thread's trace point buffer.
TEST(Tracing, first_blocks_on_two_threads_in_one_request_context) {
  (void)getAllTracepoints();
  enableTracing();
  constexpr size_t kBatches = 12;
  constexpr size_t kBatchSize = 4000;
  for (size_t batch = 0; batch < kBatches; ++batch) {
    std::vector<std::shared_ptr<folly::RequestContext>> contexts;
    contexts.reserve(kBatchSize);
    for (size_t i = 0; i < kBatchSize; ++i) {
      contexts.push_back(std::make_shared<folly::RequestContext>());
    }
    std::atomic<size_t> started{0};
    std::atomic<size_t> finished{0};
    auto work = [&] {
      for (size_t i = 0; i < kBatchSize; ++i) {
        while (started.load(std::memory_order_acquire) <= i) {
        }
        {
          folly::RequestContextScopeGuard guard{contexts[i]};
          TraceBlock block{"racer"};
        }
        finished.fetch_add(1, std::memory_order_acq_rel);
      }
    };
    std::thread a{work};
    std::thread b{work};
    for (size_t i = 0; i < kBatchSize; ++i) {
      while (finished.load(std::memory_order_acquire) < 2 * i) {
      }
      started.store(i + 1, std::memory_order_release);
    }
    a.join();
    b.join();

    // Each context gets exactly one trace id, and ids are unique across
    // contexts.
    std::set<uint64_t> traceIds;
    for (const auto& point : getAllTracepoints()) {
      traceIds.insert(point.traceId);
    }
    ASSERT_EQ(kBatchSize, traceIds.size());
  }
}

TEST(Tracing, records_traceId_across_futures) {
  enableTracing();
  TraceBlock block{"my_block"};
  folly::ThreadedExecutor executor;
  auto fut = folly::makeFuture(42).via(&executor).thenValue(
      [b = std::move(block)](auto /* unused */) {});
  fut.wait();

  ensureValidBlock();
}

TEST(Tracing, records_traceId_across_futures_no_early_tracepoint) {
  enableTracing();
  TraceBlock block{"my_block"};
  folly::ThreadedExecutor executor;
  auto fut = folly::makeFuture(42).via(&executor).thenValue(
      [b = std::move(block)](auto /* unused */) {
        EXPECT_EQ(getAllTracepoints().size(), 1)
            << "The block's end tracepoint should not have been logged yet";
      });
  fut.wait();
}

// Threads that run in the same RequestContext share one TraceRequestData.
TEST(Tracing, blocks_on_two_threads_in_one_request_context) {
  (void)getAllTracepoints();
  enableTracing();
  auto ctx = std::make_shared<folly::RequestContext>();
  {
    folly::RequestContextScopeGuard guard{ctx};
    TraceBlock first{"first"};
  }
  std::barrier sync{2};
  auto work = [&] {
    folly::RequestContextScopeGuard guard{ctx};
    TraceBlock block{"worker"};
    sync.arrive_and_wait();
  };
  std::thread a{work};
  std::thread b{work};
  a.join();
  b.join();

  auto points = getAllTracepoints();
  ASSERT_EQ(6, points.size());
  for (const auto& point : points) {
    EXPECT_EQ(points[0].traceId, point.traceId);
  }
}

TEST(Tracing, does_not_record_if_disabled) {
  // Zeroes out all pending tracepoints from previous tests.
  (void)getAllTracepoints();

  disableTracing();
  {
    TraceBlock block{"my_block"};
  }
  auto points = getAllTracepoints();
  ASSERT_EQ(0, points.size());
}

TEST(TracingDeathTest, global_tracer_destruction_flushes_thread_local_points) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  // globalTracer is destroyed during static destruction, so the scenario has
  // to end its process. A thread that recorded points has exited, so the
  // global vector owns a heap buffer, and the main thread still holds
  // unflushed points when the process exits.
  EXPECT_EXIT(
      {
        enableTracing();
        std::thread worker{[] { TraceBlock block{"worker_block"}; }};
        worker.join();
        {
          TraceBlock block{"main_block"};
        }
        std::exit(0);
      },
      testing::ExitedWithCode(0),
      "");
}
