/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/String.h"

namespace facebook::eden {

std::vector<std::string_view> split(std::string_view s, char delim) {
  std::vector<std::string_view> result;
  std::size_t i = 0;

  while ((i = s.find(delim)) != std::string_view::npos) {
    result.emplace_back(s.substr(0, i));
    s.remove_prefix(i + 1);
  }
  result.emplace_back(s);
  return result;
}

} // namespace facebook::eden
