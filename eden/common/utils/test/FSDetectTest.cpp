/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/FSDetect.h"

#include <folly/portability/GTest.h>

using namespace facebook::eden;

TEST(FSDetect, nfsMountWithEdenSourceIsEdenFS) {
  EXPECT_TRUE(is_edenfs_nfs_mount("nfs", "edenfs:"));
  EXPECT_FALSE(is_edenfs_nfs_mount("nfs", "server:/export"));
  EXPECT_FALSE(is_edenfs_nfs_mount("apfs", "edenfs:"));
}

TEST(FSDetect, unixSocketNfsMountIsRecognizedBySocketName) {
  // macOS replaces the source of a Unix-socket NFS mount with the socket.
  EXPECT_TRUE(is_edenfs_nfs_mount(
      "nfs",
      "</Users/me/.eden/clients/repo/edenfs-nfsd.socket>:/Users/me/repo"));
  EXPECT_FALSE(
      is_edenfs_nfs_mount("nfs", "</var/run/other/nfsd.socket>:/mnt/other"));
  EXPECT_FALSE(is_edenfs_nfs_mount(
      "apfs",
      "</Users/me/.eden/clients/repo/edenfs-nfsd.socket>:/Users/me/repo"));
}

TEST(FSDetect, mountTableEntryWithUnixSocketNfsSourceIsEdenFS) {
  // The privhelper's pin scan reads statfs on macOS, where the source of a
  // Unix-socket NFS mount is the socket once the type override is off.
  EXPECT_TRUE(is_edenfs_mount(
      "</Users/me/.eden/clients/repo/edenfs-nfsd.socket>:/Users/me/repo",
      "nfs"));
  EXPECT_FALSE(
      is_edenfs_mount("</var/run/other/nfsd.socket>:/mnt/other", "nfs"));
  EXPECT_TRUE(is_edenfs_mount("edenfs:", "fuse"));
  EXPECT_TRUE(is_edenfs_mount("edenfs:", "nfs"));
}
