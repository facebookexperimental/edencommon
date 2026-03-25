# FaultInjector

Deterministic fault injection for EdenFS tests. Allows controlling the ordering and outcomes of async operations to test race conditions, cancellation, error handling, and lifecycle management.

## Architecture

- **Implementation**: `eden/common/utils/FaultInjector.h` / `FaultInjector.cpp`
- **Singleton**: One `FaultInjector` per `ServerState`, constructed with an `enabled` flag
- **When disabled**: `UNLIKELY` branch short-circuit, near-zero overhead in production
- **Matching**: Uses `boost::regex_match` against `(keyClass, keyValue)` tuples; first match wins

## Seven Fault Types

| Type | Behavior |
|------|----------|
| **Block** | Suspends via `Promise/Future` until `unblock()` is called |
| **BlockWithCancel** | Polls `CancellationToken`, throws `OperationCancelled` when cancelled |
| **Delay** | Sleeps for a fixed duration via `folly::futures::sleep` |
| **DelayedError** | Sleeps for a duration, then throws an exception |
| **Error** | Throws immediately via `exception_wrapper` |
| **Kill** | Calls `abort()` -- terminates the process |
| **Noop** | Matches but does nothing (prevents lower-priority faults from triggering) |

**Choosing a fault type:**
- **Block** — Test race conditions: "what happens when X is in progress while Y occurs?"
- **BlockWithCancel** — Test cancellation: "can this operation be properly cancelled?"
- **Error** — Test error handling: "what happens when the backing store fails?"
- **Delay** — Test timeout behavior without manual unblock
- **Kill** — Test ungraceful termination recovery
- **Noop** — Let first N calls succeed, then fail (use with `count` parameter on a subsequent fault)

## Finding Fault Injection Check Sites

To find available `keyClass` strings, search the EdenFS daemon code for fault check calls:
```bash
fbgr 'checkAsync\("' -f 'eden/fs/.*\.cpp$'
```
The first string argument to `check()`/`checkAsync()` is the `keyClass`. The second is the `keyValue` (usually a mount path or empty string) that your `keyValueRegex` matches against.

**WATCH OUT:**
- `removeFault()` and `unblock()` are SEPARATE operations — you must call both to cleanly release a blocked operation
- `waitUntilBlocked()` requires an explicit timeout parameter — always pass one (e.g., `10s`)
- Fault matching uses `boost::regex_match` (FULL string match), not search — your regex must match the entire keyValue
- This is the canonical FaultInjector reference. Other documentation files reference this.
- Coroutines + fault injection: don't `co_await` and interact from the same context. Use `CO_TEST_F` + `collectAll` pattern (see below).

## C++ Fault Injection Patterns

**Block-Wait-Act-Unblock (deterministic ordering):**
```cpp
auto& faultInjector = testMount.getServerState()->getFaultInjector();
faultInjector.injectBlock("waitForPendingWrites", ".*");

auto bgThread = std::thread([&] {
  rawMount->waitForPendingWrites().get(kTimeout);
});

// Wait until the operation is actually blocked
ASSERT_TRUE(faultInjector.waitUntilBlocked("waitForPendingWrites", 10s));

// Perform action while other operation is blocked
fuse->close();
mount.reset();

// Release the blocked operation
faultInjector.removeFault("waitForPendingWrites", ".*");
faultInjector.unblock("waitForPendingWrites", ".*");
bgThread.join();
```

**Block to test lifetime/use-after-free:**
```cpp
faultInjector.injectBlock("SaplingBackingStore::getRootTree", ".*");
auto future = queuedBackingStore->getRootTree(commit1, context);
EXPECT_FALSE(future.isReady());

queuedBackingStore.reset();  // destroy owner while op blocked
EXPECT_FALSE(weak.expired());  // should still be alive (shared_from_this)

faultInjector.unblock("SaplingBackingStore::getRootTree", ".*");
std::move(future).getTry(kTestTimeout);  // should not crash
```

**BlockWithCancel for cancellation testing:**
```cpp
folly::CancellationSource cancelSource;
faultInjector.injectBlockWithCancel(
    "checkout", ".*", cancelSource.getToken(), 5000ms, 0);

auto checkoutFuture = mount->checkout(
    rootInode, RootId{"2"}, ctx, __func__).semi().via(executor);
testMount.drainServerExecutor();
EXPECT_FALSE(checkoutFuture.isReady());

cancelSource.requestCancellation();
while (!checkoutFuture.isReady()) {
  testMount.drainServerExecutor();
}
EXPECT_TRUE(checkoutFuture.isReady());
EXPECT_ANY_THROW(std::move(checkoutFuture).get());
```

## Coroutine Fault Injection (CO_TEST_F + collectAll)

Fault injection with coroutines requires a different pattern than futures. You can't `co_await` a faulted coroutine and interact with it while suspended from the same context. Instead, use `collectAll` to run two tasks concurrently:

1. **Task 1**: Wraps the coroutine under test (converts `now_task` to `Task` via `co_invoke`)
2. **Task 2**: Yields, verifies state while task 1 is suspended, then unblocks

```cpp
#include <folly/coro/Collect.h>
#include <folly/coro/GtestHelpers.h>
#include <folly/coro/Task.h>

CO_TEST_F(MyTestFixture, coFaultInjection) {
  auto weak = std::weak_ptr<MyObject>(sharedObject);
  faultInjector.injectBlock("MyObject::doWork", ".*");

  // Task 1: call the coroutine under test.
  // co_invoke wraps now_task -> Task so collectAll can schedule it.
  auto workTask = folly::coro::co_invoke(
      [&]() -> folly::coro::Task<Result> {
        co_return co_await sharedObject->co_doWork(args...);
      });

  // Task 2: runs after task 1 suspends at the fault injection point.
  auto verifyTask =
      folly::coro::co_invoke([&]() -> folly::coro::Task<void> {
        // Yield to let task 1 start and suspend.
        co_await folly::coro::co_reschedule_on_current_executor;

        // Confirm task 1 is blocked (use 0ms — it should already be blocked).
        EXPECT_TRUE(faultInjector.waitUntilBlocked("MyObject::doWork", 0ms));

        // Interact with state while task 1 is suspended...
        sharedObject.reset();
        EXPECT_FALSE(weak.expired());  // alive via shared_from_this()

        // Release.
        faultInjector.removeFault("MyObject::doWork", ".*");
        faultInjector.unblock("MyObject::doWork", ".*");
      });

  auto [result, _] = co_await folly::coro::collectAll(
      std::move(workTask), std::move(verifyTask));
  EXPECT_NE(result.value, nullptr);
}
```

**Key points:**
- `CO_TEST_F` provides the executor — no need to create a `CPUThreadPoolExecutor`
- `co_reschedule_on_current_executor` yields so task 1 can run first and hit the fault
- `waitUntilBlocked` with `0ms` timeout confirms task 1 is suspended (it should be by the time task 2 runs after the yield)
- `co_invoke` is needed because `collectAll` requires `Task<T>`, not `now_task`
- See D98081895 for a real example in `SaplingBackingStoreTest.cpp`

**Note:** For using fault injection in Python integration tests (thrift-based `FaultDefinition`, `wait_on_fault_hit`), see the example in `eden/integration/README.md`.
