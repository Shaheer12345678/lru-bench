// Concurrency stress test: many threads mixing reads and writes on one cache.
//
// Size bounds alone would miss the more dangerous failure, a value that is torn, freed or never
// written, so every value read back is validated as well. The test is most useful under
// ThreadSanitizer and AddressSanitizer, which turn a lucky pass into a reported race or bad access.

#include <gtest/gtest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <optional>
#include <random>
#include <thread>
#include <vector>

#include "factories.hpp"

// Sanitizer builds run many times slower, so the build can lower this without editing the test.
#ifndef LRU_CONCURRENT_OPS
#define LRU_CONCURRENT_OPS 1000000
#endif

namespace lru_test {
namespace {

constexpr std::uint64_t kTotalOps = LRU_CONCURRENT_OPS;
constexpr int kThreads = 8;
constexpr std::size_t kCapacity = 1024;
// Four keys per slot keeps the cache full and evicting constantly, which is where the list and the
// index are most likely to fall out of step.
constexpr std::uint64_t kKeySpace = 4 * kCapacity;

// splitmix64 finaliser: cheap, and every input bit affects every output bit, so a checksum built
// from it changes completely if any field of a record is damaged.
constexpr std::uint64_t mix(std::uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// A value spanning several words whose fields must agree with each other and with the key it was
// stored under. A torn write, a read of freed memory, or a value filed under the wrong key breaks
// that agreement, which a single integer value could not reveal.
struct Record {
    std::uint64_t key = 0;
    std::uint64_t stamp = 0;
    std::uint64_t checksum = 0;

    static Record make(std::uint64_t key, std::uint64_t stamp) noexcept {
        return Record{key, stamp, mix(key ^ mix(stamp))};
    }

    bool valid_for(std::uint64_t expected_key) const noexcept {
        return key == expected_key && checksum == mix(key ^ mix(stamp));
    }
};

void raise_to(std::atomic<std::size_t>& maximum, std::size_t candidate) noexcept {
    std::size_t current = maximum.load(std::memory_order_relaxed);
    while (candidate > current &&
           !maximum.compare_exchange_weak(current, candidate, std::memory_order_relaxed)) {
    }
}

template <typename Factory>
class LruConcurrentTest : public ::testing::Test {};

using ConcurrentFactories = ::testing::Types<V1Factory>;

TYPED_TEST_SUITE(LruConcurrentTest, ConcurrentFactories);

TYPED_TEST(LruConcurrentTest, MixedOpsKeepSizeBoundedAndValuesIntact) {
    auto cache = TypeParam::template make<std::uint64_t, Record>(kCapacity);

    // Failures are counted rather than asserted inside the threads, so one bad interleaving yields
    // a single clear report instead of thousands of lines, and nothing depends on gtest assertions
    // being safe to call from worker threads.
    std::atomic<std::uint64_t> size_violations{0};
    std::atomic<std::uint64_t> corrupt_reads{0};
    std::atomic<std::uint64_t> hits{0};
    std::atomic<std::size_t> max_size_seen{0};
    std::atomic<bool> workers_done{false};

    auto observe_size = [&] {
        const std::size_t size = cache.size();
        raise_to(max_size_seen, size);
        if (size > cache.capacity()) {
            size_violations.fetch_add(1, std::memory_order_relaxed);
        }
    };

    auto check_read = [&](std::uint64_t key, const Record& record) {
        hits.fetch_add(1, std::memory_order_relaxed);
        if (!record.valid_for(key)) {
            corrupt_reads.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Every thread checks in on `ready`, then blocks on `go`, which opens only once all have checked
    // in. Without this the first threads would run alone while later ones are still being created.
    std::latch ready(kThreads + 1);
    std::latch go(1);

    // Polls size continuously while the workers run, catching a transient overshoot that a check
    // made only by the workers or only at the end could miss.
    std::thread monitor([&] {
        ready.count_down();
        go.wait();
        while (!workers_done.load(std::memory_order_relaxed)) {
            observe_size();
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        const auto thread_index = static_cast<std::uint64_t>(t);
        const std::uint64_t ops =
            kTotalOps / kThreads + (t == 0 ? kTotalOps % kThreads : 0);

        workers.emplace_back([&, thread_index, ops] {
            // Fixed seeds keep each thread's operation sequence reproducible between runs, even
            // though the interleaving is not.
            std::mt19937_64 rng(mix(thread_index));
            ready.count_down();
            go.wait();

            for (std::uint64_t i = 0; i < ops; ++i) {
                const std::uint64_t key = rng() % kKeySpace;
                const std::uint64_t op = rng() % 10;

                if (op < 5) {
                    cache.put(key, Record::make(key, (thread_index << 48) | i));
                } else if (op < 8) {
                    if (const std::optional<Record> record = cache.get(key)) {
                        check_read(key, *record);
                    }
                } else {
                    cache.visit(key, [&](const Record& record) { check_read(key, record); });
                }

                if ((i & 0xFF) == 0) {
                    observe_size();
                }
            }
        });
    }

    ready.wait();
    go.count_down();

    for (std::thread& worker : workers) {
        worker.join();
    }
    workers_done.store(true, std::memory_order_relaxed);
    monitor.join();

    EXPECT_EQ(size_violations.load(), 0u) << "largest size observed: " << max_size_seen.load();
    EXPECT_EQ(corrupt_reads.load(), 0u);
    EXPECT_GT(hits.load(), 0u) << "no read ever hit, so value integrity was never exercised";
    EXPECT_LE(cache.size(), cache.capacity());

    // With the threads stopped, every stored entry must still be valid and reachable, and the
    // number reachable through the index must match the size the cache reports.
    std::uint64_t reachable = 0;
    std::uint64_t corrupt_at_rest = 0;
    for (std::uint64_t key = 0; key < kKeySpace; ++key) {
        cache.visit(key, [&](const Record& record) {
            ++reachable;
            if (!record.valid_for(key)) {
                ++corrupt_at_rest;
            }
        });
    }
    EXPECT_EQ(corrupt_at_rest, 0u);
    EXPECT_EQ(reachable, cache.size());
}

}  // namespace
}  // namespace lru_test
