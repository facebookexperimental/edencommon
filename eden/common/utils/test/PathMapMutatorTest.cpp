/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "eden/common/utils/PathMapMutator.h"
#include <folly/portability/GTest.h>

using namespace facebook::eden;
using namespace facebook::eden::path_literals;

TEST(PathMapMutator, noChanges) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));
  map.insert(std::make_pair(PathComponent("b"), 2));
  map.insert(std::make_pair(PathComponent("c"), 3));

  PathMapMutator<int> mutator(std::move(map));
  auto result = std::move(mutator).finalize();

  EXPECT_EQ(3, result.size());
  EXPECT_EQ(1, result.at("a"_pc));
  EXPECT_EQ(2, result.at("b"_pc));
  EXPECT_EQ(3, result.at("c"_pc));
}

TEST(PathMapMutator, eraseByIterator) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));
  map.insert(std::make_pair(PathComponent("b"), 2));
  map.insert(std::make_pair(PathComponent("c"), 3));

  PathMapMutator<int> mutator(std::move(map));
  auto it = mutator.find("b"_pc);
  ASSERT_NE(it, mutator.end());
  mutator.erase(it);

  // find should not return erased entries.
  EXPECT_EQ(mutator.find("b"_pc), mutator.end());

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());
  EXPECT_EQ(1, result.at("a"_pc));
  EXPECT_EQ(3, result.at("c"_pc));
  EXPECT_EQ(result.find("b"_pc), result.end());
}

TEST(PathMapMutator, eraseByKey) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));
  map.insert(std::make_pair(PathComponent("b"), 2));

  PathMapMutator<int> mutator(std::move(map));
  EXPECT_EQ(1, mutator.erase("b"_pc));
  EXPECT_EQ(0, mutator.erase("b"_pc)); // already erased
  EXPECT_EQ(0, mutator.erase("z"_pc)); // doesn't exist

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(1, result.size());
  EXPECT_EQ(1, result.at("a"_pc));
}

TEST(PathMapMutator, emplaceNew) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("b"), 2));

  PathMapMutator<int> mutator(std::move(map));
  auto [it1, inserted1] = mutator.emplace("a"_pc, 1);
  EXPECT_TRUE(inserted1);
  auto [it2, inserted2] = mutator.emplace("c"_pc, 3);
  EXPECT_TRUE(inserted2);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(3, result.size());
  EXPECT_EQ(1, result.at("a"_pc));
  EXPECT_EQ(2, result.at("b"_pc));
  EXPECT_EQ(3, result.at("c"_pc));
}

TEST(PathMapMutator, emplaceExisting) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));

  PathMapMutator<int> mutator(std::move(map));
  auto [it, inserted] = mutator.emplace("a"_pc, 99);
  EXPECT_FALSE(inserted);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(1, result.size());
  EXPECT_EQ(1, result.at("a"_pc)); // unchanged
}

TEST(PathMapMutator, eraseAndReplace) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));
  map.insert(std::make_pair(PathComponent("b"), 2));

  PathMapMutator<int> mutator(std::move(map));
  mutator.erase("a"_pc);
  auto [it, inserted] = mutator.emplace("a"_pc, 99);
  EXPECT_TRUE(inserted);

  // find should return the replaced entry.
  auto found = mutator.find("a"_pc);
  ASSERT_NE(found, mutator.end());
  EXPECT_EQ(99, found->second);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());
  EXPECT_EQ(99, result.at("a"_pc));
  EXPECT_EQ(2, result.at("b"_pc));
}

TEST(PathMapMutator, eraseAndInsertDifferentKey) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));
  map.insert(std::make_pair(PathComponent("c"), 3));

  PathMapMutator<int> mutator(std::move(map));
  mutator.erase("a"_pc);
  mutator.emplace("b"_pc, 2);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());
  EXPECT_EQ(result.find("a"_pc), result.end());
  EXPECT_EQ(2, result.at("b"_pc));
  EXPECT_EQ(3, result.at("c"_pc));
}

TEST(PathMapMutator, bulkEraseAndInsert) {
  // Simulate a checkout-like workload: erase many old entries, insert
  // many new entries.
  PathMap<int> map(CaseSensitivity::Sensitive);
  for (int i = 0; i < 100; ++i) {
    map.insert(std::make_pair(PathComponent(fmt::format("old_{:03d}", i)), i));
  }
  // Also add some entries that won't be touched.
  for (int i = 0; i < 50; ++i) {
    map.insert(
        std::make_pair(PathComponent(fmt::format("keep_{:03d}", i)), i + 1000));
  }

  PathMapMutator<int> mutator(std::move(map));

  // Erase all "old_" entries.
  for (int i = 0; i < 100; ++i) {
    auto key = PathComponent(fmt::format("old_{:03d}", i));
    mutator.erase(PathComponentPiece(key));
  }

  // Insert new entries.
  for (int i = 0; i < 100; ++i) {
    mutator.emplace(
        PathComponentPiece(PathComponent(fmt::format("new_{:03d}", i))),
        i + 2000);
  }

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(150, result.size());

  // Old entries should be gone.
  for (int i = 0; i < 100; ++i) {
    auto key = PathComponent(fmt::format("old_{:03d}", i));
    EXPECT_EQ(result.find(PathComponentPiece(key)), result.end());
  }

  // Keep entries should be unchanged.
  for (int i = 0; i < 50; ++i) {
    auto key = PathComponent(fmt::format("keep_{:03d}", i));
    EXPECT_EQ(i + 1000, result.at(PathComponentPiece(key)));
  }

  // New entries should be present.
  for (int i = 0; i < 100; ++i) {
    auto key = PathComponent(fmt::format("new_{:03d}", i));
    EXPECT_EQ(i + 2000, result.at(PathComponentPiece(key)));
  }
}

TEST(PathMapMutator, resultIsSorted) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("c"), 3));
  map.insert(std::make_pair(PathComponent("f"), 6));

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("a"_pc, 1);
  mutator.emplace("d"_pc, 4);
  mutator.emplace("g"_pc, 7);
  mutator.erase("c"_pc);

  auto result = std::move(mutator).finalize();

  // Should be sorted: a, d, f, g
  ASSERT_EQ(4, result.size());
  auto it = result.begin();
  EXPECT_EQ("a"_pc, it->first);
  ++it;
  EXPECT_EQ("d"_pc, it->first);
  ++it;
  EXPECT_EQ("f"_pc, it->first);
  ++it;
  EXPECT_EQ("g"_pc, it->first);
}

TEST(PathMapMutator, outOfOrderAppendThenErase) {
  // When suffix entries arrive out of order, sorting must remap erased
  // indices correctly.
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("m"), 1));

  PathMapMutator<int> mutator(std::move(map));

  // Append out of order (z before a).
  mutator.emplace("z"_pc, 26);
  mutator.emplace("a"_pc, 1); // triggers suffixNeedsSort_

  // Erase "z" — currently at some index in the unsorted suffix.
  mutator.erase("z"_pc);

  // find should not find erased entry.
  EXPECT_EQ(mutator.find("z"_pc), mutator.end());
  // find should still find non-erased suffix entry.
  EXPECT_NE(mutator.find("a"_pc), mutator.end());
  EXPECT_EQ(1, mutator.find("a"_pc)->second);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());
  EXPECT_EQ(1, result.at("a"_pc));
  EXPECT_EQ(1, result.at("m"_pc));
  EXPECT_EQ(result.find("z"_pc), result.end());
}

TEST(PathMapMutator, outOfOrderAppendEraseAndReemplace) {
  PathMap<int> map(CaseSensitivity::Sensitive);

  PathMapMutator<int> mutator(std::move(map));

  // Out of order appends.
  mutator.emplace("c"_pc, 3);
  mutator.emplace("a"_pc, 1);
  mutator.emplace("b"_pc, 2);

  // Erase one, re-emplace with new value.
  mutator.erase("a"_pc);
  mutator.emplace("a"_pc, 10);

  auto result = std::move(mutator).finalize();
  ASSERT_EQ(3, result.size());
  EXPECT_EQ(10, result.at("a"_pc));
  EXPECT_EQ(2, result.at("b"_pc));
  EXPECT_EQ(3, result.at("c"_pc));
}

// ============================================================
// Tests for find/erase/emplace consistency across sorted prefix and suffix
// ============================================================

TEST(PathMapMutator, findNewlyEmplacedEntry) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("b"), 2));

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("z"_pc, 99);

  // find() should see the newly emplaced entry.
  auto it = mutator.find("z"_pc);
  ASSERT_NE(it, mutator.end());
  EXPECT_EQ(99, it->second);
}

TEST(PathMapMutator, eraseNewlyEmplacedEntry) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("z"_pc, 99);

  // Erase the newly emplaced entry.
  EXPECT_EQ(1, mutator.erase("z"_pc));
  EXPECT_EQ(mutator.find("z"_pc), mutator.end());

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(1, result.size());
  EXPECT_EQ(1, result.at("a"_pc));
  EXPECT_EQ(result.find("z"_pc), result.end());
}

TEST(PathMapMutator, emplaceSameNewKeyTwice) {
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("a"), 1));

  PathMapMutator<int> mutator(std::move(map));
  auto [it1, ins1] = mutator.emplace("z"_pc, 10);
  EXPECT_TRUE(ins1);
  EXPECT_EQ(10, it1->second);

  // Second emplace with same key should return existing, not insert.
  auto [it2, ins2] = mutator.emplace("z"_pc, 20);
  EXPECT_FALSE(ins2);
  EXPECT_EQ(10, it2->second); // original value preserved

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());
  EXPECT_EQ(10, result.at("z"_pc));
}

TEST(PathMapMutator, emplaceEraseEmplaceNewKey) {
  // emplace new key, erase it, emplace it again with different value.
  PathMap<int> map(CaseSensitivity::Sensitive);

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("x"_pc, 1);
  mutator.erase("x"_pc);
  mutator.emplace("x"_pc, 2);

  auto it = mutator.find("x"_pc);
  ASSERT_NE(it, mutator.end());
  EXPECT_EQ(2, it->second);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(1, result.size());
  EXPECT_EQ(2, result.at("x"_pc));
}

TEST(PathMapMutator, eraseNewlyEmplacedByIterator) {
  PathMap<int> map(CaseSensitivity::Sensitive);

  PathMapMutator<int> mutator(std::move(map));
  auto [it, inserted] = mutator.emplace("x"_pc, 42);
  ASSERT_TRUE(inserted);
  ASSERT_NE(it, mutator.end());

  mutator.erase(it);
  EXPECT_EQ(mutator.find("x"_pc), mutator.end());

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(0, result.size());
}

TEST(PathMapMutator, mixedSortedAndUnsortedOperations) {
  // Interleave operations on entries in sorted prefix and unsorted suffix.
  PathMap<int> map(CaseSensitivity::Sensitive);
  map.insert(std::make_pair(PathComponent("b"), 2));
  map.insert(std::make_pair(PathComponent("d"), 4));

  PathMapMutator<int> mutator(std::move(map));

  // Erase from sorted prefix.
  mutator.erase("b"_pc);

  // Add to suffix.
  mutator.emplace("a"_pc, 1);
  mutator.emplace("c"_pc, 3);
  mutator.emplace("e"_pc, 5);

  // Erase from suffix.
  mutator.erase("c"_pc);

  // Replace in sorted prefix (was erased).
  mutator.emplace("b"_pc, 20);

  // Verify all finds work.
  EXPECT_EQ(1, mutator.find("a"_pc)->second);
  EXPECT_EQ(20, mutator.find("b"_pc)->second);
  EXPECT_EQ(mutator.find("c"_pc), mutator.end());
  EXPECT_EQ(4, mutator.find("d"_pc)->second);
  EXPECT_EQ(5, mutator.find("e"_pc)->second);

  auto result = std::move(mutator).finalize();
  ASSERT_EQ(4, result.size());

  auto it = result.begin();
  EXPECT_EQ("a"_pc, it->first);
  EXPECT_EQ(1, it->second);
  ++it;
  EXPECT_EQ("b"_pc, it->first);
  EXPECT_EQ(20, it->second);
  ++it;
  EXPECT_EQ("d"_pc, it->first);
  EXPECT_EQ(4, it->second);
  ++it;
  EXPECT_EQ("e"_pc, it->first);
  EXPECT_EQ(5, it->second);
}

// ============================================================
// Case-insensitive tests
// ============================================================

TEST(PathMapMutator, caseInsensitiveFind) {
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("Foo"), 1));

  PathMapMutator<int> mutator(std::move(map));

  // Find should match regardless of case.
  EXPECT_NE(mutator.find("Foo"_pc), mutator.end());
  EXPECT_NE(mutator.find("foo"_pc), mutator.end());
  EXPECT_NE(mutator.find("FOO"_pc), mutator.end());
  EXPECT_EQ(1, mutator.find("foo"_pc)->second);
}

TEST(PathMapMutator, caseInsensitiveFindAfterErase) {
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("Foo"), 1));

  PathMapMutator<int> mutator(std::move(map));
  mutator.erase("foo"_pc);

  // All case variants should be not-found after erase.
  EXPECT_EQ(mutator.find("Foo"_pc), mutator.end());
  EXPECT_EQ(mutator.find("foo"_pc), mutator.end());
  EXPECT_EQ(mutator.find("FOO"_pc), mutator.end());
}

TEST(PathMapMutator, caseInsensitiveEraseByKey) {
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("Foo"), 1));

  PathMapMutator<int> mutator(std::move(map));

  // Erase using different case should work.
  EXPECT_EQ(1, mutator.erase("FOO"_pc));
  EXPECT_EQ(0, mutator.erase("foo"_pc)); // already erased

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(0, result.size());
}

TEST(PathMapMutator, caseInsensitiveEraseAndReplace) {
  // Erase "foo" then emplace "FOO" — should update key casing and value.
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("foo"), 1));
  map.insert(std::make_pair(PathComponent("bar"), 2));

  PathMapMutator<int> mutator(std::move(map));

  auto it = mutator.find("foo"_pc);
  ASSERT_NE(it, mutator.end());
  mutator.erase(it);
  auto [newIt, inserted] = mutator.emplace("FOO"_pc, 99);
  EXPECT_TRUE(inserted);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());

  auto found = result.find("FOO"_pc);
  ASSERT_NE(found, result.end());
  EXPECT_EQ("FOO"_pc, found->first); // casing updated
  EXPECT_EQ(99, found->second);
}

TEST(PathMapMutator, caseInsensitiveEmplaceExistingNoChange) {
  // Emplace with different case on a non-erased entry should NOT replace.
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("foo"), 1));

  PathMapMutator<int> mutator(std::move(map));
  auto [it, inserted] = mutator.emplace("FOO"_pc, 99);
  EXPECT_FALSE(inserted); // already exists, not erased

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(1, result.size());
  EXPECT_EQ(1, result.at("foo"_pc)); // value unchanged
  EXPECT_EQ("foo"_pc, result.begin()->first); // key casing unchanged
}

TEST(PathMapMutator, caseInsensitiveNewEntryDoesNotCollide) {
  // Insert a truly new entry that happens to differ only in case from an
  // erased entry. Since the erased entry is un-erased in-place, this should
  // work correctly.
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("aaa"), 1));
  map.insert(std::make_pair(PathComponent("zzz"), 2));

  PathMapMutator<int> mutator(std::move(map));
  mutator.erase("aaa"_pc);
  mutator.emplace("AAA"_pc, 10);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(2, result.size());
  EXPECT_EQ("AAA"_pc, result.begin()->first);
  EXPECT_EQ(10, result.begin()->second);
}

TEST(PathMapMutator, caseInsensitiveBulkCaseChange) {
  // Simulate a checkout that changes casing of multiple entries.
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("alpha"), 1));
  map.insert(std::make_pair(PathComponent("beta"), 2));
  map.insert(std::make_pair(PathComponent("gamma"), 3));
  map.insert(std::make_pair(PathComponent("unchanged"), 4));

  PathMapMutator<int> mutator(std::move(map));

  // Change casing of alpha, beta, gamma.
  mutator.erase("alpha"_pc);
  mutator.emplace("ALPHA"_pc, 10);
  mutator.erase("beta"_pc);
  mutator.emplace("BETA"_pc, 20);
  mutator.erase("gamma"_pc);
  mutator.emplace("GAMMA"_pc, 30);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(4, result.size());

  // Verify all entries have updated casing and values.
  auto a = result.find("alpha"_pc);
  ASSERT_NE(a, result.end());
  EXPECT_EQ("ALPHA"_pc, a->first);
  EXPECT_EQ(10, a->second);

  auto b = result.find("beta"_pc);
  ASSERT_NE(b, result.end());
  EXPECT_EQ("BETA"_pc, b->first);
  EXPECT_EQ(20, b->second);

  auto g = result.find("gamma"_pc);
  ASSERT_NE(g, result.end());
  EXPECT_EQ("GAMMA"_pc, g->first);
  EXPECT_EQ(30, g->second);

  // Unchanged entry should be untouched.
  auto u = result.find("unchanged"_pc);
  ASSERT_NE(u, result.end());
  EXPECT_EQ("unchanged"_pc, u->first);
  EXPECT_EQ(4, u->second);
}

TEST(PathMapMutator, caseInsensitiveEraseAndInsertDifferentName) {
  // Erase "foo", insert "BAR" — no case collision, both should work.
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("foo"), 1));

  PathMapMutator<int> mutator(std::move(map));
  mutator.erase("foo"_pc);
  mutator.emplace("BAR"_pc, 2);

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(1, result.size());
  EXPECT_EQ(result.find("foo"_pc), result.end());
  EXPECT_EQ(2, result.at("BAR"_pc));
}

TEST(PathMapMutator, caseInsensitiveResultIsSorted) {
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("charlie"), 3));
  map.insert(std::make_pair(PathComponent("echo"), 5));

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("ALPHA"_pc, 1);
  mutator.emplace("DELTA"_pc, 4);
  mutator.erase("charlie"_pc);
  mutator.emplace("CHARLIE"_pc, 30);

  auto result = std::move(mutator).finalize();

  // Should be sorted case-insensitively: ALPHA, CHARLIE, DELTA, echo
  ASSERT_EQ(4, result.size());
  auto it = result.begin();
  EXPECT_EQ("ALPHA"_pc, it->first);
  EXPECT_EQ(1, it->second);
  ++it;
  EXPECT_EQ("CHARLIE"_pc, it->first);
  EXPECT_EQ(30, it->second);
  ++it;
  EXPECT_EQ("DELTA"_pc, it->first);
  EXPECT_EQ(4, it->second);
  ++it;
  EXPECT_EQ("echo"_pc, it->first);
  EXPECT_EQ(5, it->second);
}

TEST(PathMapMutator, caseInsensitiveFindNewlyEmplaced) {
  PathMap<int> map(CaseSensitivity::Insensitive);
  map.insert(std::make_pair(PathComponent("aaa"), 1));

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("ZZZ"_pc, 99);

  // Find with different case should work.
  auto it = mutator.find("zzz"_pc);
  ASSERT_NE(it, mutator.end());
  EXPECT_EQ("ZZZ"_pc, it->first);
  EXPECT_EQ(99, it->second);
}

TEST(PathMapMutator, caseInsensitiveEraseNewlyEmplaced) {
  PathMap<int> map(CaseSensitivity::Insensitive);

  PathMapMutator<int> mutator(std::move(map));
  mutator.emplace("Foo"_pc, 1);

  // Erase with different case.
  EXPECT_EQ(1, mutator.erase("FOO"_pc));
  EXPECT_EQ(mutator.find("foo"_pc), mutator.end());

  auto result = std::move(mutator).finalize();
  EXPECT_EQ(0, result.size());
}

TEST(PathMapMutator, caseInsensitiveEmplaceSameNewKeyTwice) {
  PathMap<int> map(CaseSensitivity::Insensitive);

  PathMapMutator<int> mutator(std::move(map));
  auto [it1, ins1] = mutator.emplace("Foo"_pc, 10);
  EXPECT_TRUE(ins1);

  // Same key, different case — should find existing.
  auto [it2, ins2] = mutator.emplace("FOO"_pc, 20);
  EXPECT_FALSE(ins2);
  EXPECT_EQ(10, it2->second); // original value preserved
  EXPECT_EQ("Foo"_pc, it2->first); // original casing preserved
}
