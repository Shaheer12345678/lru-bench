#pragma once

// Adapters that give every cache implementation the same construction interface, so the shared
// suites in this directory can create caches without knowing which design they are exercising.
// See test_correctness.hpp for the shape a factory must have.

#include <cstddef>

#include "lru/IntrusiveLru.hpp"
#include "lru/LruCache.hpp"
#include "lru/ShardedLru.hpp"

namespace lru_test {

struct V1Factory {
    template <typename K, typename V>
    using cache = lru::LruCache<K, V>;

    template <typename K, typename V>
    static cache<K, V> make(std::size_t capacity) {
        return cache<K, V>(capacity);
    }
};

struct V2Factory {
    template <typename K, typename V>
    using cache = lru::IntrusiveLru<K, V>;

    template <typename K, typename V>
    static cache<K, V> make(std::size_t capacity) {
        return cache<K, V>(capacity);
    }
};

// The shard count is part of the factory so one suite can be instantiated at several counts.
template <std::size_t Shards>
struct V3Factory {
    template <typename K, typename V>
    using cache = lru::ShardedLru<K, V>;

    template <typename K, typename V>
    static cache<K, V> make(std::size_t capacity) {
        return cache<K, V>(capacity, Shards);
    }
};

}  // namespace lru_test
