// Proves, rather than asserts, that IntrusiveLru does not allocate once it is constructed.
//
// Allocations are counted by replacing the global operator new and delete for this executable.
// A counting allocator was the alternative, but neither cache takes an allocator parameter, so it
// could not reach the list and hash map nodes inside LruCache without changing the public API, and
// the claim being tested is "never calls the allocator", which only a process-wide hook can see.
//
// The hook counts every allocation in the process, including any made by the key or value types:
// a std::string key longer than its small-buffer size allocates when the caller builds it, whatever
// the cache does. The workload below therefore uses integer keys and values, and the measured window
// is single threaded and contains nothing but cache operations, so every counted allocation belongs
// to the cache.
//
// The same workload runs against LruCache as a control. A counter that was never wired up would read
// zero for both designs, which is indistinguishable from a genuine pass unless the control is non-zero.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <optional>

#include "factories.hpp"

namespace {

std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_bytes{0};

void* counted_alloc(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* counted_aligned_alloc(std::size_t size, std::align_val_t alignment) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    g_bytes.fetch_add(size, std::memory_order_relaxed);
    const auto align = static_cast<std::size_t>(alignment);
    // aligned_alloc requires the size to be a non-zero multiple of the alignment.
    const std::size_t rounded = size == 0 ? align : (size + align - 1) / align * align;
    if (void* p = std::aligned_alloc(align, rounded)) {
        return p;
    }
    throw std::bad_alloc();
}

}  // namespace

void* operator new(std::size_t size) { return counted_alloc(size); }
void* operator new[](std::size_t size) { return counted_alloc(size); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    return counted_aligned_alloc(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return counted_aligned_alloc(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return counted_alloc(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return counted_alloc(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return counted_aligned_alloc(size, alignment);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        return counted_aligned_alloc(size, alignment);
    } catch (...) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { std::free(p); }

namespace lru_test {
namespace {

constexpr std::size_t kCapacity = 4096;
constexpr std::uint64_t kKeySpace = 4 * kCapacity;
constexpr std::uint64_t kSteadyStateOps = 200000;

struct AllocationCount {
    std::uint64_t allocations = 0;
    std::uint64_t bytes = 0;
};

AllocationCount counted_so_far() noexcept {
    return {g_allocations.load(std::memory_order_relaxed), g_bytes.load(std::memory_order_relaxed)};
}

// xorshift64: a key generator that cannot allocate, unlike the standard engines' seeding helpers.
std::uint64_t next_random(std::uint64_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

// Counts allocations made by a mixed put/get/visit workload on a cache that is already full and has
// already evicted, so any one-off allocation on first insert or first eviction is excluded.
template <typename Factory>
AllocationCount steady_state_allocations() {
    auto cache = Factory::template make<std::uint64_t, std::uint64_t>(kCapacity);
    for (std::uint64_t key = 0; key < 2 * kCapacity; ++key) {
        cache.put(key, key);
    }

    std::uint64_t state = 0x243F6A8885A308D3ULL;
    std::uint64_t hits = 0;

    const AllocationCount before = counted_so_far();
    for (std::uint64_t i = 0; i < kSteadyStateOps; ++i) {
        const std::uint64_t key = next_random(state) % kKeySpace;
        const std::uint64_t op = next_random(state) % 10;
        if (op < 5) {
            cache.put(key, i);
        } else if (op < 8) {
            if (const std::optional<std::uint64_t> value = cache.get(key)) {
                ++hits;
            }
        } else if (cache.visit(key, [](const std::uint64_t&) {})) {
            ++hits;
        }
    }
    const AllocationCount after = counted_so_far();

    // Checked after the window closes, because gtest assertions may allocate.
    EXPECT_EQ(cache.size(), kCapacity);
    EXPECT_GT(hits, 0u);
    return {after.allocations - before.allocations, after.bytes - before.bytes};
}

TEST(SteadyStateAllocations, IntrusiveLruAllocatesNothingWhileLruCacheDoes) {
    const AllocationCount v1 = steady_state_allocations<V1Factory>();
    const AllocationCount v2 = steady_state_allocations<V2Factory>();

    std::cout << "[ counts   ] " << kSteadyStateOps << " ops on a full cache of " << kCapacity
              << ": LruCache " << v1.allocations << " allocations (" << v1.bytes << " bytes), "
              << "IntrusiveLru " << v2.allocations << " allocations (" << v2.bytes << " bytes)\n";

    ASSERT_GT(v1.allocations, 0u) << "the control made no allocations, so the counter is not working";
    EXPECT_EQ(v2.allocations, 0u) << v2.bytes << " bytes allocated";
}

}  // namespace
}  // namespace lru_test
