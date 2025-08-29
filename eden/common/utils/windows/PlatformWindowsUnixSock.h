/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#ifdef _WIN32

#include <folly/portability/Windows.h>

namespace facebook::eden {

// super hacky way to make the compilers happy since folly
// and afunix.h both define sockaddr_un
DWORD getPeerIoctlCode();

} // namespace facebook::eden
#endif
