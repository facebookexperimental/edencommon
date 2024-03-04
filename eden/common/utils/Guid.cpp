/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/Guid.h"

#ifdef _WIN32
#include <combaseapi.h> // @manual
#endif

#ifdef _WIN32

namespace facebook::eden {

Guid Guid::generate() {
  GUID id;
  HRESULT result = CoCreateGuid(&id);
  if (FAILED(result)) {
    throw std::system_error(
        result, HResultErrorCategory::get(), "Failed to create a GUID");
  }
  return Guid{id};
}

Guid::Guid(const std::string& s) {
  auto ret = UuidFromStringA((RPC_CSTR)s.c_str(), &guid_);
  if (ret != RPC_S_OK) {
    throw makeWin32ErrorExplicit(
        ret, fmt::format(FMT_STRING("Failed to parse UUID: {}"), s));
  }
}

} // namespace facebook::eden

#endif
