/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Bug.h"

#include <folly/ExceptionWrapper.h>
#include <folly/portability/GTest.h>
#include <folly/test/TestUtils.h>

using namespace facebook::eden;

namespace {
[[noreturn]] void buggyFunction() {
  EDEN_BUG() << "oh noes";
}

class ScopedEdenBugFatal {
 public:
  explicit ScopedEdenBugFatal(bool fatal) : previous_{isEdenBugFatal()} {
    setEdenBugFatal(fatal);
  }

  ~ScopedEdenBugFatal() {
    setEdenBugFatal(previous_);
  }

 private:
  bool previous_;
};
} // namespace

TEST(EdenBug, throws) {
  EdenBugDisabler noCrash;
  EXPECT_THROW_RE(buggyFunction(), std::runtime_error, "oh noes");
  EXPECT_THROW_RE(EDEN_BUG() << "doh", std::runtime_error, "doh");
}

TEST(EdenBug, toException) {
  EdenBugDisabler noCrash;
  auto ew = EDEN_BUG_EXCEPTION() << "whoops";
  EXPECT_THROW_RE(ew.throw_exception(), std::runtime_error, "whoops");
}

TEST(EdenBug, defaultsToBuildMode) {
  EXPECT_EQ(folly::kIsDebug, isEdenBugFatal());
}

TEST(EdenBug, nonFatalStillThrows) {
  ScopedEdenBugFatal notFatal{false};
  EXPECT_THROW_RE(
      EDEN_BUG() << "recoverable", std::runtime_error, "recoverable");
}

TEST(EdenBug, disablerWinsOverFatal) {
  ScopedEdenBugFatal fatal{true};
  EdenBugDisabler noCrash;
  EXPECT_THROW_RE(EDEN_BUG() << "suppressed", std::runtime_error, "suppressed");
}

TEST(EdenBugDeathTest, fatalWhenEnabledRegardlessOfBuildMode) {
  EXPECT_DEATH(
      {
        setEdenBugFatal(true);
        EDEN_BUG() << "planted";
      },
      "crashing due to EDEN_BUG");
}

TEST(EdenBug, countsEvenWhenNotFatal) {
  ScopedEdenBugFatal notFatal{false};
  const auto before = getEdenBugCount();

  EXPECT_THROW_RE(EDEN_BUG() << "counted", std::runtime_error, "counted");
  EXPECT_EQ(before + 1, getEdenBugCount());

  auto ew = EDEN_BUG_EXCEPTION() << "also counted";
  EXPECT_EQ(before + 2, getEdenBugCount());
}

TEST(EdenBugNonFatal, reportsWithoutThrowing) {
  ScopedEdenBugFatal notFatal{false};
  const auto before = getEdenBugCount();

  EDEN_BUG_NONFATAL() << "reported but not thrown";

  EXPECT_EQ(before + 1, getEdenBugCount());
}

TEST(EdenExpect, silentWhenTheConditionHolds) {
  ScopedEdenBugFatal notFatal{false};
  const auto before = getEdenBugCount();

  EDEN_EXPECT(1 + 1 == 2) << "never emitted";

  EXPECT_EQ(before, getEdenBugCount());
}

TEST(EdenExpect, reportsAndContinuesWhenTheConditionFails) {
  ScopedEdenBugFatal notFatal{false};
  const auto before = getEdenBugCount();

  bool reachedNextStatement = false;
  EDEN_EXPECT(1 == 2) << " context";
  reachedNextStatement = true;

  EXPECT_TRUE(reachedNextStatement);
  EXPECT_EQ(before + 1, getEdenBugCount());
}

TEST(EdenExpect, conditionIsEvaluatedInEveryBuild) {
  ScopedEdenBugFatal notFatal{false};
  int evaluations = 0;

  EDEN_EXPECT([&] {
    ++evaluations;
    return true;
  }());

  EXPECT_EQ(1, evaluations);
}

TEST(EdenExpectDeathTest, fatalWhenEnabled) {
  EXPECT_DEATH(
      {
        setEdenBugFatal(true);
        EDEN_EXPECT(1 == 2) << "planted";
      },
      "crashing due to EDEN_BUG");
}
