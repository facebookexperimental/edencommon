/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/ProcMountInfo.h"

#include <cerrno>
#include <stdexcept>

#include <folly/Conv.h>
#include <folly/String.h>

namespace facebook::eden {

folly::Expected<std::vector<MountTableEntry>, int> parseProcMountInfo(
    std::string_view contents,
    MountInfoOptions options) {
  std::vector<MountTableEntry> mounts;
  folly::StringPiece remaining{contents};
  std::vector<folly::StringPiece> fields;
  while (!remaining.empty()) {
    const auto line = remaining.split_step('\n');
    if (line.empty()) {
      continue;
    }
    const auto separator = line.find(" - ");
    if (separator == folly::StringPiece::npos) {
      return folly::makeUnexpected(EINVAL);
    }
    fields.clear();
    folly::split(' ', line.subpiece(0, separator), fields);
    folly::StringPiece fsType, source, superOptions;
    if (fields.size() < 6 ||
        !folly::split(
            ' ', line.subpiece(separator + 3), fsType, source, superOptions)) {
      return folly::makeUnexpected(EINVAL);
    }
    folly::StringPiece major, minor;
    if (!folly::split(':', fields[2], major, minor)) {
      return folly::makeUnexpected(EINVAL);
    }
    auto devMajor = folly::tryTo<uint32_t>(major);
    auto devMinor = folly::tryTo<uint32_t>(minor);
    if (!devMajor || !devMinor) {
      return folly::makeUnexpected(EINVAL);
    }

    MountTableEntry info;
    info.devMajor = *devMajor;
    info.devMinor = *devMinor;
    info.fsType = fsType.str();
    try {
      info.mountRoot = folly::cUnescape<std::string>(fields[3]);
      info.mountPoint = folly::cUnescape<std::string>(fields[4]);
      if (options.includeMountSource) {
        info.mountSource = folly::cUnescape<std::string>(source);
      }
    } catch (const std::invalid_argument&) {
      return folly::makeUnexpected(EINVAL);
    }
    if (options.includeMountOptions) {
      // FUSE's user_id is in the superblock options after the separator.
      info.mountOptions = superOptions.str();
    }
    mounts.push_back(std::move(info));
  }
  return mounts;
}

} // namespace facebook::eden
