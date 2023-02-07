/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#ifdef _WIN32
#include <memory>
#include <system_error>

#include <folly/portability/Windows.h>

namespace folly {
template <typename T>
class Try;
}

namespace facebook::eden {

// We declare our own copy here because Ntifs.h is not included in the
// standard install of the Visual Studio Community compiler.
struct REPARSE_DATA_BUFFER {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
};

struct ReparseDataDeleter {
  void operator()(void* p) {
    free(p);
  }
};

using ReparseDataBuffer =
    std::unique_ptr<REPARSE_DATA_BUFFER, ReparseDataDeleter>;

folly::Try<ReparseDataBuffer> getReparseData(HANDLE fd);

} // namespace facebook::eden

#endif
