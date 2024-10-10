/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/telemetry/ScubaStructuredLogger.h"

#include <folly/json/json.h>
#include <folly/portability/GMock.h>
#include <folly/portability/GTest.h>

#include "eden/common/telemetry/ScribeLogger.h"

using namespace facebook::eden;
using namespace testing;

namespace {

struct TestScribeLogger : public ScribeLogger {
  std::vector<std::string> lines;

  void log(std::string line) override {
    lines.emplace_back(std::move(line));
  }
};

struct TestLogEvent : public TestEvent {
  std::string str;
  int number = 0;

  TestLogEvent(std::string str, int number)
      : str(std::move(str)), number(number) {}

  void populate(DynamicEvent& event) const override {
    event.addString("str", str);
    event.addInt("number", number);
  }

  char const* getType() const override {
    return "test_event";
  }
};

struct ScubaStructuredLoggerTest : public ::testing::Test {
  std::shared_ptr<TestScribeLogger> scribe{
      std::make_shared<TestScribeLogger>()};
  ScubaStructuredLogger logger{
      scribe,
      SessionInfo{},
  };
};

} // namespace

TEST_F(ScubaStructuredLoggerTest, json_is_written_in_one_line) {
  logger.logEvent(TestLogEvent{"name", 10});
  EXPECT_EQ(1, scribe->lines.size());
  const auto& line = scribe->lines[0];
  auto index = line.find('\n');
  EXPECT_EQ(std::string::npos, index);
}

std::vector<std::string> keysOf(const folly::dynamic& d) {
  std::vector<std::string> rv;
  for (const auto& key : d.keys()) {
    rv.push_back(key.asString());
  }
  return rv;
}

TEST_F(ScubaStructuredLoggerTest, json_contains_types_at_top_level_and_values) {
  logger.logEvent(TestLogEvent{"name", 10});
  EXPECT_EQ(1, scribe->lines.size());
  const auto& line = scribe->lines[0];
  auto doc = folly::parseJson(line);
  EXPECT_TRUE(doc.isObject());
  EXPECT_THAT(keysOf(doc), UnorderedElementsAre("int", "normal"));

  auto ints = doc["int"];
  EXPECT_TRUE(ints.isObject());
  EXPECT_THAT(
      keysOf(ints), UnorderedElementsAre("time", "number", "session_id"));

  auto normals = doc["normal"];
  EXPECT_TRUE(normals.isObject());
#if defined(__APPLE__)
  EXPECT_THAT(
      keysOf(normals),
      UnorderedElementsAre(
          "str", "user", "host", "type", "os", "osver", "system_architecture"));
#else
  EXPECT_THAT(
      keysOf(normals),
      UnorderedElementsAre("str", "user", "host", "type", "os", "osver"));
#endif
}
