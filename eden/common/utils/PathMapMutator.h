/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/FBVector.h>
#include <folly/logging/xlog.h>
#include <algorithm>
#include <unordered_set>
#include <utility>

#include "eden/common/utils/PathMap.h"

namespace facebook::eden {

/**
 * PathMap wrapper for efficient batch updates. Optimized for emplace() calls in
 * sorted order (like checkout).
 *
 * PathMap erase/emplace operations are O(n) due to element shifting.
 * PathMapMutator defers them:
 *
 * - Erased entries are tracked by index and removed in one pass.
 * - New entries are appended to a sorted suffix and merged in O(n).
 * - If the suffix is appended out of order, the mutator compacts itself
 *   back into a clean sorted state before continuing. This is for correctness -
 *   the assumption is emplace() is called in sorted order.
 *
 * The newly emplaced entries are stored in the "tail" the same
 * underlying PathMap vector. This avoids allocating a separate vector.
 *
 * Call finalize() to produce the resultant PathMap.
 */
template <typename Value, typename Key = PathComponent>
class PathMapMutator {
  using Map = PathMap<Value, Key>;
  using Pair = std::pair<Key, Value>;
  using Vector = folly::fbvector<Pair>;
  using Piece = typename Key::piece_type;
  using Compare = typename Map::key_compare;

 public:
  using iterator = typename Vector::iterator;
  using const_iterator = typename Vector::const_iterator;
  using size_type = typename Vector::size_type;

 private:
  struct FindResult {
    const_iterator it;
    bool erased;
  };

 public:
  explicit PathMapMutator(Map&& map)
      : compare_(map.getCaseSensitivity()),
        caseSensitive_(map.getCaseSensitivity()),
        map_(std::move(map)) {
    suffixStart_ = vec().size();
  }

  ~PathMapMutator() = default;
  PathMapMutator(const PathMapMutator&) = delete;
  PathMapMutator& operator=(const PathMapMutator&) = delete;
  PathMapMutator(PathMapMutator&&) = delete;
  PathMapMutator& operator=(PathMapMutator&&) = delete;

  iterator find(Piece key) {
    auto [it, erased] = findImpl(key);
    return erased ? end() : toMutable(it);
  }

  const_iterator find(Piece key) const {
    auto [it, erased] = findImpl(key);
    return erased ? end() : it;
  }

  iterator end() {
    return vec().end();
  }

  const_iterator end() const {
    return vec().end();
  }

  void erase(iterator it) {
    erased_.insert(indexOf(it));
  }

  size_type erase(Piece key) {
    auto it = find(key);
    if (it == end()) {
      return 0;
    }
    erase(it);
    return 1;
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Piece key, Args&&... args) {
    auto [cit, erased] = findImpl(key);
    if (cit != vec().end()) {
      auto it = toMutable(cit);
      if (!erased) {
        return {it, false};
      }
      // Reuse the erased slot.
      erased_.erase(indexOf(it));
      it->first = Key(key);
      it->second = Value(std::forward<Args>(args)...);
      return {it, true};
    }

    // If the new key would break suffix sort order, compact the entire vector.
    // This maintains correctness at the cost of our performance gain. The
    // assumption is that emplace() is normally called in sorted order as trees
    // are reconciled together.
    if (vec().size() > suffixStart_ && !compare_(vec().back().first, key)) {
      XLOGF(
          WARN,
          "PathMapMutator: suffix appended out of order ({}), compacting",
          key.view());
      compact();
    }

    vec().emplace_back(Key(key), Value(std::forward<Args>(args)...));
    return {vec().end() - 1, true};
  }

  Map finalize() {
    compact();
    return std::move(map_);
  }

 private:
  Vector& vec() {
    return static_cast<Vector&>(map_);
  }

  const Vector& vec() const {
    return static_cast<const Vector&>(map_);
  }

  size_t indexOf(const_iterator it) const {
    return static_cast<size_t>(it - vec().begin());
  }

  iterator toMutable(const_iterator cit) {
    return vec().begin() + (cit - vec().begin());
  }

  /// Find a key in both prefix and suffix. Returns the iterator and whether
  /// it is marked as erased.
  FindResult findImpl(Piece key) const {
    auto prefixEnd = vec().begin() + suffixStart_;
    auto it = std::lower_bound(vec().begin(), prefixEnd, key, compare_);
    if (it != prefixEnd && !compare_(key, it->first)) {
      return {it, erased_.count(indexOf(it)) > 0};
    }
    if (vec().size() > suffixStart_) {
      auto sit = std::lower_bound(
          vec().begin() + suffixStart_, vec().end(), key, compare_);
      if (sit != vec().end() && !compare_(key, sit->first)) {
        return {sit, erased_.count(indexOf(sit)) > 0};
      }
    }
    return {vec().end(), false};
  }

  /// Remove erased entries and merge prefix + suffix into sorted order.
  void compact() {
    size_t idx = 0;
    size_t prefixErased = 0;
    vec().erase(
        std::remove_if(
            vec().begin(),
            vec().end(),
            [&](const Pair&) {
              bool removed = erased_.count(idx) > 0;
              if (removed && idx < suffixStart_) {
                ++prefixErased;
              }
              ++idx;
              return removed;
            }),
        vec().end());
    suffixStart_ -= prefixErased;
    erased_.clear();

    std::inplace_merge(
        vec().begin(), vec().begin() + suffixStart_, vec().end(), compare_);

    suffixStart_ = vec().size();
  }

  Compare compare_;
  CaseSensitivity caseSensitive_;
  Map map_;
  size_t suffixStart_{0};
  std::unordered_set<size_t> erased_;
};

} // namespace facebook::eden
