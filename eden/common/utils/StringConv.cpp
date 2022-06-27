/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/StringConv.h"

namespace facebook::eden {

#ifdef _WIN32

std::wstring multibyteToWideString(folly::StringPiece multiBytePiece) {
  if (multiBytePiece.empty()) {
    return L"";
  }

  int inputSize = folly::to_narrow(folly::to_signed(multiBytePiece.size()));

  // To avoid extra copy or using max size buffers we should get the size
  // first and allocate the right size buffer.
  int size = MultiByteToWideChar(
      CP_UTF8, 0, multiBytePiece.data(), inputSize, nullptr, 0);

  if (size > 0) {
    std::wstring wideString(size, 0);
    int resultSize = MultiByteToWideChar(
        CP_UTF8, 0, multiBytePiece.data(), inputSize, wideString.data(), size);
    if (size == resultSize) {
      return wideString;
    }
  }
  throw makeWin32ErrorExplicit(
      GetLastError(), "Failed to convert char to wide char");
}

#endif

} // namespace facebook::eden
