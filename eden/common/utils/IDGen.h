/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <stdint.h>

namespace facebook::eden {

/**
 * Very efficiently returns a new uint64_t unique to this process. Amortizes
 * the cost of synchronizing threads across many ID allocations.
 *
 * All returned IDs are nonzero.
 *
 * TODO: It might be beneficial to add a parameter to request more than one
 * unique ID at a time, though such an API would make it possible to exhaust
 * the range of a 64-bit integer.
 */
uint64_t generateUniqueID() noexcept;

} // namespace facebook::eden
