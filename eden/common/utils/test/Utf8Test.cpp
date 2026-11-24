/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Utf8.h"

#include <cstring>
#include <memory>
#include <string>

#include <folly/portability/GTest.h>

using namespace facebook::eden;

namespace {
const folly::StringPiece kValidStrings[] = {
    "",
    "abcdef",
    "\0foo\n\0",
    reinterpret_cast<const char*>(u8"\u0080"), // 2 bytes
    reinterpret_cast<const char*>(u8"\u00A2"), // 2 bytes
    reinterpret_cast<const char*>(u8"\u0800"), // 3 bytes
    reinterpret_cast<const char*>(u8"\u0939"), // 3 bytes
    reinterpret_cast<const char*>(u8"\U00010348"), // 4 bytes
    reinterpret_cast<const char*>(u8"\U00040000"), // 4 bytes
};
}

TEST(Utf8Test, isValidUtf8) {
  for (auto str : kValidStrings) {
    EXPECT_TRUE(isValidUtf8(str));
  }

  EXPECT_FALSE(isValidUtf8("\xff"));
  // overlong
  EXPECT_FALSE(isValidUtf8("\xF0\x82\x82\xAC"));
  EXPECT_FALSE(isValidUtf8("\xA0prefix\xB0"));
}

TEST(Utf8Test, isValidUtf8RejectsTruncatedSequenceAtEndOfRange) {
  const folly::StringPiece kTruncated[] = {
      "\xC2",
      "\xE0\xA0",
      "\xF0\x90\x80",
  };
  for (auto str : kTruncated) {
    // Copy into an allocation of exactly the input's size, so that a read
    // past the end of the range is not masked by a NUL terminator.
    auto buffer = std::make_unique<char[]>(str.size());
    std::memcpy(buffer.get(), str.data(), str.size());
    EXPECT_FALSE(isValidUtf8(folly::StringPiece{buffer.get(), str.size()}))
        << "truncated sequence of length " << str.size();
  }
}

TEST(Utf8Test, isValidUtf8RejectsTruncatedSequenceBeforeContinuationByte) {
  // The range ends in a truncated sequence, and the byte just past the range
  // is a continuation byte.
  std::string str(40, 'a');
  str[38] = '\xC2';
  str[39] = '\xA0';
  EXPECT_FALSE(isValidUtf8(folly::StringPiece{str.data(), 39}));
}

TEST(Utf8String, ensureValidUtf8) {
  for (auto str : kValidStrings) {
    EXPECT_EQ(str, ensureValidUtf8(str));
  }

  EXPECT_EQ(reinterpret_cast<const char*>(u8"\uFFFD"), ensureValidUtf8("\xff"));
  // overlong
  EXPECT_EQ(
      reinterpret_cast<const char*>(u8"foo\uFFFD\uFFFD\uFFFD\uFFFDbar"),
      ensureValidUtf8(
          "foo\xF0\x82\x82\xAC"
          "bar"));
  EXPECT_EQ(
      reinterpret_cast<const char*>(u8"\uFFFDprefix\uFFFD"),
      ensureValidUtf8("\xA0prefix\xB0"));
}
