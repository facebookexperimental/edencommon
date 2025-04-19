/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/portability/SysTypes.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace facebook::eden {

class DynamicEvent {
 public:
  using IntMap = std::unordered_map<std::string, int64_t>;
  using StringMap = std::unordered_map<std::string, std::string>;
  using DoubleMap = std::unordered_map<std::string, double>;
  using StringVecMap =
      std::unordered_map<std::string, std::vector<std::string>>;

  DynamicEvent() = default;
  DynamicEvent(const DynamicEvent&) = default;
  DynamicEvent(DynamicEvent&&) = default;
  DynamicEvent& operator=(const DynamicEvent&) = default;
  DynamicEvent& operator=(DynamicEvent&&) = default;

  /**
   * Truncate the given integer and only keeps the highest significant bits.
   * This method is intended to be used for data which does not have to be
   * 100% accurate. This is used to reduce the integer cardinality to save
   * storage quota in databases.
   */
  void
  addTruncatedInt(std::string name, int64_t value, uint32_t bits_to_keep = 8);

  void addInt(std::string name, int64_t value);
  void addString(std::string name, std::string value);
  void addDouble(std::string name, double value);
  void addStringVec(std::string name, std::vector<std::string> value);

  /**
   * Convenience function that adds boolean values as integer 0 or 1.
   */
  void addBool(std::string name, bool value) {
    addInt(std::move(name), value);
  }

  const IntMap& getIntMap() const {
    return ints_;
  }
  const StringMap& getStringMap() const {
    return strings_;
  }
  const DoubleMap& getDoubleMap() const {
    return doubles_;
  }
  const StringVecMap& getStringVecMap() const {
    return stringVecs_;
  }

 private:
  // Due to limitations in the underlying log database, limit the field types to
  // int64_t, double, string, and vector<string>
  IntMap ints_;
  StringMap strings_;
  DoubleMap doubles_;
  StringVecMap stringVecs_;
};

} // namespace facebook::eden
