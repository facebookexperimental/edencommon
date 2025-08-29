/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <system_error>
#include "eden/common/utils/windows/WinError.h"

namespace facebook::eden {

/**
 * Return true if this exception contains an errno value in ex.code().value()
 */
inline bool isErrnoError(const std::system_error& ex) {
  // std::generic_category is the correct category to represent errno values.
  // However folly/Exception.h unfortunately throws errno values as
  // std::system_category for now.
  return (
      ex.code().category() == std::generic_category() ||
      ex.code().category() == std::system_category());
}

/**
 * Return true if this exception is equivalent to an ENOENT error code.
 */
inline bool isEnoent(const std::system_error& ex) {
  auto ret = isErrnoError(ex) && ex.code().value() == ENOENT;
#ifdef _WIN32
  ret = ret ||
      (ex.code().category() == Win32ErrorCategory::get() &&
       (ex.code().value() == ERROR_PATH_NOT_FOUND ||
        ex.code().value() == ERROR_FILE_NOT_FOUND));
#endif
  return ret;
}

inline bool isEnotempty(const std::system_error& ex) {
  return isErrnoError(ex) && ex.code().value() == ENOTEMPTY;
}

} // namespace facebook::eden
