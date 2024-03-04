/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/SysctlUtil.h"

#ifdef __APPLE__

#include <sys/sysctl.h> // @manual
#include <sys/types.h>

#include <folly/Exception.h>

std::string getSysCtlByName(const char* name, size_t size) {
  if (size == 0) {
    return std::string{};
  }
  std::string buffer(size, 0);
  size_t returnedSize = size - 1;
  auto ret = sysctlbyname(name, &buffer[0], &returnedSize, nullptr, 0);
  if (ret != 0) {
    folly::throwSystemError("failed to retrieve sysctl ", name);
  }
  buffer.resize(returnedSize);
  return buffer;
}
#endif
