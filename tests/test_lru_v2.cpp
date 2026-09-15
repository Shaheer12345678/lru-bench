#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>

#include "factories.hpp"
#include "test_correctness.hpp"

namespace lru_test {
namespace {

using V2Types = ::testing::Types<V2Factory>;

// A shift of 64 in the bucket calculation is undefined behaviour, so an ordinary build may hash into
// the wrong bucket, crash, or appear to work. Checking the arithmetic directly turns a regression
// into a plain assertion failure in every build type, not only under UBSan.
TEST(IntrusiveLruBuckets, SmallestCapacitiesKeepTheShiftDefined) {
    const std::initializer_list<std::uint64_t> hashes = {
        0, 1, 2, 0x9E3779B97F4A7C15ULL, std::uint64_t{1} << 63, std::numeric_limits<std::uint64_t>::max()};

    for (std::size_t capacity = 0; capacity <= 5; ++capacity) {
        const std::size_t buckets = lru::detail::intrusive_bucket_count(capacity);
        ASSERT_GE(buckets, 2u) << "capacity " << capacity;
        ASSERT_GE(buckets, capacity) << "capacity " << capacity;

        const unsigned shift = lru::detail::fibonacci_shift(buckets);
        // Asserted before any shift is evaluated, so a regression is reported rather than executed.
        ASSERT_LT(shift, 64u) << "capacity " << capacity;

        for (std::uint64_t hash : hashes) {
            EXPECT_LT(lru::detail::fibonacci_bucket(hash, shift), buckets)
                << "capacity " << capacity << ", hash " << hash;
        }
    }
}

// The same edge through the public API: keys spread across the whole hash range at the smallest
// capacities, where the table has the fewest buckets.
TEST(IntrusiveLruBuckets, SmallestCapacitiesRoundTripKeysAcrossTheHashRange) {
    const std::initializer_list<std::uint64_t> keys = {
        0, 1, 2, 0x9E3779B97F4A7C15ULL, std::uint64_t{1} << 63, std::numeric_limits<std::uint64_t>::max()};

    for (std::size_t capacity = 1; capacity <= 4; ++capacity) {
        lru::IntrusiveLru<std::uint64_t, std::uint64_t> cache(capacity);
        for (std::uint64_t key : keys) {
            cache.put(key, ~key);
            ASSERT_EQ(cache.get(key), std::optional<std::uint64_t>(~key))
                << "capacity " << capacity << ", key " << key;
            ASSERT_LE(cache.size(), capacity);
        }
    }
}

}  // namespace

INSTANTIATE_TYPED_TEST_SUITE_P(V2, LruContractTest, V2Types);
INSTANTIATE_TYPED_TEST_SUITE_P(V2, LruStrictOrderTest, V2Types);

}  // namespace lru_test
