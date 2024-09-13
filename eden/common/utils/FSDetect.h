/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include <folly/Range.h>

namespace facebook::eden {

inline bool is_edenfs_fs_type(folly::StringPiece fs_type) {
  return !fs_type.empty() &&
      (fs_type == "edenfs" || fs_type.startsWith("edenfs:"));
}

inline bool is_edenfs_fs_mount(
    folly::StringPiece line_entry,
    const std::string& mountPoint) {
  return is_edenfs_fs_type(line_entry) &&
      line_entry.find(mountPoint) != std::string::npos;
}

} // namespace facebook::eden
