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

struct TypelessTestLogEvent : public TypelessTestEvent {
  std::string str;
  int number = 0;

  TypelessTestLogEvent(std::string str, int number)
      : str(std::move(str)), number(number) {}

  void populate(DynamicEvent& event) const override {
    event.addString("str", str);
    event.addInt("number", number);
  }
};

struct VectorTestLogEvent : public TestEvent {
  std::vector<std::string> strvec;

  explicit VectorTestLogEvent(std::vector<std::string> strvec)
      : strvec(std::move(strvec)) {}

  void populate(DynamicEvent& event) const override {
    event.addStringVec("strvec", strvec);
  }

  char const* getType() const override {
    return "vector_test_event";
  }
};

struct SetTestLogEvent : public TestEvent {
  std::unordered_set<std::string> strset;

  explicit SetTestLogEvent(std::unordered_set<std::string> strset)
      : strset(std::move(strset)) {}

  void populate(DynamicEvent& event) const override {
    event.addStringSet("strset", strset);
  }

  char const* getType() const override {
    return "set_test_event";
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

TEST_F(
    ScubaStructuredLoggerTest,
    typeless_json_doesnt_contain_type_at_top_level) {
  logger.logEvent(TypelessTestLogEvent{"different name", 12});
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
          "str", "user", "host", "os", "osver", "system_architecture"));
#else
  EXPECT_THAT(
      keysOf(normals),
      UnorderedElementsAre("str", "user", "host", "os", "osver"));
#endif
}

TEST_F(ScubaStructuredLoggerTest, empty_stringvec_test) {
  VectorTestLogEvent event({});
  logger.logEvent(event);
  EXPECT_EQ(1, scribe->lines.size());
  const auto& line = scribe->lines[0];
  auto doc = folly::parseJson(line);
  EXPECT_TRUE(doc.isObject());
  EXPECT_THAT(keysOf(doc), UnorderedElementsAre("int", "normal", "normvector"));

  auto normvecs = doc["normvector"];
  EXPECT_TRUE(normvecs.isObject());
  EXPECT_THAT(keysOf(normvecs), UnorderedElementsAre("strvec"));
  EXPECT_TRUE(normvecs["strvec"].empty());
}

TEST_F(ScubaStructuredLoggerTest, stringvec_test) {
  VectorTestLogEvent event({"a", "b", "c"});
  logger.logEvent(event);
  EXPECT_EQ(1, scribe->lines.size());
  const auto& line = scribe->lines[0];
  auto doc = folly::parseJson(line);
  EXPECT_TRUE(doc.isObject());
  EXPECT_THAT(keysOf(doc), UnorderedElementsAre("int", "normal", "normvector"));

  auto normvecs = doc["normvector"];
  EXPECT_TRUE(normvecs.isObject());
  EXPECT_THAT(keysOf(normvecs), UnorderedElementsAre("strvec"));
  EXPECT_FALSE(normvecs["strvec"].empty());
  EXPECT_EQ(normvecs["strvec"][0].asString(), "a");
  EXPECT_EQ(normvecs["strvec"][1].asString(), "b");
  EXPECT_EQ(normvecs["strvec"][2].asString(), "c");
}

TEST_F(ScubaStructuredLoggerTest, empty_stringset_test) {
  SetTestLogEvent event({});
  logger.logEvent(event);
  EXPECT_EQ(1, scribe->lines.size());
  const auto& line = scribe->lines[0];
  auto doc = folly::parseJson(line);
  EXPECT_TRUE(doc.isObject());
  EXPECT_THAT(keysOf(doc), UnorderedElementsAre("int", "normal", "tags"));

  auto tags = doc["tags"];
  EXPECT_TRUE(tags.isObject());
  EXPECT_THAT(keysOf(tags), UnorderedElementsAre("strset"));
  EXPECT_TRUE(tags["strset"].empty());
}

TEST_F(ScubaStructuredLoggerTest, stringset_test) {
  SetTestLogEvent event({"a", "b", "c"});
  logger.logEvent(event);
  EXPECT_EQ(1, scribe->lines.size());
  const auto& line = scribe->lines[0];
  auto doc = folly::parseJson(line);
  EXPECT_TRUE(doc.isObject());
  EXPECT_THAT(keysOf(doc), UnorderedElementsAre("int", "normal", "tags"));

  auto tags = doc["tags"];
  EXPECT_TRUE(tags.isObject());
  EXPECT_THAT(keysOf(tags), UnorderedElementsAre("strset"));
  EXPECT_FALSE(tags["strset"].empty());
  EXPECT_EQ(tags["strset"].size(), 3);
  std::unordered_set<std::string> values;
  for (const auto& item : tags["strset"]) {
    values.insert(item.asString());
  }
  EXPECT_THAT(values, UnorderedElementsAre("c", "b", "a"));
}
