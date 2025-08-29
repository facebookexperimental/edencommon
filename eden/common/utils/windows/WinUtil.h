/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <folly/portability/SysTypes.h>

#ifdef _WIN32
// Get the PID of the peer process on the other end of a socket.
namespace facebook::eden {

pid_t getPeerProcessID(intptr_t fd);

} // namespace facebook::eden
#endif
