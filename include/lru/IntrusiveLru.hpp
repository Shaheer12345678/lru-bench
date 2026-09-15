#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lru {

// v2: the same contract and the same single mutex as LruCache, but with every allocation moved to
// construction.
//
// v1 pays for two heap allocations on each insert (a list node and a hash map node) and chases
// pointers scattered across the heap on every operation. Here all entries live in one preallocated
// array of nodes, addressed by 32-bit indices, and both structures that tie them together are
// intrusive: each node carries its own recency links and its own hash chain link. Steady-state
// operation on a full cache therefore never touches the allocator, and the nodes it does touch sit
// next to each other in memory.
//
// The cost is paid up front. The node array is sized for the full capacity at construction, so a
// cache that is sized generously but rarely filled reserves memory it never uses (see the
// constructor for what is committed immediately and what only on first use).
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
    requires std::move_constructible<K> && std::move_constructible<V>
class IntrusiveLru {
public:
    using key_type = K;
    using mapped_type = V;

    // Indices are 32 bits to keep nodes small; kNil and the sentinel index must stay representable.
    static constexpr std::size_t kMaxCapacity = std::numeric_limits<std::uint32_t>::max() - 1;

    // Allocates everything the cache will ever need. The node array is obtained without being
    // written to, so its pages are only committed by the OS as slots are first used (the sentinel,
    // at the end of the array, is the one exception). The bucket array has to be filled with kNil,
    // so it is committed in full here.
    explicit IntrusiveLru(std::size_t capacity)
        : capacity_(checked_capacity(capacity)),
          bucket_count_(std::bit_ceil(std::max<std::size_t>(capacity_, 2))),
          bucket_shift_(static_cast<unsigned>(64 - std::countr_zero(bucket_count_))),
          sentinel_(static_cast<std::uint32_t>(capacity_)),
          nodes_(std::make_unique_for_overwrite<Node[]>(capacity_ + 1)),
          buckets_(std::make_unique_for_overwrite<std::uint32_t[]>(bucket_count_)) {
        std::fill_n(buckets_.get(), bucket_count_, kNil);
        nodes_[sentinel_].prev = sentinel_;
        nodes_[sentinel_].next = sentinel_;
    }

    IntrusiveLru(const IntrusiveLru&) = delete;
    IntrusiveLru& operator=(const IntrusiveLru&) = delete;
    IntrusiveLru(IntrusiveLru&&) = delete;
    IntrusiveLru& operator=(IntrusiveLru&&) = delete;

    // Only slots on the recency list hold constructed entries; free and never-used slots are raw.
    ~IntrusiveLru() {
        for (std::uint32_t i = nodes_[sentinel_].next; i != sentinel_; i = nodes_[i].next) {
            std::destroy_at(entry(i));
        }
    }

    void put(K key, V value) {
        std::lock_guard lock(mutex_);

        const std::size_t bucket = bucket_of(key);
        if (const std::uint32_t found = find(bucket, key); found != kNil) {
            entry(found)->value = std::move(value);
            move_to_front(found);
            return;
        }

        if (capacity_ == 0) {
            return;
        }

        const std::uint32_t slot = acquire_slot();
        try {
            std::construct_at(storage(slot), std::move(key), std::move(value));
        } catch (...) {
            release_slot(slot);
            throw;
        }
        link_front(slot);
        nodes_[slot].chain_next = buckets_[bucket];
        buckets_[bucket] = slot;
        ++size_;
    }

    // Same contract as LruCache::visit: fn runs under the lock and must not re-enter the cache.
    template <typename F>
        requires std::invocable<F, const V&>
    bool visit(const K& key, F&& fn) {
        std::lock_guard lock(mutex_);

        const std::uint32_t found = find(bucket_of(key), key);
        if (found == kNil) {
            return false;
        }
        move_to_front(found);
        std::invoke(std::forward<F>(fn), std::as_const(entry(found)->value));
        return true;
    }

    std::optional<V> get(const K& key)
        requires std::copy_constructible<V>
    {
        std::optional<V> result;
        visit(key, [&result](const V& value) { result.emplace(value); });
        return result;
    }

    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return size_;
    }

    std::size_t capacity() const noexcept { return capacity_; }

private:
    static constexpr std::uint32_t kNil = std::numeric_limits<std::uint32_t>::max();

    struct Entry {
        // Parenthesised aggregate initialisation is not available on every supported compiler, and
        // std::construct_at needs a constructor to call.
        Entry(K&& k, V&& v) : key(std::move(k)), value(std::move(v)) {}

        K key;
        V value;
    };

    // Trivially default constructible on purpose, so the array can be allocated without writing to
    // it. The entry storage holds a live Entry only while the node is on the recency list.
    struct Node {
        std::uint32_t prev;
        std::uint32_t next;
        std::uint32_t chain_next;
        alignas(Entry) std::byte storage[sizeof(Entry)];
    };

    static std::size_t checked_capacity(std::size_t capacity) {
        if (capacity > kMaxCapacity) {
            throw std::length_error("IntrusiveLru capacity exceeds 32-bit node indices");
        }
        return capacity;
    }

    Entry* storage(std::uint32_t i) noexcept {
        return reinterpret_cast<Entry*>(nodes_[i].storage);
    }

    // launder: the Entry was created in the byte array by construct_at, and the pointer obtained
    // from the array is not the one construct_at returned.
    Entry* entry(std::uint32_t i) noexcept { return std::launder(storage(i)); }

    // Fibonacci hashing spreads the high bits of the multiplied hash across the power-of-two table.
    // std::hash for integers is the identity on common standard libraries, so masking the low bits
    // directly would put structured keys (all multiples of 64, say) into a few buckets.
    std::size_t bucket_of(const K& key) const {
        const std::uint64_t h = static_cast<std::uint64_t>(hash_(key));
        return static_cast<std::size_t>((h * 0x9E3779B97F4A7C15ULL) >> bucket_shift_);
    }

    std::uint32_t find(std::size_t bucket, const K& key) {
        for (std::uint32_t i = buckets_[bucket]; i != kNil; i = nodes_[i].chain_next) {
            if (key_equal_(entry(i)->key, key)) {
                return i;
            }
        }
        return kNil;
    }

    // Prefers a released slot, then a never-used one, and evicts only when neither exists. Taking
    // never-used slots in order keeps a partly filled cache confined to the front of the array.
    std::uint32_t acquire_slot() {
        if (free_head_ != kNil) {
            const std::uint32_t slot = free_head_;
            free_head_ = nodes_[slot].next;
            return slot;
        }
        if (next_unused_ < capacity_) {
            return static_cast<std::uint32_t>(next_unused_++);
        }

        const std::uint32_t victim = nodes_[sentinel_].prev;
        unlink_from_bucket(victim);
        unlink(victim);
        std::destroy_at(entry(victim));
        --size_;
        return victim;
    }

    void release_slot(std::uint32_t slot) noexcept {
        nodes_[slot].next = free_head_;
        free_head_ = slot;
    }

    void unlink_from_bucket(std::uint32_t slot) {
        std::uint32_t* link = &buckets_[bucket_of(entry(slot)->key)];
        while (*link != slot) {
            link = &nodes_[*link].chain_next;
        }
        *link = nodes_[slot].chain_next;
    }

    void unlink(std::uint32_t i) noexcept {
        nodes_[nodes_[i].prev].next = nodes_[i].next;
        nodes_[nodes_[i].next].prev = nodes_[i].prev;
    }

    void link_front(std::uint32_t i) noexcept {
        const std::uint32_t first = nodes_[sentinel_].next;
        nodes_[i].prev = sentinel_;
        nodes_[i].next = first;
        nodes_[first].prev = i;
        nodes_[sentinel_].next = i;
    }

    void move_to_front(std::uint32_t i) noexcept {
        if (nodes_[sentinel_].next != i) {
            unlink(i);
            link_front(i);
        }
    }

    const std::size_t capacity_;
    const std::size_t bucket_count_;
    const unsigned bucket_shift_;
    const std::uint32_t sentinel_;

    mutable std::mutex mutex_;
    std::unique_ptr<Node[]> nodes_;
    std::unique_ptr<std::uint32_t[]> buckets_;
    std::size_t size_ = 0;
    std::size_t next_unused_ = 0;
    std::uint32_t free_head_ = kNil;

    [[no_unique_address]] Hash hash_;
    [[no_unique_address]] KeyEqual key_equal_;
};

}  // namespace lru
