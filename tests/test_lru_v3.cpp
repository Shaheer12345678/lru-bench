#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <vector>

#include "factories.hpp"
#include "test_correctness.hpp"

namespace lru_test {
namespace {

// The contract suite runs at several shard counts. 32 is the ceiling because the suite uses a capacity of
// 64 and some of its tests put two keys: with at least two slots per shard those tests cannot depend on
// whether the two keys happen to share a shard.
using V3ContractTypes =
    ::testing::Types<V3Factory<1>, V3Factory<2>, V3Factory<7>, V3Factory<16>, V3Factory<32>>;

// Exact global LRU order is only promised with a single shard; per-shard order is tested below.
using V3StrictOrderTypes = ::testing::Types<V3Factory<1>>;

using Cache = lru::ShardedLru<int, int>;

// The first `count` non-negative keys that the cache routes to `shard`.
std::vector<int> keys_in_shard(const Cache& cache, std::size_t shard, std::size_t count) {
    std::vector<int> keys;
    for (int key = 0; keys.size() < count; ++key) {
        if (cache.shard_for(key) == shard) {
            keys.push_back(key);
        }
    }
    return keys;
}

TEST(ShardedLruShards, ShardCountIsClampedToCapacity) {
    EXPECT_EQ(Cache(64, 8).shard_count(), 8u);
    EXPECT_EQ(Cache(8, 8).shard_count(), 8u);
    EXPECT_EQ(Cache(3, 8).shard_count(), 3u);
    EXPECT_EQ(Cache(1, 8).shard_count(), 1u);
    EXPECT_EQ(Cache(0, 8).shard_count(), 1u);
    EXPECT_THROW({ Cache cache(64, 0); }, std::invalid_argument);
}

// Filling far past capacity reaches exactly the requested capacity only if the per-shard capacities sum
// to it; a split that dropped the remainder would stop short, one that rounded up would overshoot.
TEST(ShardedLruShards, ShardCapacitiesSumToTheRequestedCapacity) {
    struct Case {
        std::size_t capacity;
        std::size_t shards;
    };
    for (const Case c : {Case{10, 3}, Case{64, 7}, Case{3, 8}, Case{1, 4}, Case{0, 4}, Case{1000, 16}}) {
        Cache cache(c.capacity, c.shards);
        for (int key = 0; key < static_cast<int>(c.capacity) * 100 + 1000; ++key) {
            cache.put(key, key);
        }
        EXPECT_EQ(cache.size(), c.capacity) << "capacity " << c.capacity << ", shards " << c.shards;
        EXPECT_EQ(cache.capacity(), c.capacity);
    }
}

TEST(ShardedLruShards, ShardSelectionIsStableInRangeAndReachesEveryShard) {
    const Cache cache(64, 7);
    std::vector<std::size_t> keys_per_shard(cache.shard_count(), 0);
    for (int key = 0; key < 10000; ++key) {
        const std::size_t shard = cache.shard_for(key);
        ASSERT_LT(shard, cache.shard_count());
        ASSERT_EQ(shard, cache.shard_for(key));
        ++keys_per_shard[shard];
    }
    for (std::size_t shard = 0; shard < keys_per_shard.size(); ++shard) {
        EXPECT_GT(keys_per_shard[shard], 0u) << "shard " << shard;
    }
}

// Per-shard ordering: 4 shards of 3 slots each, all keys under test chosen from shard 1.

TEST(ShardedLruPerShardOrder, EvictsLeastRecentlyUsedFirstWithinAShard) {
    Cache cache(12, 4);
    const std::vector<int> k = keys_in_shard(cache, 1, 5);
    cache.put(k[0], 0);
    cache.put(k[1], 1);
    cache.put(k[2], 2);

    // Recency within the shard, most to least recent, is now k0, k1, k2.
    ASSERT_TRUE(cache.get(k[1]).has_value());
    ASSERT_TRUE(cache.get(k[0]).has_value());

    cache.put(k[3], 3);  // evicts k2
    cache.put(k[4], 4);  // evicts k1

    EXPECT_FALSE(contains(cache, k[2]));
    EXPECT_FALSE(contains(cache, k[1]));
    EXPECT_TRUE(contains(cache, k[0]));
    EXPECT_TRUE(contains(cache, k[3]));
    EXPECT_TRUE(contains(cache, k[4]));
}

TEST(ShardedLruPerShardOrder, GetRefreshesRecencyWithinAShard) {
    Cache cache(12, 4);
    const std::vector<int> k = keys_in_shard(cache, 1, 4);
    cache.put(k[0], 0);
    cache.put(k[1], 1);
    cache.put(k[2], 2);

    ASSERT_EQ(cache.get(k[0]), std::optional<int>(0));
    cache.put(k[3], 3);

    EXPECT_FALSE(contains(cache, k[1]));
    EXPECT_TRUE(contains(cache, k[0]));
    EXPECT_TRUE(contains(cache, k[2]));
    EXPECT_TRUE(contains(cache, k[3]));
}

TEST(ShardedLruPerShardOrder, VisitRefreshesRecencyWithinAShard) {
    Cache cache(12, 4);
    const std::vector<int> k = keys_in_shard(cache, 1, 4);
    cache.put(k[0], 0);
    cache.put(k[1], 1);
    cache.put(k[2], 2);

    ASSERT_TRUE(cache.visit(k[0], [](const int&) {}));
    cache.put(k[3], 3);

    EXPECT_FALSE(contains(cache, k[1]));
    EXPECT_TRUE(contains(cache, k[0]));
    EXPECT_TRUE(contains(cache, k[2]));
    EXPECT_TRUE(contains(cache, k[3]));
}

TEST(ShardedLruPerShardOrder, UpdateRefreshesRecencyWithinAShard) {
    Cache cache(12, 4);
    const std::vector<int> k = keys_in_shard(cache, 1, 4);
    cache.put(k[0], 0);
    cache.put(k[1], 1);
    cache.put(k[2], 2);

    cache.put(k[0], 100);
    cache.put(k[3], 3);

    EXPECT_FALSE(contains(cache, k[1]));
    EXPECT_EQ(cache.get(k[0]), std::optional<int>(100));
    EXPECT_TRUE(contains(cache, k[2]));
    EXPECT_TRUE(contains(cache, k[3]));
}

TEST(ShardedLruPerShardOrder, MissDoesNotMutateOrderWithinAShard) {
    Cache cache(12, 4);
    const std::vector<int> k = keys_in_shard(cache, 1, 7);
    cache.put(k[0], 0);
    cache.put(k[1], 1);
    cache.put(k[2], 2);

    // Misses on keys that route to the same shard.
    ASSERT_EQ(cache.get(k[5]), std::nullopt);
    ASSERT_FALSE(cache.visit(k[6], [](const int&) {}));

    cache.put(k[3], 3);  // must evict k0
    cache.put(k[4], 4);  // must evict k1

    EXPECT_FALSE(contains(cache, k[0]));
    EXPECT_FALSE(contains(cache, k[1]));
    EXPECT_TRUE(contains(cache, k[2]));
    EXPECT_TRUE(contains(cache, k[3]));
    EXPECT_TRUE(contains(cache, k[4]));
}

// The defining property of sharding: churn in one shard never evicts from another.
TEST(ShardedLruPerShardOrder, EvictionInOneShardLeavesOtherShardsUntouched) {
    Cache cache(12, 4);
    std::vector<int> bystanders;
    for (std::size_t shard : {std::size_t{0}, std::size_t{2}, std::size_t{3}}) {
        for (int key : keys_in_shard(cache, shard, 3)) {
            cache.put(key, key);
            bystanders.push_back(key);
        }
    }

    for (int key : keys_in_shard(cache, 1, 50)) {
        cache.put(key, key);
    }

    EXPECT_EQ(cache.size(), 12u);
    for (int key : bystanders) {
        EXPECT_TRUE(contains(cache, key)) << "key " << key << " evicted from a shard that saw no inserts";
    }
}

// One shard must be observably the same cache as IntrusiveLru, not merely similar: the same operation
// sequence gives the same result at every step.
TEST(ShardedLruSingleShard, MatchesIntrusiveLruOperationForOperation) {
    lru::IntrusiveLru<int, int> reference(16);
    Cache sharded(16, 1);
    ASSERT_EQ(sharded.shard_count(), 1u);

    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    auto next = [&state] {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };

    for (int step = 0; step < 20000; ++step) {
        const int key = static_cast<int>(next() % 64);
        switch (next() % 3) {
            case 0:
                reference.put(key, step);
                sharded.put(key, step);
                break;
            case 1:
                ASSERT_EQ(sharded.get(key), reference.get(key)) << "step " << step;
                break;
            default: {
                int seen_reference = -1;
                int seen_sharded = -1;
                const bool hit_reference = reference.visit(key, [&](const int& v) { seen_reference = v; });
                const bool hit_sharded = sharded.visit(key, [&](const int& v) { seen_sharded = v; });
                ASSERT_EQ(hit_sharded, hit_reference) << "step " << step;
                ASSERT_EQ(seen_sharded, seen_reference) << "step " << step;
                break;
            }
        }
        ASSERT_EQ(sharded.size(), reference.size()) << "step " << step;
    }
}

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(V3, LruContractTest, V3ContractTypes);
INSTANTIATE_TYPED_TEST_SUITE_P(V3, LruStrictOrderTest, V3StrictOrderTypes);

}  // namespace lru_test
