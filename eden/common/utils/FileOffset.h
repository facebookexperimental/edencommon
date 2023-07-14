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
 * Signed type representing a file offset. Corresponds to off_t on unix and
 * __int64 on Windows.
 *
 * We can't use off_t because it's int32_t on Windows's MSVCRT/UCRT.
 */
using FileOffset = int64_t;

// TODO: Introduce a FileSize type that is unsigned?

} // namespace facebook::eden
