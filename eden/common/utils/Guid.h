/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fmt/format.h>
#include <folly/portability/Windows.h>
#include "eden/common/utils/windows/WinError.h"

#ifdef _WIN32

template <>
struct fmt::formatter<GUID> {
  constexpr auto parse(fmt::format_parse_context& ctx) {
    auto it = ctx.begin();
    auto end = ctx.end();
    if (it != end && *it != '}') {
      // Handle custom format specifiers here
      // Currently none are supported.
    }
    return it;
  }

  template <typename FormatContext>
  auto format(const GUID& guid, FormatContext& ctx) const {
    return fmt::format_to(
        ctx.out(),
        FMT_STRING(
            "{{{:08X}-{:04X}-{:04X}-{:02X}{:02X}-{:02X}{:02X}{:02X}{:02X}{:02X}{:02X}}}"),
        guid.Data1,
        guid.Data2,
        guid.Data3,
        guid.Data4[0],
        guid.Data4[1],
        guid.Data4[2],
        guid.Data4[3],
        guid.Data4[4],
        guid.Data4[5],
        guid.Data4[6],
        guid.Data4[7]);
  }
};

namespace facebook::eden {

class Guid {
 public:
  static Guid generate();

  explicit Guid(const std::string& s);

  Guid() = default;
  Guid(const GUID& guid) noexcept : guid_{guid} {}

  Guid(const Guid& other) noexcept : guid_{other.guid_} {}

  Guid& operator=(const Guid& other) noexcept {
    guid_ = other.guid_;
    return *this;
  }

  const GUID& getGuid() const noexcept {
    return guid_;
  }

  operator const GUID&() const noexcept {
    return guid_;
  }

  operator const GUID*() const noexcept {
    return &guid_;
  }

  bool operator==(const Guid& other) const noexcept {
    return guid_ == other.guid_;
  }

  bool operator!=(const Guid& other) const noexcept {
    return !(*this == other);
  }

  std::string toString() const noexcept {
    return fmt::to_string(guid_);
  }

 private:
  GUID guid_{};
};

} // namespace facebook::eden

namespace std {
template <>
struct hash<facebook::eden::Guid> {
  size_t operator()(const facebook::eden::Guid& guid) const {
    return folly::hash::SpookyHashV2::Hash64(
        reinterpret_cast<const void*>(&guid), sizeof(guid), 0);
  }
};
} // namespace std

template <>
struct fmt::formatter<facebook::eden::Guid> : public fmt::formatter<GUID> {
  template <typename FormatContext>
  auto format(const facebook::eden::Guid& guid, FormatContext& ctx) const {
    return fmt::formatter<GUID>::format(guid.getGuid(), ctx);
  }
};

#endif
