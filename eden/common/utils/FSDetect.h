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

/**
 * macOS reports an NFS mount served over a Unix domain socket as
 * "<socket path>:<mount path>" and drops the "edenfs:" source. EdenFS names
 * its nfsd socket so that the source still identifies the mount without
 * touching it, which a stale mount would not survive.
 */
inline bool is_edenfs_unix_socket_nfs_source(folly::StringPiece mount_source) {
  return mount_source.startsWith("<") &&
      mount_source.find("/edenfs-nfsd.socket>:") != folly::StringPiece::npos;
}

inline bool is_edenfs_nfs_mount(
    folly::StringPiece fs_type,
    folly::StringPiece mount_source) {
  return fs_type == "nfs" &&
      (is_edenfs_fs_type(mount_source) ||
       is_edenfs_unix_socket_nfs_source(mount_source));
}

inline bool is_edenfs_fs_mount(
    folly::StringPiece line_entry,
    const std::string& mountPoint) {
  return is_edenfs_fs_type(line_entry) &&
      line_entry.find(mountPoint) != std::string::npos;
}

/**
 * Whether a mount table entry is an EdenFS mount, given its source and
 * filesystem type as reported by statmount(2), /proc/mounts or statfs(2).
 * EdenFS FUSE mounts use the source "edenfs:" with type "fuse", which
 * /proc/mounts reports as "fuse.edenfs" when the mount was created with that
 * subtype. EdenFS NFS mounts keep the "edenfs:" source with type "nfs", or
 * on macOS carry the nfsd socket as their source, see is_edenfs_nfs_mount().
 */
inline bool is_edenfs_mount(
    folly::StringPiece mountSource,
    folly::StringPiece fsType) {
  return is_edenfs_fs_type(mountSource) || is_edenfs_fs_type(fsType) ||
      fsType == "fuse.edenfs" || is_edenfs_nfs_mount(fsType, mountSource);
}

} // namespace facebook::eden
