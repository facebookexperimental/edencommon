/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <folly/Expected.h>

namespace facebook::eden {

struct MountTableEntry {
  uint32_t devMajor{};
  uint32_t devMinor{};
  std::string mountRoot;
  std::string mountPoint;
  std::string mountSource;
  std::string fsType;
  std::string mountOptions;
};

struct MountInfoOptions {
  bool includeMountSource{false};
  bool includeMountOptions{false};
};

/**
 * Parse Linux /proc/<pid>/mountinfo, returning EINVAL for malformed entries.
 * Paths and the mount source are unescaped. Mount options retain their
 * escaping so an escaped comma cannot be mistaken for an option separator.
 */
folly::Expected<std::vector<MountTableEntry>, int> parseProcMountInfo(
    std::string_view contents,
    MountInfoOptions options = {});

} // namespace facebook::eden
