/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Portability.h>

namespace facebook::eden {
enum class CaseSensitivity : bool {
  Insensitive = false,
  Sensitive = true,
};

constexpr CaseSensitivity kPathMapDefaultCaseSensitive =
    static_cast<CaseSensitivity>(folly::kIsLinux);
} // namespace facebook::eden
