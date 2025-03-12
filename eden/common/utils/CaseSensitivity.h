/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Portability.h>
#include <ostream>

namespace facebook::eden {
enum class CaseSensitivity : bool {
  Insensitive = false,
  Sensitive = true,
};

constexpr CaseSensitivity kPathMapDefaultCaseSensitive =
    static_cast<CaseSensitivity>(folly::kIsLinux);

inline std::ostream& operator<<(std::ostream& os, CaseSensitivity cs) {
  return os << (cs == CaseSensitivity::Sensitive ? "Sensitive" : "Insensitive");
}
} // namespace facebook::eden
