#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "lru/IntrusiveLru.hpp"

namespace lru {

namespace detail {

// Shards and buckets must be chosen from independent bits of the hash. IntrusiveLru picks buckets from
// the top bits of hash * phi; choosing shards from those same bits would confine each shard's keys to a
// fraction of its buckets. Remixing with the splitmix64 finaliser first also stops structured keys (all
// multiples of the shard count, say) from landing in one shard.
constexpr std::uint64_t shard_mix(std::uint64_t x) noexcept {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Never more shards than entries, so every shard can hold at least one: a zero-capacity shard would
// silently refuse to cache every key that hashes to it. Capacity 0 still gets one (empty) shard.
constexpr std::size_t effective_shard_count(std::size_t capacity, std::size_t requested) noexcept {
    return std::clamp<std::size_t>(requested, 1, std::max<std::size_t>(capacity, 1));
}

// Floor plus remainder, so the shard capacities sum exactly to the requested capacity.
constexpr std::size_t shard_capacity(std::size_t capacity, std::size_t shards, std::size_t index) noexcept {
    return capacity / shards + (index < capacity % shards ? 1 : 0);
}

}  // namespace detail

// v3: independent IntrusiveLru shards, each with its own mutex, so threads touching different shards
// never wait for each other.
//
// The price is that recency becomes per shard. Eviction removes the least recently used entry of the
// key's shard, not of the whole cache, and shards fill unevenly, so a sharded cache can evict an entry a
// single global list would have kept. The shared contract suite holds for any shard count; exact global
// LRU order holds only with one shard.
//
// The shard count is a constructor argument rather than a template parameter so one type serves every
// count and the count can be chosen at run time. The cost is a runtime modulo in shard selection, which
// a compile-time count could turn into a multiplication.
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
    requires std::move_constructible<K> && std::move_constructible<V>
class ShardedLru {
public:
    using key_type = K;
    using mapped_type = V;

    // shard_count is clamped to the capacity (see detail::effective_shard_count); shard_count() reports
    // the count actually used. A shard count of zero is a caller error.
    ShardedLru(std::size_t capacity, std::size_t shard_count)
        : capacity_(capacity), shard_count_(checked_shard_count(capacity, shard_count)) {
        shards_ = allocator_.allocate(shard_count_);
        std::size_t built = 0;
        try {
            for (; built < shard_count_; ++built) {
                std::construct_at(shards_ + built, detail::shard_capacity(capacity_, shard_count_, built));
            }
        } catch (...) {
            destroy_shards(built);
            throw;
        }
    }

    ShardedLru(const ShardedLru&) = delete;
    ShardedLru& operator=(const ShardedLru&) = delete;
    ShardedLru(ShardedLru&&) = delete;
    ShardedLru& operator=(ShardedLru&&) = delete;

    ~ShardedLru() { destroy_shards(shard_count_); }

    void put(K key, V value) {
        Shard& target = shard(key);
        target.lru.put(std::move(key), std::move(value));
    }

    template <typename F>
        requires std::invocable<F, const V&>
    bool visit(const K& key, F&& fn) {
        return shard(key).lru.visit(key, std::forward<F>(fn));
    }

    std::optional<V> get(const K& key)
        requires std::copy_constructible<V>
    {
        return shard(key).lru.get(key);
    }

    // Sums the shards one lock at a time. Exact when no other thread is writing. Under concurrent writes
    // it is not a consistent snapshot, since each shard is read at a different moment, but it never
    // exceeds capacity(): every term is bounded by its own shard's capacity. Locking every shard to get a
    // true snapshot would stall all writers for the duration.
    std::size_t size() const {
        std::size_t total = 0;
        for (std::size_t i = 0; i < shard_count_; ++i) {
            total += shards_[i].lru.size();
        }
        return total;
    }

    std::size_t capacity() const noexcept { return capacity_; }

    std::size_t shard_count() const noexcept { return shard_count_; }

    // Exposed so callers and tests can reason about distribution, for example to find keys that share a
    // shard or to measure how unevenly shards fill.
    std::size_t shard_for(const K& key) const {
        return static_cast<std::size_t>(detail::shard_mix(static_cast<std::uint64_t>(hash_(key))) %
                                        shard_count_);
    }

private:
    // Assumed cache line size. Aligning each shard to it keeps one shard's mutex and counters off the
    // cache lines of its neighbours, so threads working on different shards do not invalidate each
    // other's caches. std::hardware_destructive_interference_size is not used because its value can
    // differ between compilers, which would change this type's layout.
    static constexpr std::size_t kCacheLineSize = 64;

    struct alignas(kCacheLineSize) Shard {
        explicit Shard(std::size_t capacity) : lru(capacity) {}

        IntrusiveLru<K, V, Hash, KeyEqual> lru;
    };

    static std::size_t checked_shard_count(std::size_t capacity, std::size_t requested) {
        if (requested == 0) {
            throw std::invalid_argument("ShardedLru needs at least one shard");
        }
        return detail::effective_shard_count(capacity, requested);
    }

    Shard& shard(const K& key) { return shards_[shard_for(key)]; }

    void destroy_shards(std::size_t built) noexcept {
        while (built > 0) {
            std::destroy_at(shards_ + --built);
        }
        allocator_.deallocate(shards_, shard_count_);
    }

    const std::size_t capacity_;
    const std::size_t shard_count_;
    [[no_unique_address]] std::allocator<Shard> allocator_;
    Shard* shards_ = nullptr;
    [[no_unique_address]] Hash hash_;
};

}  // namespace lru
