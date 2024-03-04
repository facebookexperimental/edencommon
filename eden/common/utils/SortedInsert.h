/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <algorithm>

namespace facebook::eden {

// Generic function to insert an item in sorted order
template <typename T, typename COMP, typename CONT>
inline typename CONT::iterator sorted_insert(CONT& vec, T&& val, COMP compare) {
  auto find =
      std::lower_bound(vec.begin(), vec.end(), std::forward<T>(val), compare);
  if (find != vec.end() && !compare(val, *find)) {
    // Already exists
    return find;
  }
  return vec.emplace(find, val);
}

} // namespace facebook::eden
