/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdint>

namespace facebook::eden {

/**
 * Returns a random, process-stable positive integer in the range of [0,
 * UINT32_MAX]
 */
uint32_t getSessionId();

} // namespace facebook::eden
