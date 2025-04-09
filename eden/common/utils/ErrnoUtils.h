/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cerrno>

namespace facebook::eden {

inline bool isErrnoFromHangingMount(int err, bool isNFS) {
  if (isNFS) {
    // Hard NFS mounts tend to return EIO, whereas soft NFS mounts return
    // ETIMEDOUT
    return err == ENOTCONN || err == EIO || err == ETIMEDOUT;
  } else {
    // FUSE mounts (seem to) always return ENOTCONN when the mount is hanging
    return err == ENOTCONN || err == EIO;
  }
}
} // namespace facebook::eden
