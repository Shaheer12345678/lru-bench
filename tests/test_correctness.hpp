#pragma once

// Shared correctness suite for every cache design in the repo.
//
// The suite is written once, as GoogleTest typed-parameterized tests, and each implementation
// instantiates it from its own test executable. Passing the identical test bodies is the evidence
// that the designs are behaviourally interchangeable, so these tests must never be edited to suit
// a particular implementation.
//
// An implementation plugs in through a factory type (the factories live in factories.hpp):
//
//     struct Factory {
//         template <typename K, typename V> using cache = ...;
//         template <typename K, typename V> static cache<K, V> make(std::size_t capacity);
//     };
//
// Caches own a mutex and cannot be moved, so make() relies on guaranteed copy elision. The factory
// indirection also lets a design with extra construction parameters (such as a shard count) choose
// them without the tests knowing.
//
// Include this header from exactly one translation unit per test binary: typed-parameterized test
// bodies are ordinary function definitions.

#include <gtest/gtest.h>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "lru/Policy.hpp"

namespace lru_test {

template <typename Factory, typename K, typename V>
using CacheOf = typename Factory::template cache<K, V>;

// Membership check built on visit(). It refreshes recency like any read, so tests only call it
// after the last operation whose ordering effect they are measuring.
template <typename Cache>
bool contains(Cache& cache, const typename Cache::key_type& key) {
    return cache.visit(key, [](const auto&) {});
}

// A value type that records every copy. Moves are free. A cache that takes values by value and
// moves them into place should never need to copy one, and a stray copy on a hot path is exactly
// the kind of regression that compiles silently.
struct CopyCounter {
    static inline int copies = 0;

    int payload = 0;

    CopyCounter() = default;
    explicit CopyCounter(int p) : payload(p) {}
    CopyCounter(const CopyCounter& other) : payload(other.payload) { ++copies; }
    CopyCounter& operator=(const CopyCounter& other) {
        payload = other.payload;
        ++copies;
        return *this;
    }
    CopyCounter(CopyCounter&&) noexcept = default;
    CopyCounter& operator=(CopyCounter&&) noexcept = default;
    ~CopyCounter() = default;
};

// ---------------------------------------------------------------------------------------------
// Contract suite: guarantees every design must provide, including a sharded one.
//
// A sharded cache splits its capacity across shards, so a tiny capacity can leave some shards
// with no room at all, and eviction is decided per shard rather than globally. These tests
// therefore use a capacity large enough that any reasonable shard count still gives every shard
// space, and they bound size from above instead of asserting exactly which entries were evicted.
// ---------------------------------------------------------------------------------------------

inline constexpr std::size_t kRoomyCapacity = 64;

template <typename Factory>
class LruContractTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(LruContractTest);

TYPED_TEST_P(LruContractTest, SatisfiesCacheConcept) {
    using IntCache = CacheOf<TypeParam, int, int>;
    using StringCache = CacheOf<TypeParam, std::string, std::string>;
    using MoveOnlyCache = CacheOf<TypeParam, int, std::unique_ptr<int>>;

    static_assert(lru::LruCacheLike<IntCache>);
    static_assert(lru::LruCacheLike<StringCache>);
    static_assert(lru::LruCacheLike<MoveOnlyCache>);

    // get() copies the value out, so it must exist for copyable values and be absent, rather than
    // failing deep inside a template, for move-only ones.
    static_assert(requires(IntCache& c, int k) { c.get(k); });
    static_assert(!requires(MoveOnlyCache& c, int k) { c.get(k); });
}

TYPED_TEST_P(LruContractTest, PutThenGetReturnsStoredValue) {
    auto cache = TypeParam::template make<int, std::string>(kRoomyCapacity);

    cache.put(1, "one");
    cache.put(2, "two");

    EXPECT_EQ(cache.get(1), std::optional<std::string>("one"));
    EXPECT_EQ(cache.get(2), std::optional<std::string>("two"));
    EXPECT_EQ(cache.size(), 2u);
}

TYPED_TEST_P(LruContractTest, MissReturnsEmptyAndDoesNotInsert) {
    auto cache = TypeParam::template make<int, int>(kRoomyCapacity);
    cache.put(1, 10);

    EXPECT_EQ(cache.get(2), std::nullopt);

    bool invoked = false;
    EXPECT_FALSE(cache.visit(3, [&invoked](const int&) { invoked = true; }));
    EXPECT_FALSE(invoked);

    EXPECT_EQ(cache.size(), 1u);
}

TYPED_TEST_P(LruContractTest, UpdateReplacesValueWithoutGrowing) {
    auto cache = TypeParam::template make<int, int>(kRoomyCapacity);
    cache.put(1, 10);
    cache.put(2, 20);

    cache.put(1, 11);

    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.get(1), std::optional<int>(11));
    EXPECT_EQ(cache.get(2), std::optional<int>(20));
}

TYPED_TEST_P(LruContractTest, SizeNeverExceedsCapacity) {
    auto cache = TypeParam::template make<int, int>(kRoomyCapacity);

    for (int key = 0; key < static_cast<int>(kRoomyCapacity) * 10; ++key) {
        cache.put(key, key);
        ASSERT_LE(cache.size(), kRoomyCapacity) << "after inserting key " << key;
    }
}

TYPED_TEST_P(LruContractTest, CapacityReportsConstructorArgument) {
    for (std::size_t capacity : {std::size_t{0}, std::size_t{1}, std::size_t{7}, kRoomyCapacity}) {
        auto cache = TypeParam::template make<int, int>(capacity);
        EXPECT_EQ(cache.capacity(), capacity);
    }
}

TYPED_TEST_P(LruContractTest, CapacityZeroStoresNothing) {
    auto cache = TypeParam::template make<int, int>(0);

    cache.put(1, 10);
    cache.put(1, 11);
    cache.put(2, 20);

    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.get(1), std::nullopt);
    EXPECT_EQ(cache.get(2), std::nullopt);
}

TYPED_TEST_P(LruContractTest, MoveOnlyValueWorks) {
    auto cache = TypeParam::template make<int, std::unique_ptr<int>>(kRoomyCapacity);

    cache.put(1, std::make_unique<int>(10));

    int seen = 0;
    ASSERT_TRUE(cache.visit(1, [&seen](const std::unique_ptr<int>& value) { seen = *value; }));
    EXPECT_EQ(seen, 10);

    cache.put(1, std::make_unique<int>(11));
    ASSERT_TRUE(cache.visit(1, [&seen](const std::unique_ptr<int>& value) { seen = *value; }));
    EXPECT_EQ(seen, 11);
    EXPECT_EQ(cache.size(), 1u);

    // Overfill so evictions destroy move-only values too.
    for (int key = 2; key < static_cast<int>(kRoomyCapacity) * 4; ++key) {
        cache.put(key, std::make_unique<int>(key));
    }
    EXPECT_LE(cache.size(), kRoomyCapacity);
}

TYPED_TEST_P(LruContractTest, NoValueCopiesOnPutUpdateVisitOrEviction) {
    auto cache = TypeParam::template make<int, CopyCounter>(kRoomyCapacity);
    CopyCounter::copies = 0;

    cache.put(1, CopyCounter(1));
    cache.put(1, CopyCounter(2));

    CopyCounter incoming(3);
    cache.put(2, std::move(incoming));

    int seen = 0;
    ASSERT_TRUE(cache.visit(1, [&seen](const CopyCounter& value) { seen = value.payload; }));
    EXPECT_EQ(seen, 2);

    for (int key = 3; key < static_cast<int>(kRoomyCapacity) * 4; ++key) {
        cache.put(key, CopyCounter(key));
    }

    EXPECT_EQ(CopyCounter::copies, 0);
}

REGISTER_TYPED_TEST_SUITE_P(LruContractTest, SatisfiesCacheConcept, PutThenGetReturnsStoredValue,
                            MissReturnsEmptyAndDoesNotInsert, UpdateReplacesValueWithoutGrowing,
                            SizeNeverExceedsCapacity, CapacityReportsConstructorArgument,
                            CapacityZeroStoresNothing, MoveOnlyValueWorks,
                            NoValueCopiesOnPutUpdateVisitOrEviction);

// ---------------------------------------------------------------------------------------------
// Strict order suite: exact global least-recently-used eviction.
//
// Only a design with one recency list across all keys can promise this. A sharded cache runs it
// with a single shard, and its multi-shard ordering is covered by its own tests.
// ---------------------------------------------------------------------------------------------

template <typename Factory>
class LruStrictOrderTest : public ::testing::Test {};

TYPED_TEST_SUITE_P(LruStrictOrderTest);

TYPED_TEST_P(LruStrictOrderTest, EvictsLeastRecentlyUsedFirst) {
    auto cache = TypeParam::template make<int, int>(3);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    // Recency from most to least recent is now 1, 2, 3.
    ASSERT_TRUE(cache.get(2).has_value());
    ASSERT_TRUE(cache.get(1).has_value());

    cache.put(4, 40);  // evicts 3
    cache.put(5, 50);  // evicts 2

    EXPECT_EQ(cache.size(), 3u);
    EXPECT_FALSE(contains(cache, 3));
    EXPECT_FALSE(contains(cache, 2));
    EXPECT_TRUE(contains(cache, 1));
    EXPECT_TRUE(contains(cache, 4));
    EXPECT_TRUE(contains(cache, 5));
}

TYPED_TEST_P(LruStrictOrderTest, GetRefreshesRecency) {
    auto cache = TypeParam::template make<int, int>(3);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    ASSERT_EQ(cache.get(1), std::optional<int>(10));
    cache.put(4, 40);

    EXPECT_FALSE(contains(cache, 2));
    EXPECT_TRUE(contains(cache, 1));
    EXPECT_TRUE(contains(cache, 3));
    EXPECT_TRUE(contains(cache, 4));
}

TYPED_TEST_P(LruStrictOrderTest, VisitRefreshesRecency) {
    auto cache = TypeParam::template make<int, int>(3);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    ASSERT_TRUE(cache.visit(1, [](const int&) {}));
    cache.put(4, 40);

    EXPECT_FALSE(contains(cache, 2));
    EXPECT_TRUE(contains(cache, 1));
    EXPECT_TRUE(contains(cache, 3));
    EXPECT_TRUE(contains(cache, 4));
}

TYPED_TEST_P(LruStrictOrderTest, MissDoesNotMutateOrder) {
    auto cache = TypeParam::template make<int, int>(3);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    ASSERT_EQ(cache.get(99), std::nullopt);
    ASSERT_FALSE(cache.visit(98, [](const int&) {}));

    // Two further inserts must evict exactly the two oldest keys, in insertion order. Any
    // reordering caused by the misses would evict a different pair.
    cache.put(4, 40);
    cache.put(5, 50);

    EXPECT_EQ(cache.size(), 3u);
    EXPECT_FALSE(contains(cache, 1));
    EXPECT_FALSE(contains(cache, 2));
    EXPECT_TRUE(contains(cache, 3));
    EXPECT_TRUE(contains(cache, 4));
    EXPECT_TRUE(contains(cache, 5));
}

TYPED_TEST_P(LruStrictOrderTest, UpdateRefreshesRecencyWithoutGrowing) {
    auto cache = TypeParam::template make<int, int>(3);
    cache.put(1, 10);
    cache.put(2, 20);
    cache.put(3, 30);

    cache.put(1, 100);
    EXPECT_EQ(cache.size(), 3u);

    cache.put(4, 40);

    EXPECT_EQ(cache.size(), 3u);
    EXPECT_FALSE(contains(cache, 2));
    EXPECT_EQ(cache.get(1), std::optional<int>(100));
    EXPECT_TRUE(contains(cache, 3));
    EXPECT_TRUE(contains(cache, 4));
}

// Covers the capacity-1 edge case only, not ordering: with a single slot there is no recency to get wrong.
TYPED_TEST_P(LruStrictOrderTest, CapacityOneKeepsOnlyMostRecent) {
    auto cache = TypeParam::template make<int, int>(1);

    cache.put(1, 10);
    cache.put(2, 20);

    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.get(1), std::nullopt);

    cache.put(2, 21);

    EXPECT_EQ(cache.size(), 1u);
    EXPECT_EQ(cache.get(2), std::optional<int>(21));
}

REGISTER_TYPED_TEST_SUITE_P(LruStrictOrderTest, EvictsLeastRecentlyUsedFirst, GetRefreshesRecency,
                            VisitRefreshesRecency, MissDoesNotMutateOrder,
                            UpdateRefreshesRecencyWithoutGrowing, CapacityOneKeepsOnlyMostRecent);

}  // namespace lru_test
