/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Utf8.h"

#include <folly/Unicode.h>

namespace facebook::eden {

std::string ensureValidUtf8(folly::ByteRange str) {
  std::string output;
  output.reserve(str.size());
  const unsigned char* begin = str.begin();
  const unsigned char* const end = str.end();
  while (begin != end) {
    folly::appendCodePointToUtf8(
        folly::utf8ToCodePoint(begin, end, true), output);
  }
  return output;
}

} // namespace facebook::eden
