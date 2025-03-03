/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/WinUtil.h"

#ifdef _WIN32

#include "c2p/secure_thrift/cpp/client/platform/windows/PlatformWindowsUnixSock.h" // @donotremove

namespace facebook::eden {

pid_t getPeerProcessID(intptr_t fd) {
  // Windows handling is separate from cred based methods
  u_long peer_PID;
  DWORD returned_size; // Broken and always returns 0 as per
                       // https://github.com/microsoft/WSL/issues/4676
                       // but is a required parameter
  int valid_peer = WSAIoctl(
      fd,
      facebook::corp2prod::platform::getPeerIoctlCode(),
      nullptr,
      0,
      &peer_PID,
      sizeof(peer_PID),
      &returned_size,
      nullptr,
      nullptr);

  if (valid_peer != NULL) {
    return 0;
  }
  return peer_PID;
}

} // namespace facebook::eden
#endif
