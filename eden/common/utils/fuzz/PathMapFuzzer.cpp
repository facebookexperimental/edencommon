/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <map>
#include <string>
#include <utility>

#include <folly/logging/xlog.h>

#include "eden/common/utils/CaseSensitivity.h"
#include "eden/common/utils/PathFuncs.h"
#include "eden/common/utils/PathMap.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

namespace facebook::eden {

/**
 * Checks the internal structural invariants of a PathMap: region bounds,
 * tombstone accounting, sortedness of both regions, key uniqueness across
 * regions, and the compaction bounds that every mutating operation must
 * re-establish before returning.
 */
struct PathMapTestAccess {
  template <typename V, typename K>
  static void checkInvariants(const PathMap<V, K>& map) {
    const auto raw = map.rawSize();
    const auto sortedEnd = map.sortedEnd_;
    XCHECK_LE(sortedEnd, raw);

    // Tombstone accounting: deadCount_ matches the set bits, no bit is set
    // outside the sorted prefix, and a missing bitmap means no dead entries.
    size_t dead = 0;
    if (map.tombstones_) {
      const auto& bits = *map.tombstones_;
      for (size_t i = 0; i < bits.size(); ++i) {
        if (bits[i]) {
          XCHECK_LT(i, sortedEnd) << "tombstone outside the sorted prefix";
          ++dead;
        }
      }
    }
    XCHECK_EQ(dead, map.deadCount_);
    if (!map.tombstones_) {
      XCHECK_EQ(0u, map.deadCount_);
    }
    XCHECK_EQ(raw - map.deadCount_, map.size());

    // Both regions are strictly sorted (dead entries included: tombstoning
    // does not move entries).
    for (size_t i = 1; i < sortedEnd; ++i) {
      XCHECK(map.compare_(map.rawAt(i - 1).first, map.rawAt(i).first))
          << "sorted prefix out of order at " << i;
    }
    for (size_t i = sortedEnd + 1; i < raw; ++i) {
      XCHECK(map.compare_(map.rawAt(i - 1).first, map.rawAt(i).first))
          << "pending region out of order at " << i;
    }

    // No pending key may match any prefix key, live or dead: an insert that
    // matches a live prefix entry returns it, and one that matches a dead
    // prefix entry revives it in place.
    for (size_t p = sortedEnd; p < raw; ++p) {
      for (size_t s = 0; s < sortedEnd; ++s) {
        const bool equal =
            !map.compare_(map.rawAt(p).first, map.rawAt(s).first) &&
            !map.compare_(map.rawAt(s).first, map.rawAt(p).first);
        XCHECK(!equal) << "pending key duplicates a prefix key";
      }
    }

    // Mutating operations compact before returning when either region
    // outgrows its bound.
    XCHECK_LE(raw - sortedEnd, map.pendingLimit())
        << "pending region over limit";
    XCHECK_LE(map.deadCount_, map.deadLimit())
        << "dead entries over the compaction threshold";
  }
};

namespace {

using namespace facebook::security::lionhead;

/// Orders std::string keys exactly like PathMap orders its entries.
struct ModelLess {
  CaseSensitivity caseSensitive;
  bool operator()(const std::string& a, const std::string& b) const {
    return isPathPieceLess(
        PathComponentPiece{a}, PathComponentPiece{b}, caseSensitive);
  }
};

/// Reference model: the stored key keeps the case it was first inserted
/// with, matching PathMap's behavior.
using Model = std::map<std::string, int, ModelLess>;

/**
 * Mostly short keys over the alphabet {a, A, b, B}, so case-insensitive
 * collisions between distinct spellings are the common case, plus numbered
 * filler keys ("k0".."k63") so maps grow far past the internal 32-entry
 * compaction thresholds even in case-insensitive mode, where the colliding
 * alphabet alone yields too few distinct keys.
 */
template <typename Fdp>
std::string genKey(Fdp& f) {
  if (f.u8_range("key_form", 0, 2) == 2) {
    return "k" + std::to_string(f.index("key_num", 64));
  }
  auto key = f.str("key", f.d_str(f.d_chars("aAbB"), f.d_len(5)));
  if (key.empty()) {
    key = "a";
  }
  return key;
}

/// Full differential check: internal invariants, then exact (key, value)
/// sequence equality against the model, including key case.
void verify(const PathMap<int>& map, const Model& model) {
  PathMapTestAccess::checkInvariants(map);
  XCHECK_EQ(map.size(), model.size());
  auto it = map.begin();
  for (const auto& [key, value] : model) {
    XCHECK(it != map.end());
    XCHECK_EQ(it->first.view(), key) << "iteration key mismatch";
    XCHECK_EQ(it->second, value) << "iteration value mismatch";
    ++it;
  }
  XCHECK(it == map.end());
}

} // namespace

FUZZ(PathMap, FuzzMutationsAgainstModel) {
  const auto caseSensitive = f.boolean("case_sensitive")
      ? CaseSensitivity::Sensitive
      : CaseSensitivity::Insensitive;
  PathMap<int> map{caseSensitive};
  Model model{ModelLess{caseSensitive}};
  int nextValue = 0;

  while (f.has_more_data()) {
    switch (f.u8_range("op", 0, 10)) {
      case 0: { // insert: existing entries (any case) are left alone
        const auto key = genKey(f);
        const int value = nextValue++;
        auto [modelIt, modelInserted] = model.emplace(key, value);
        auto [mapIt, mapInserted] =
            map.insert(std::make_pair(PathComponent{key}, value));
        XCHECK_EQ(modelInserted, mapInserted);
        XCHECK_EQ(mapIt->first.view(), modelIt->first);
        XCHECK_EQ(mapIt->second, modelIt->second);
        break;
      }
      case 1: { // emplace: same semantics as insert
        const auto key = genKey(f);
        const int value = nextValue++;
        auto [modelIt, modelInserted] = model.emplace(key, value);
        auto [mapIt, mapInserted] = map.emplace(PathComponentPiece{key}, value);
        XCHECK_EQ(modelInserted, mapInserted);
        XCHECK_EQ(mapIt->first.view(), modelIt->first);
        XCHECK_EQ(mapIt->second, modelIt->second);
        break;
      }
      case 2: { // insert_or_assign: overwrites the value, keeps the key case
        const auto key = genKey(f);
        const int value = nextValue++;
        auto [modelIt, modelInserted] = model.insert_or_assign(key, value);
        auto [mapIt, mapInserted] =
            map.insert_or_assign(PathComponentPiece{key}, value);
        XCHECK_EQ(modelInserted, mapInserted);
        XCHECK_EQ(mapIt->first.view(), modelIt->first);
        XCHECK_EQ(mapIt->second, modelIt->second);
        break;
      }
      case 3: { // operator[]: value assignment through the reference
        const auto key = genKey(f);
        const int value = nextValue++;
        model[key] = value;
        map[PathComponentPiece{key}] = value;
        break;
      }
      case 4: { // erase by key; a revival via a later insert changes case
        const auto key = genKey(f);
        const auto modelErased = model.erase(key);
        const auto mapErased = map.erase(PathComponentPiece{key});
        XCHECK_EQ(modelErased, mapErased);
        break;
      }
      case 5: { // erase(iterator): the returned iterator resumes correctly
        if (map.empty()) {
          break;
        }
        const auto index = f.index("erase_index", map.size());
        auto mapIt = map.begin();
        auto modelIt = model.begin();
        for (size_t i = 0; i < index; ++i) {
          ++mapIt;
          ++modelIt;
        }
        auto mapNext = map.erase(mapIt);
        auto modelNext = model.erase(modelIt);
        while (modelNext != model.end()) {
          XCHECK(mapNext != map.end());
          XCHECK_EQ(mapNext->first.view(), modelNext->first);
          XCHECK_EQ(mapNext->second, modelNext->second);
          ++mapNext;
          ++modelNext;
        }
        XCHECK(mapNext == map.end());
        break;
      }
      case 6: { // lookups: find, count, lower_bound agree with the model
        const auto key = genKey(f);
        const auto piece = PathComponentPiece{key};
        const auto modelIt = model.find(key);
        const auto mapIt = map.find(piece);
        XCHECK_EQ(modelIt == model.end(), mapIt == map.end());
        if (modelIt != model.end()) {
          XCHECK_EQ(mapIt->first.view(), modelIt->first);
          XCHECK_EQ(mapIt->second, modelIt->second);
          XCHECK_EQ(map.at(piece), modelIt->second);
        }
        XCHECK_EQ(model.count(key), map.count(piece));
        auto modelLb = model.lower_bound(key);
        auto mapLb = map.lower_bound(piece);
        if (modelIt != model.end()) {
          XCHECK(mapLb == mapIt) << "lower_bound of a present key is find";
        }
        // Walk from the lookup position to the end: an iterator built at an
        // arbitrary position must merge the two regions from there exactly
        // as one built at begin() does.
        while (modelLb != model.end()) {
          XCHECK(mapLb != map.end());
          const auto& entry = *mapLb++;
          XCHECK_EQ(entry.first.view(), modelLb->first);
          XCHECK_EQ(entry.second, modelLb->second);
          ++modelLb;
        }
        XCHECK(mapLb == map.end());
        break;
      }
      case 7: // explicit compaction is always allowed
        map.compact();
        break;
      case 8: { // copies contain live entries and mutate independently
        PathMap<int> copy{map};
        XCHECK(copy == map);
        verify(copy, model);
        copy.clear();
        XCHECK(copy.empty());
        break;
      }
      case 9:
        map.clear();
        model.clear();
        break;
      case 10: { // bulk populate: push the map past the compaction bounds
        const auto count = f.u8_range("bulk_count", 1, 64);
        for (uint8_t i = 0; i < count; ++i) {
          const auto key = "k" + std::to_string(i);
          const int value = nextValue++;
          model.emplace(key, value);
          map.emplace(PathComponentPiece{key}, value);
        }
        break;
      }
    }
    verify(map, model);
  }
}

FUZZ(PathMap, FuzzVectorConstructor) {
  const auto caseSensitive = f.boolean("case_sensitive")
      ? CaseSensitivity::Sensitive
      : CaseSensitivity::Insensitive;

  // Arbitrary order with duplicates, including case-insensitive ones; the
  // constructor must sort and keep the earliest entry for each key.
  folly::fbvector<std::pair<PathComponent, int>> entries;
  Model model{ModelLess{caseSensitive}};
  int value = 0;
  while (f.has_more_data() && entries.size() < 512) {
    const auto key = genKey(f);
    entries.emplace_back(PathComponent{key}, value);
    model.emplace(key, value);
    ++value;
  }

  PathMap<int> map{std::move(entries), caseSensitive};
  verify(map, model);

  // The constructed representation must also mutate correctly.
  if (!model.empty()) {
    const auto& victim = model.begin()->first;
    XCHECK_EQ(1u, map.erase(PathComponentPiece{victim}));
    model.erase(model.begin());
    verify(map, model);
  }
  map.insert_or_assign(PathComponentPiece{std::string{"aA"}}, value);
  model.insert_or_assign("aA", value);
  verify(map, model);
}

} // namespace facebook::eden
