/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Memory.h"

#include <cstdio>
#include <cstdlib>

namespace facebook::eden {

void assertZeroBits(const void* memory, size_t size) {
  if (0 == size) {
    return;
  }
  auto* p = static_cast<const unsigned char*>(memory);
  if (p[0] || memcmp(p, p + 1, size - 1)) {
    fprintf(stderr, "unexpected nonzero bits: ");
    for (size_t i = 0; i < size; ++i) {
      fprintf(stderr, "%01x%01x", p[i] & 15, p[i] >> 4);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
    abort();
  }
}
} // namespace facebook::eden
