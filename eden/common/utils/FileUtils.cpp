/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/FileUtils.h"

#ifdef _WIN32

#include <winioctl.h> // @manual

namespace facebook::eden {

ReparseDataBuffer getReparseData(HANDLE fd) {
  auto buffer = ReparseDataBuffer(static_cast<REPARSE_DATA_BUFFER*>(
      malloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE)));
  if (!buffer) {
    throw std::bad_alloc();
  }

  DWORD written;
  auto result = DeviceIoControl(
      fd,
      FSCTL_GET_REPARSE_POINT,
      nullptr,
      0,
      buffer.get(),
      MAXIMUM_REPARSE_DATA_BUFFER_SIZE,
      &written,
      nullptr);

  if (!result && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    buffer.reset(static_cast<REPARSE_DATA_BUFFER*>(malloc(written)));
    if (!buffer) {
      throw std::bad_alloc();
    }

    result = DeviceIoControl(
        fd,
        FSCTL_GET_REPARSE_POINT,
        nullptr,
        0,
        buffer.get(),
        MAXIMUM_REPARSE_DATA_BUFFER_SIZE,
        &written,
        nullptr);
  }

  if (!result) {
    throw std::system_error(
        GetLastError(), std::system_category(), "FSCTL_GET_REPARSE_POINT");
  }

  return buffer;
}

} // namespace facebook::eden
#endif
