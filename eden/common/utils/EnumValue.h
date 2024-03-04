/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Utility.h>

namespace facebook::eden {

/**
 * It's common in error messages to log the underlying value of an enumeration.
 * Bring a short function into the eden namespace to retrieve that value.
 */
template <typename E>
constexpr std::underlying_type_t<E> enumValue(E e) noexcept {
  return folly::to_underlying(e);
}

} // namespace facebook::eden
