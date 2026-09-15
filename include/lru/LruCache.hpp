#pragma once

#include <concepts>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace lru {

// v1: the textbook LRU cache, a hash map pointing into a doubly linked list, behind one mutex.
//
// It exists as the baseline the other designs are measured against, so it deliberately makes the
// obvious choices and pays their costs: every insert allocates a list node and a map node, nodes
// are scattered across the heap, the key is stored twice (once in the map, once in the list entry
// so eviction can find the map slot), and a single lock serialises every operation, reads included,
// because a read has to move the entry to the front.
//
// Keys must be copyable because of that double storage. Values only need to be movable.
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
    requires std::copy_constructible<K> && std::move_constructible<V>
class LruCache {
public:
    using key_type = K;
    using mapped_type = V;

    // Capacity 0 is accepted rather than rejected: a cache that never stores anything is a valid,
    // if useless, configuration, and a sharded cache splitting a small total can hand a shard zero.
    explicit LruCache(std::size_t capacity) : capacity_(capacity) {
        index_.reserve(capacity);
    }

    // The mutex pins the object in place; there is no meaningful way to move a cache that other
    // threads may be holding a reference to.
    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;
    LruCache(LruCache&&) = delete;
    LruCache& operator=(LruCache&&) = delete;
    ~LruCache() = default;

    // Inserting an existing key replaces its value and counts as a use, matching what a caller
    // refreshing stale data expects. When full, the least recently used entry is evicted first.
    void put(K key, V value) {
        std::lock_guard lock(mutex_);

        if (auto found = index_.find(key); found != index_.end()) {
            found->second->value = std::move(value);
            entries_.splice(entries_.begin(), entries_, found->second);
            return;
        }

        if (capacity_ == 0) {
            return;
        }

        if (entries_.size() == capacity_) {
            index_.erase(entries_.back().key);
            entries_.pop_back();
        }

        entries_.push_front(Entry{std::move(key), std::move(value)});
        try {
            index_.emplace(entries_.front().key, entries_.begin());
        } catch (...) {
            // Keep the list and the index describing the same set of entries.
            entries_.pop_front();
            throw;
        }
    }

    // Runs fn on the value while the lock is still held, and marks the entry as most recently used.
    // This is the only read path that works for move-only values and cannot hand out a dangling
    // reference. fn must not call back into this cache, since the mutex is not recursive.
    template <typename F>
        requires std::invocable<F, const V&>
    bool visit(const K& key, F&& fn) {
        std::lock_guard lock(mutex_);

        auto found = index_.find(key);
        if (found == index_.end()) {
            return false;
        }
        entries_.splice(entries_.begin(), entries_, found->second);
        std::invoke(std::forward<F>(fn), std::as_const(found->second->value));
        return true;
    }

    // Convenience for copyable values: the copy is taken under the lock, so the caller owns a
    // snapshot that stays valid regardless of later evictions.
    std::optional<V> get(const K& key)
        requires std::copy_constructible<V>
    {
        std::optional<V> result;
        visit(key, [&result](const V& value) { result.emplace(value); });
        return result;
    }

    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return entries_.size();
    }

    std::size_t capacity() const noexcept { return capacity_; }

private:
    struct Entry {
        K key;
        V value;
    };

    using EntryList = std::list<Entry>;

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    // Front is most recently used. std::list is chosen because splice() reorders without
    // invalidating the iterators the index holds.
    EntryList entries_;
    std::unordered_map<K, typename EntryList::iterator, Hash, KeyEqual> index_;
};

}  // namespace lru
