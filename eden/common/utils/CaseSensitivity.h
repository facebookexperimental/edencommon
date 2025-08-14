/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fmt/format.h>
#include <folly/Portability.h>

namespace facebook::eden {
enum class CaseSensitivity : bool {
  Insensitive = false,
  Sensitive = true,
};

constexpr CaseSensitivity kPathMapDefaultCaseSensitive =
    static_cast<CaseSensitivity>(folly::kIsLinux);

} // namespace facebook::eden

template <>
struct fmt::formatter<facebook::eden::CaseSensitivity>
    : formatter<string_view> {
  template <typename Context>
  auto format(const facebook::eden::CaseSensitivity& cs, Context& ctx) const {
    return formatter<string_view>::format(
        cs == facebook::eden::CaseSensitivity::Sensitive ? "Sensitive"
                                                         : "Insensitive",
        ctx);
  }
};
