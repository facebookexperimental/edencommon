/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <folly/FBVector.h>
#include <folly/Portability.h>
#include <folly/logging/xlog.h>
#include <folly/memory/Malloc.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "eden/common/utils/CaseSensitivity.h"
#include "eden/common/utils/PathFuncs.h"
#include "eden/common/utils/Throw.h"

namespace facebook::eden {

/** An associative container that maps from one of our path types to an
 * arbitrary value type.
 *
 * This is similar to std::map but has a couple of different properties:
 * - lookups can be made using the Piece (non-stored) variant of the key
 *   type and won't require allocation just for the lookup.
 * - The entries are stored in a single vector, so the whole map is one
 *   allocation with no per-node overhead, and iteration yields entries in
 *   sorted key order.
 * - insert and erase are amortized: rather than shifting the vector on
 *   every mutation, inserts go to a small sorted pending region at the
 *   tail and erases mark a tombstone, and mutating operations fold those
 *   back into the sorted prefix once they grow past a small bound. This
 *   makes a burst of n mutations cost O(n log n) total instead of O(n^2).
 * - insert and erase invalidate iterators; lookups and iteration do not
 *   modify the map and are safe under a shared lock.
 * - Iterators are forward-only. Use the find/lower_bound members for
 *   lookups: free algorithms like std::lower_bound still compile against
 *   the forward iterators, but degrade to a linear scan.
 */
template <typename Value, typename Key = PathComponent>
class PathMap : private folly::fbvector<std::pair<Key, Value>> {
  using Pair = std::pair<Key, Value>;
  using Vector = folly::fbvector<Pair>;
  using Piece = typename Key::piece_type;
  using Allocator = typename Vector::allocator_type;
  using VectorSizeType = typename Vector::size_type;

  // Comparator that knows how compare Stored and Piece in the vector.
  struct Compare {
    explicit Compare(CaseSensitivity caseSensitive)
        : caseSensitive_{caseSensitive} {}

    // Compare two values that are convertible to the Piece type.
    template <typename A, typename B>
    typename std::enable_if<
        std::is_convertible<A, Piece>::value &&
            std::is_convertible<B, Piece>::value,
        bool>::type
    operator()(const A& a, const B& b) const {
      return isPathPieceLess(Piece(a), Piece(b), caseSensitive_);
    }

    // Compare a Piece-convertible value against the stored Pair.
    template <typename A, typename B, typename C>
    typename std::enable_if<
        std::is_convertible<A, Piece>::value &&
            std::is_convertible<B, Piece>::value,
        bool>::type
    operator()(const A& a, const std::pair<B, C>& rhs) const {
      return isPathPieceLess(Piece(a), Piece(rhs.first), caseSensitive_);
    }

    // Compare the stored Pair against a Piece-convertible value.
    template <typename A, typename B, typename C>
    typename std::enable_if<
        std::is_convertible<A, Piece>::value &&
            std::is_convertible<B, Piece>::value,
        bool>::type
    operator()(const std::pair<A, C>& lhs, const B& b) const {
      return isPathPieceLess(Piece(lhs.first), Piece(b), caseSensitive_);
    }

    // Compare two stored Pairs.
    template <typename A, typename B>
      requires std::convertible_to<A, Piece>
    bool operator()(const std::pair<A, B>& lhs, const std::pair<A, B>& rhs)
        const {
      return isPathPieceLess(
          Piece(lhs.first), Piece(rhs.first), caseSensitive_);
    }

    CaseSensitivity caseSensitive_{kPathMapDefaultCaseSensitive};
  };

  // Hold an instance of the comparator.
  Compare compare_;

  // The vector holds a sorted prefix [0, sortedEnd_) followed by a small
  // sorted pending region [sortedEnd_, size) of recent inserts. Prefix
  // entries whose bit is set in tombstones_ have been erased; deadCount_
  // counts them. Pending entries are never tombstoned: the pending region
  // is small enough to erase from directly. Keys are unique across both
  // regions, dead entries included: inserting a key that matches a
  // tombstoned prefix entry revives that entry in place rather than
  // adding a pending one.
  VectorSizeType sortedEnd_{0};
  VectorSizeType deadCount_{0};
  std::unique_ptr<std::vector<bool>> tombstones_;

  // Compaction cost is O(size), so it is only worth batching this many
  // mutations at a minimum.
  static constexpr VectorSizeType kMinCompactionBatch = 32;

  /** Merged forward iterator over the sorted prefix and the pending
   * region, skipping tombstones. Holds one cursor into each region plus
   * switchIdx_, the prefix index before which the pending cursor's entry
   * belongs. Stepping through the prefix therefore costs an integer
   * compare, and key compares are confined to one binary search per
   * pending entry, when the pending cursor moves. With no pending region
   * switchIdx_ is sortedEnd_ and the walk is a plain scan of the prefix. */
  template <bool IsConst>
  class Iter {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = Pair;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const Pair*, Pair*>;
    using reference = std::conditional_t<IsConst, const Pair&, Pair&>;

    Iter() = default;

    /* implicit */ Iter(const Iter<!IsConst>& other)
      requires IsConst
        : map_{other.map_},
          data_{other.data_},
          dead_{other.dead_},
          sortedIdx_{other.sortedIdx_},
          pendingIdx_{other.pendingIdx_},
          switchIdx_{other.switchIdx_} {}

    reference operator*() const {
      return data_[onSorted() ? sortedIdx_ : pendingIdx_];
    }

    pointer operator->() const {
      return &operator*();
    }

    Iter& operator++() {
      if (onSorted()) {
        ++sortedIdx_;
        if (dead_) {
          sortedIdx_ = map_->skipDead(sortedIdx_);
        }
      } else {
        ++pendingIdx_;
        switchIdx_ = map_->switchIndex(pendingIdx_, sortedIdx_);
      }
      return *this;
    }

    Iter operator++(int) {
      Iter tmp = *this;
      ++*this;
      return tmp;
    }

    bool operator==(const Iter& other) const {
      // Comparing iterators from different maps is undefined behavior;
      // positions alone are not comparable across maps.
      XDCHECK_EQ(map_, other.map_);
      return sortedIdx_ == other.sortedIdx_ && pendingIdx_ == other.pendingIdx_;
    }

   private:
    friend class PathMap;
    friend Iter<!IsConst>;

    using MapPtr = std::conditional_t<IsConst, const PathMap*, PathMap*>;
    using PairPtr = std::conditional_t<IsConst, const Pair*, Pair*>;

    Iter(MapPtr map, VectorSizeType sortedIdx, VectorSizeType pendingIdx)
        : Iter{
              map,
              sortedIdx,
              pendingIdx,
              map->switchIndex(pendingIdx, sortedIdx)} {}

    /** For callers that already know the switch index, notably end() and
     * lookups on a map with no pending region. */
    Iter(
        MapPtr map,
        VectorSizeType sortedIdx,
        VectorSizeType pendingIdx,
        VectorSizeType switchIdx)
        : map_{map},
          data_{map->storage()},
          dead_{map->tombstones_.get()},
          sortedIdx_{sortedIdx},
          pendingIdx_{pendingIdx},
          switchIdx_{switchIdx} {}

    /** The prefix cursor's entry precedes the pending cursor's. Once the
     * prefix is exhausted sortedIdx_ == sortedEnd_ == switchIdx_, so any
     * remaining pending entries are emitted. */
    bool onSorted() const {
      return sortedIdx_ != switchIdx_;
    }

    MapPtr map_{nullptr};
    // The map's entry storage and tombstone bitmap (null when it has none),
    // cached so a step does not go through map_. Mutations invalidate
    // iterators, so neither can go stale.
    PairPtr data_{nullptr};
    const std::vector<bool>* dead_{nullptr};
    // sortedIdx_ never rests on a tombstoned entry, and never exceeds
    // switchIdx_.
    VectorSizeType sortedIdx_{0};
    VectorSizeType pendingIdx_{0};
    VectorSizeType switchIdx_{0};
  };

 public:
  // Various type aliases to satisfy container concepts.
  using key_type = Key;
  using mapped_type = Value;
  using value_type = typename Vector::value_type;
  using key_compare = Compare;
  using allocator_type = Allocator;
  using reference = Pair&;
  using const_reference = const Pair&;
  using iterator = Iter<false>;
  using const_iterator = Iter<true>;
  using size_type = typename Vector::size_type;
  using difference_type = typename Vector::difference_type;
  using pointer = Pair*;
  using const_pointer = const Pair*;

  // Construct empty.
  explicit PathMap(CaseSensitivity caseSensitive) : compare_(caseSensitive) {}

  // Populate from an initializer_list.
  PathMap(std::initializer_list<value_type> init, CaseSensitivity caseSensitive)
      : PathMap(init.begin(), init.end(), caseSensitive) {}

  // Populate from a pair of input iterators.
  template <typename InputIterator>
  PathMap(
      InputIterator first,
      InputIterator last,
      CaseSensitivity caseSensitive)
      : compare_(caseSensitive) {
    // The std::distance call is O(1) if the iterators are random-access, but
    // O(n) otherwise.  We're fine with the O(n) on the basis that if n is large
    // enough to matter, the cost of iterating will be dwarfed by the cost
    // of growing the storage several times during population.
    this->reserve(std::distance(first, last));
    for (; first != last; ++first) {
      insert(*first);
    }
  }

  // Initialize using given vector of entries, sorting and deduping if needed.
  // This is more efficient than calling PathMap::emplace n times.
  PathMap(Vector&& entries, CaseSensitivity caseSensitive)
      : Vector(std::move(entries)), compare_(caseSensitive) {
    Vector& vec = *this;

    // In practice, Sapling yields tree entries naively sorted. On case
    // sensitive filesystems, the natural sorting will match what we want and
    // should never contain duplicates, so we will hit our fast path below and
    // avoid sorting. On case insensitive filesystems, if the entries don't
    // happen to be sorted (or there are case insensitive collisions), we will
    // hit the slow path below to sort and/or dedupe.

    bool needsSortAndOrDedupe = false;
    for (size_t idx = 0; idx < vec.size(); idx++) {
      if (idx > 0 && !compare_(vec[idx - 1].first, vec[idx].first)) {
        needsSortAndOrDedupe = true;
        break;
      }
    }

    if (needsSortAndOrDedupe) {
      // Need to sort and/or remove duplicates. The earliest entry "wins".

      // Stable sort so earliest entry remains first after sort.
      std::stable_sort(vec.begin(), vec.end(), compare_);

      // Unique out the duplicates.
      auto last = std::unique(vec.begin(), vec.end(), [=](auto& a, auto& b) {
        // NB: assumes Key is PathComponent (but so does Compare).
        return isPathPieceEqual(a.first, b.first, caseSensitive);
      });
      vec.erase(last, vec.end());
    }

    sortedEnd_ = vec.size();
  }

  PathMap(const PathMap& other)
      : Vector(other),
        compare_(other.compare_),
        sortedEnd_(other.sortedEnd_),
        deadCount_(other.deadCount_),
        tombstones_(
            other.tombstones_
                ? std::make_unique<std::vector<bool>>(*other.tombstones_)
                : nullptr) {}
  PathMap& operator=(const PathMap& other) {
    PathMap(other).swap(*this);
    return *this;
  }

  PathMap(PathMap&& other) noexcept
      : Vector(std::move(other)),
        compare_(other.compare_),
        sortedEnd_(std::exchange(other.sortedEnd_, 0)),
        deadCount_(std::exchange(other.deadCount_, 0)),
        tombstones_(std::move(other.tombstones_)) {}
  PathMap& operator=(PathMap&& other) {
    other.swap(*this);
    return *this;
  }

  // The storage layout is deliberately not part of this interface: only the
  // operations below are supported, so that the representation can change
  // without auditing every caller. In particular there is no random access
  // to entries, and no reverse iteration.

  iterator begin() {
    return iterator{this, skipDead(0), sortedEnd_};
  }
  const_iterator begin() const {
    return const_iterator{this, skipDead(0), sortedEnd_};
  }
  const_iterator cbegin() const {
    return begin();
  }

  iterator end() {
    return iterator{this, sortedEnd_, rawSize(), sortedEnd_};
  }
  const_iterator end() const {
    return const_iterator{this, sortedEnd_, rawSize(), sortedEnd_};
  }
  const_iterator cend() const {
    return end();
  }

  size_type size() const {
    return rawSize() - deadCount_;
  }
  bool empty() const {
    return size() == 0;
  }

  void clear() {
    Vector::clear();
    sortedEnd_ = 0;
    deadCount_ = 0;
    tombstones_.reset();
  }

  /** Reserve storage for at least `n` entries. */
  void reserve(size_type n) {
    Vector::reserve(n);
  }

  /** Erase the entry referenced by `pos`. Invalidates iterators.
   * Returns an iterator to the entry following the erased one; computing
   * it costs O(length of the tombstone run following `pos`), so callers
   * erasing in descending order should erase by key instead. */
  iterator erase(const_iterator pos) {
    XDCHECK_EQ(pos.map_, this);
    switch (eraseAt(pos)) {
      case EraseAction::RemovedPending:
        return iterator{this, pos.sortedIdx_, pos.pendingIdx_};
      case EraseAction::RemovedSorted:
        return iterator{this, pos.sortedIdx_, pos.pendingIdx_ - 1};
      case EraseAction::Tombstoned:
        break;
    }
    const auto nextSorted = skipDead(pos.sortedIdx_ + 1);
    if (deadCount_ > deadLimit()) {
      // Compaction moves entries, so remember the next entry by key and
      // re-find it afterwards.
      const const_iterator next{this, nextSorted, pos.pendingIdx_};
      if (next == cend()) {
        compact();
        return end();
      }
      const Key nextKey = next->first;
      compact();
      return lower_bound(nextKey.piece());
    }
    return iterator{this, nextSorted, pos.pendingIdx_};
  }

  /** Bytes of heap storage held for the entries themselves, excluding
   * anything the keys and values point to. Reported by the map rather than
   * computed from a capacity so that it stays correct as the storage
   * layout changes. */
  size_type estimateStorageBytes() const {
    size_type bytes = folly::goodMallocSize(sizeof(Pair) * Vector::capacity());
    if (tombstones_) {
      bytes += folly::goodMallocSize((tombstones_->capacity() + 7) / 8);
    }
    return bytes;
  }

  // Swap contents with another map.
  void swap(PathMap& other) noexcept {
    Vector::swap(other);
    std::swap(compare_, other.compare_);
    std::swap(sortedEnd_, other.sortedEnd_);
    std::swap(deadCount_, other.deadCount_);
    std::swap(tombstones_, other.tombstones_);
  }

  /** Fold the pending region into the sorted prefix and drop tombstoned
   * entries. O(size). Called automatically by insert and erase when the
   * pending region or tombstone count grows past a bound; exposed for
   * callers that finish a batch of mutations and want to pay the cost
   * eagerly. Invalidates iterators. */
  void compact() {
    if (deadCount_ != 0) {
      removeDead();
    }
    mergePending();
  }

  /**
   * Returns an iterator to the first entry whose key is not less than `key`.
   *
   * TODO(xavierd): a potential optimization for case sensitive PathMap would
   * be to first perform a case insensitive search, and then fallback to the
   * case sensitive search.
   */
  iterator lower_bound(Piece key) {
    return iterator{
        this, skipDead(prefixLowerBound(key)), pendingLowerBound(key)};
  }

  const_iterator lower_bound(Piece key) const {
    return const_iterator{
        this, skipDead(prefixLowerBound(key)), pendingLowerBound(key)};
  }

  /** Find using the Piece representation of a key.
   * Does not allocate a copy of the key string.
   */
  iterator find(Piece key) {
    return findImpl<iterator>(this, key);
  }

  /** Find using the Piece representation of a key.
   * Does not allocate a copy of the key string.
   */
  const_iterator find(Piece key) const {
    return findImpl<const_iterator>(this, key);
  }

  /** Insert a new key-value pair.
   * If the key already exists, it is left unaltered.
   * Returns a pair consisting of an iterator to the position for key and
   * a boolean that is true if an insert took place. */
  std::pair<iterator, bool> insert(const value_type& val) {
    return insertImpl(Piece(val.first), val.second);
  }

  /** Emplace a new key-value pair by constructing it in-place.
   * If the key already exists, it is left unaltered.
   * If an insertion happens, the args are forwarded to the Value
   * constructor.
   * Returns a pair consisting of an iterator to the position for key and
   * a boolean that is true if an insert took place. */
  template <typename... Args>
  std::pair<iterator, bool> emplace(Piece key, Args&&... args) {
    return insertImpl(key, std::forward<Args>(args)...);
  }

  /** Returns a reference to the map position for key, creating it needed.
   * If the key is already present, no additional allocations are performed.
   */
  mapped_type& operator[](Piece key) {
    return insertImpl(key).first->second;
  }

  /** Returns a reference to the map position for key, if present.
   * Throws std::out_of_range if the key is not present (this const
   * form is not allowed to mutate the map). */
  const mapped_type& operator[](Piece key) const {
    return at(key);
  }

  /** Returns a reference to the map position for key, if present.
   * Throws std::out_of_range if the key is not present. */
  mapped_type& at(Piece key) {
    auto iter = find(key);
    if (iter == end()) {
      throwf<std::out_of_range>("no such key {}", key);
    }
    return iter->second;
  }

  /** Returns a reference to the map position for key, if present.
   * Throws std::out_of_range if the key is not present. */
  const mapped_type& at(Piece key) const {
    const auto iter = find(key);
    if (iter == end()) {
      throwf<std::out_of_range>("no such key {}", key);
    }
    return iter->second;
  }

  /** Erase the value associated with key.
   * Does not allocate any additional memory to look up the key.
   * Returns the number of matching elements that were erased; this is
   * always either 1 or 0. */
  size_type erase(Piece key) {
    auto iter = find(key);
    if (iter == end()) {
      return 0;
    }
    // Unlike erase(pos), skip locating the following entry: that walk is
    // what would make erasing a map in descending key order quadratic.
    if (eraseAt(iter) == EraseAction::Tombstoned && deadCount_ > deadLimit()) {
      compact();
    }
    return 1;
  }

  /** Returns 1 if there is an entry with the given key and 0 otherwise. */
  size_type count(Piece key) const {
    auto iter = find(key);
    return iter != end();
  }

  CaseSensitivity getCaseSensitivity() const {
    return compare_.caseSensitive_;
  }

  template <typename V, typename K>
  friend class PathMapMutator;

 private:
  Pair& rawAt(VectorSizeType i) {
    return Vector::operator[](i);
  }
  const Pair& rawAt(VectorSizeType i) const {
    return Vector::operator[](i);
  }
  Pair* storage() {
    return Vector::data();
  }
  const Pair* storage() const {
    return Vector::data();
  }
  VectorSizeType rawSize() const {
    return Vector::size();
  }

  bool isDead(VectorSizeType i) const {
    return tombstones_ && i < tombstones_->size() && (*tombstones_)[i];
  }

  /** Returns the first non-tombstoned prefix index at or after `i`. */
  VectorSizeType skipDead(VectorSizeType i) const {
    if (!tombstones_) {
      return i;
    }
    while (i < sortedEnd_ && isDead(i)) {
      ++i;
    }
    return i;
  }

  /** Prefix index before which the pending entry at `pendingIdx` belongs:
   * the first live prefix entry with a greater key, or sortedEnd_ once the
   * pending region is exhausted. Keys are unique across the regions, so no
   * prefix key equals the pending key.
   *
   * `from` is a lower bound for the result: every live prefix entry before
   * it must precede the pending key. The search gallops outward from there
   * (probing 1, 3, 7, ... entries ahead, then binary-searching the last
   * gap), so a switch point next to `from`, as when pending entries are
   * interleaved with prefix entries, costs a couple of compares, while a
   * distant one still costs O(log distance). */
  VectorSizeType switchIndex(VectorSizeType pendingIdx, VectorSizeType from)
      const {
    if (pendingIdx >= rawSize()) {
      return sortedEnd_;
    }
    const auto key = rawAt(pendingIdx).first.piece();
    VectorSizeType low = from;
    VectorSizeType high = from;
    for (VectorSizeType step = 1;; step *= 2) {
      high = low + step;
      if (high >= sortedEnd_) {
        high = sortedEnd_;
        break;
      }
      if (compare_(key, rawAt(high).first)) {
        break;
      }
      low = high + 1;
    }
    const auto begin = Vector::begin();
    return skipDead(
        std::lower_bound(begin + low, begin + high, key, compare_) - begin);
  }

  /** Shared body of the two find overloads. With no pending region this is
   * one binary search plus one compare, like a plain sorted vector. */
  template <typename I, typename Self>
  static I findImpl(Self* self, Piece key) {
    const auto sortedEnd = self->sortedEnd_;
    const auto rawSize = self->rawSize();
    const auto prefixIdx = self->prefixLowerBound(key);
    if (prefixIdx != sortedEnd &&
        !self->compare_(key, self->rawAt(prefixIdx).first)) {
      // Keys are unique across both regions, dead entries included, so a
      // tombstoned match means the key is absent everywhere.
      if (self->isDead(prefixIdx)) {
        return I{self, sortedEnd, rawSize, sortedEnd};
      }
      if (sortedEnd == rawSize) {
        return I{self, prefixIdx, rawSize, sortedEnd};
      }
      return I{self, prefixIdx, self->pendingLowerBound(key)};
    }
    if (sortedEnd != rawSize) {
      const auto pendingIdx = self->pendingLowerBound(key);
      if (pendingIdx != rawSize &&
          !self->compare_(key, self->rawAt(pendingIdx).first)) {
        return I{self, self->skipDead(prefixIdx), pendingIdx};
      }
    }
    return I{self, sortedEnd, rawSize, sortedEnd};
  }

  void markDead(VectorSizeType i) {
    if (!tombstones_) {
      tombstones_ = std::make_unique<std::vector<bool>>();
    }
    if (tombstones_->size() < sortedEnd_) {
      tombstones_->resize(sortedEnd_, false);
    }
    (*tombstones_)[i] = true;
    ++deadCount_;
  }

  enum class EraseAction { RemovedPending, RemovedSorted, Tombstoned };

  /** Shared erase mechanics for the two erase overloads: pending entries
   * and small-map sorted entries are removed from the vector directly,
   * other sorted entries are tombstoned. The compaction check is left to
   * the caller, since erase(const_iterator) must capture its successor
   * before compaction moves entries. */
  EraseAction eraseAt(const const_iterator& pos) {
    if (!pos.onSorted()) {
      Vector::erase(Vector::begin() + pos.pendingIdx_);
      return EraseAction::RemovedPending;
    }
    if (rawSize() <= kMinCompactionBatch && deadCount_ == 0) {
      Vector::erase(Vector::begin() + pos.sortedIdx_);
      --sortedEnd_;
      return EraseAction::RemovedSorted;
    }
    markDead(pos.sortedIdx_);
    return EraseAction::Tombstoned;
  }

  /** Raw index of the first prefix entry (live or dead) not less than
   * `key`; sortedEnd_ if there is none. */
  VectorSizeType prefixLowerBound(Piece key) const {
    return std::lower_bound(
               Vector::begin(), Vector::begin() + sortedEnd_, key, compare_) -
        Vector::begin();
  }

  /** Raw index of the first pending entry not less than `key`; the raw
   * vector size if there is none. */
  VectorSizeType pendingLowerBound(Piece key) const {
    return std::lower_bound(
               Vector::begin() + sortedEnd_, Vector::end(), key, compare_) -
        Vector::begin();
  }

  VectorSizeType pendingLimit() const {
    return std::max<VectorSizeType>(
        kMinCompactionBatch,
        static_cast<VectorSizeType>(std::sqrt(static_cast<double>(rawSize()))));
  }

  VectorSizeType deadLimit() const {
    // Compact once tombstones exceed a quarter of the live entries (a
    // fifth of raw storage at that point); compaction cost stays amortized
    // O(1) per erase (~5 entry moves).
    return std::max<VectorSizeType>(kMinCompactionBatch, size() / 4);
  }

  template <typename... ValueArgs>
  std::pair<iterator, bool> insertImpl(Piece key, ValueArgs&&... valueArgs) {
    if (sortedEnd_ == rawSize() &&
        (rawSize() == 0 || compare_(rawAt(rawSize() - 1).first, key))) {
      // In-order append: the key is greater than every stored entry, so
      // grow the sorted prefix directly with no binary search. This keeps
      // bulk sorted insertion O(1) per entry.
      Vector::emplace_back(
          Key(key), Value(std::forward<ValueArgs>(valueArgs)...));
      ++sortedEnd_;
      return {iterator{this, sortedEnd_ - 1, rawSize()}, true};
    }
    const auto prefixIdx = prefixLowerBound(key);
    const bool prefixMatch =
        prefixIdx != sortedEnd_ && !compare_(key, rawAt(prefixIdx).first);
    if (prefixMatch && !isDead(prefixIdx)) {
      return {iterator{this, prefixIdx, pendingLowerBound(key)}, false};
    }
    const auto pendingIdx = pendingLowerBound(key);
    if (pendingIdx != rawSize() && !compare_(key, rawAt(pendingIdx).first)) {
      return {iterator{this, skipDead(prefixIdx), pendingIdx}, false};
    }
    if (prefixMatch) {
      // Revive the tombstoned entry in place. The new key replaces the old
      // one; they may differ by case.
      rawAt(prefixIdx).first = Key(key);
      rawAt(prefixIdx).second = Value(std::forward<ValueArgs>(valueArgs)...);
      (*tombstones_)[prefixIdx] = false;
      if (--deadCount_ == 0) {
        // Drop the bitmap so lookups and iteration take the tombstone-free
        // path, as after a compaction.
        tombstones_.reset();
      }
      return {iterator{this, prefixIdx, pendingIdx}, true};
    }
    Vector::emplace(
        Vector::begin() + pendingIdx,
        Key(key),
        Value(std::forward<ValueArgs>(valueArgs)...));
    if (rawSize() - sortedEnd_ > pendingLimit()) {
      compact();
      // Compaction moved the new entry; re-find it.
      return {lower_bound(key), true};
    }
    return {iterator{this, skipDead(prefixIdx), pendingIdx}, true};
  }

  /** Rewrite the vector without the tombstoned entries. */
  void removeDead() {
    VectorSizeType out = 0;
    for (VectorSizeType i = 0; i < sortedEnd_; ++i) {
      if (!isDead(i)) {
        if (out != i) {
          rawAt(out) = std::move(rawAt(i));
        }
        ++out;
      }
    }
    const auto newSortedEnd = out;
    for (VectorSizeType i = sortedEnd_; i < rawSize(); ++i) {
      rawAt(out) = std::move(rawAt(i));
      ++out;
    }
    Vector::erase(Vector::begin() + out, Vector::end());
    sortedEnd_ = newSortedEnd;
    deadCount_ = 0;
    // Drop the bitmap so lookups and iteration take the tombstone-free
    // path until the next erase.
    tombstones_.reset();
  }

  /** Merge the pending region into the sorted prefix. */
  void mergePending() {
    if (sortedEnd_ == rawSize()) {
      return;
    }
    if (sortedEnd_ != 0 &&
        !compare_(rawAt(sortedEnd_ - 1).first, rawAt(sortedEnd_).first)) {
      std::inplace_merge(
          Vector::begin(),
          Vector::begin() + sortedEnd_,
          Vector::end(),
          compare_);
    }
    sortedEnd_ = rawSize();
  }
};

/// Equality operator.
template <typename V, typename K>
bool operator==(const PathMap<V, K>& lhs, const PathMap<V, K>& rhs) {
  return lhs.size() == rhs.size() &&
      std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

/// Inequality operator.
template <typename V, typename K>
bool operator!=(const PathMap<V, K>& lhs, const PathMap<V, K>& rhs) {
  return !(lhs == rhs);
}

/**
 * Collate two path maps with different value types.
 *
 * Given two PathMaps with the same key type, produces a new PathMap where
 * entries from both are collated by key.  The resulting PathMap's value type
 * will be a pair of optionals, where one or both optionals will be present
 * depending on whether there was a corresponding entry in the inputs.
 */
template <typename A, typename B, typename P = PathComponent>
PathMap<std::pair<std::optional<A>, std::optional<B>>, P> collatePathMaps(
    PathMap<A, P> a,
    PathMap<B, P> b) {
  auto caseSensitivity = CaseSensitivity::Insensitive;
  if (a.getCaseSensitivity() == b.getCaseSensitivity()) {
    caseSensitivity = a.getCaseSensitivity();
  } else {
    XLOG(WARN, "Comparing path maps with disjoint case sensitivity");
  }

  auto result = PathMap<std::pair<std::optional<A>, std::optional<B>>, P>{
      {}, caseSensitivity};

  auto aIt = a.begin();
  auto bIt = b.begin();
  while (aIt != a.end() && bIt != b.end()) {
    if (isPathPieceEqual(aIt->first, bIt->first, caseSensitivity)) {
      result.emplace(aIt->first, std::make_pair(aIt->second, bIt->second));
      ++aIt;
      ++bIt;
    } else if (isPathPieceLess(aIt->first, bIt->first, caseSensitivity)) {
      result.emplace(aIt->first, std::make_pair(aIt->second, std::nullopt));
      ++aIt;
    } else {
      result.emplace(bIt->first, std::make_pair(std::nullopt, bIt->second));
      ++bIt;
    }
  }
  while (aIt != a.end()) {
    result.emplace(aIt->first, std::make_pair(aIt->second, std::nullopt));
    ++aIt;
  }
  while (bIt != b.end()) {
    result.emplace(bIt->first, std::make_pair(std::nullopt, bIt->second));
    ++bIt;
  }
  return result;
}

} // namespace facebook::eden
