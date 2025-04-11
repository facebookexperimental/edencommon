/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Range.h>

#include "eden/common/utils/Throw.h"
#include "eden/fs/model/Hash.h"

namespace facebook::eden {

// bit enum representing possible hash types that could be used
// 8 should be more than enough for now
// but still this enum is represented as a variant
enum class HashType : uint8_t {
  SHA1 = (1 << 0),
  BLAKE3 = (1 << 1),
};

FOLLY_ALWAYS_INLINE void
write(const uint8_t* src, size_t len, uint8_t* dest, size_t& off) {
  memcpy(dest + off, src, len);
  off += len;
}

/**
 * Read the hash from a byte range to the destination Hash buffer.
 *
 * This is currently used by BlobAuxData and TreeAuxData.
 */
template <size_t SIZE>
void readAuxDataHash(
    const ObjectId& id,
    folly::ByteRange& bytes,
    Hash<SIZE>& hash) {
  if (bytes.size() < SIZE) {
    throwf<std::invalid_argument>(
        "auxData for {} had unexpected size {}. Could not deserialize the hash of size {}.",
        id,
        bytes.size(),
        SIZE);
  }

  auto mutableBytes = hash.mutableBytes();
  memcpy(mutableBytes.data(), bytes.data(), mutableBytes.size());
  bytes.advance(mutableBytes.size());
}

} // namespace facebook::eden
