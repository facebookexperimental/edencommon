/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Range.h>
#include <folly/Try.h>
#include <limits>
#include <string>

#include "eden/common/utils/FileOffset.h"
#include "eden/common/utils/Handle.h"
#include "eden/common/utils/PathFuncs.h"

#ifdef _WIN32

#include <memory>
#include <system_error>

#include <folly/portability/Windows.h>

namespace folly {
template <typename T>
class Try;
}

#endif

namespace facebook::eden {

/** Read up to num_bytes bytes from the file */
[[nodiscard]] folly::Try<std::string> readFile(
    AbsolutePathPiece path,
    size_t num_bytes = std::numeric_limits<size_t>::max());

/** Write data to the file pointed by path */
[[nodiscard]] folly::Try<void> writeFile(
    AbsolutePathPiece path,
    folly::ByteRange data);

/** Atomically replace the content of the file with data.
 *
 * On failure, the content of the file is unchanged.
 */
[[nodiscard]] folly::Try<void> writeFileAtomic(
    AbsolutePathPiece path,
    folly::ByteRange data);

/**
 * Read all the directory entry and return their names.
 *
 * On non-Windows OS, this is simply a wrapper around
 * boost::filesystem::directory_iterator.
 *
 * On Windows, we have to use something different as Boost will use the
 * FindFirstFile API which doesn't allow the directory to be opened with
 * FILE_SHARE_DELETE. This sharing flags allows the directory to be
 * renamed/deleted while it is being iterated on.
 */
[[nodiscard]] folly::Try<std::vector<PathComponent>> getAllDirectoryEntryNames(
    AbsolutePathPiece path);

#ifdef _WIN32

/*
 * Following is a traits class for File System handles with its handle value and
 * close function.
 */
struct FileHandleTraits {
  using Type = HANDLE;

  static Type invalidHandleValue() noexcept {
    return INVALID_HANDLE_VALUE;
  }
  static void close(Type handle) noexcept {
    CloseHandle(handle);
  }
};

using FileHandle = HandleBase<FileHandleTraits>;
/**
 * For Windows only, returns the file size of the materialized file.
 */
folly::Try<FileOffset> getMaterializedFileSize(AbsolutePathPiece pathToFile);

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
      // The format here is not officially documented, so this format is what
      // we have inferred from our own local testing, and what's mentioned on
      // github: https://github.com/microsoft/ProjFS-Managed-API/issues/55
      UINT UnknownMaybeVersion;
      BYTE ProjFsFlags;
      // we think there are 3 more flag bits here, then an id for the virtual
      // root, then a provider id, content id, and the original path of the
      // placeholder.
      UCHAR RestOfDataBuffer[1];
    } ProjFsReparseBuffer;
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

#endif

} // namespace facebook::eden
