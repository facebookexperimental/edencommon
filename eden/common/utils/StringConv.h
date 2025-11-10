/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <string_view>

namespace facebook::eden {

#ifdef _WIN32

/**
 * Convert a wide string to a utf-8 encoded string.
 */
template <class MultiByteStringType>
MultiByteStringType wideToMultibyteString(std::wstring_view wideCharPiece);

/**
 * Convert a utf-8 encoded string to a wide string.
 */
std::wstring multibyteToWideString(std::string_view multiBytePiece);

#endif

} // namespace facebook::eden
