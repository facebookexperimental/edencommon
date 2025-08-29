/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/windows/PlatformWindowsUnixSock.h" // @donotremove

#ifdef _WIN32

#include <afunix.h> // @manual

namespace facebook::eden {

DWORD getPeerIoctlCode() {
  return SIO_AF_UNIX_GETPEERPID;
}

} // namespace facebook::eden

#endif
