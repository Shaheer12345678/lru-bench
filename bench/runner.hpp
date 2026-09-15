#pragma once

// What every benchmark in this directory shares: the matrix parameters, how one operation is applied to
// a cache, and how a cache is brought to steady state. Kept free of Google Benchmark so the hit-rate
// sweep replays operations through exactly the same code path as the timed matrix.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "lru/IntrusiveLru.hpp"
#include "lru/LruCache.hpp"
#include "lru/ShardedLru.hpp"
#include "workloads.hpp"

namespace lru_bench {

inline constexpr std::uint64_t kKeySpace = 1'000'000;
inline constexpr double kZipfTheta = 0.99;  // the YCSB default
inline constexpr std::array<unsigned, 3> kCapacityPercents = {1, 10, 50};
inline constexpr std::array<unsigned, 2> kWritePercents = {5, 50};
inline constexpr std::array<KeyDistribution, 2> kDistributions = {KeyDistribution::kUniform,
                                                                  KeyDistribution::kZipfian};
inline constexpr std::size_t kMaxThreads = 8;

// At least twice the largest thread count, so eight threads rarely contend for one shard.
inline constexpr std::size_t kMatrixShardCount = 16;

// Operations per thread in a timed run. The stream repeats if a run needs more; two million is long
// enough that a key's previous occurrence in the stream is rarely what keeps it in the cache.
inline constexpr std::size_t kStreamLength = std::size_t{1} << 21;

inline std::size_t capacity_for(unsigned capacity_percent) {
    return static_cast<std::size_t>(kKeySpace * capacity_percent / 100);
}

// Long enough to fill every uniform-key cache and to let a Zipfian cache settle into its steady mix of
// hot and cold entries, and the same for every design so none starts from a warmer state.
inline std::size_t warmup_length(std::size_t capacity) {
    return std::max<std::size_t>(1'000'000, 4 * capacity);
}

inline std::size_t max_warmup_length() {
    return warmup_length(capacity_for(kCapacityPercents.back()));
}

inline const char* distribution_name(KeyDistribution distribution) {
    return distribution == KeyDistribution::kUniform ? "uniform" : "zipfian";
}

inline WorkloadSpec spec_for(KeyDistribution distribution, unsigned write_percent) {
    return WorkloadSpec{distribution, kKeySpace, kZipfTheta, write_percent};
}

// Distinct, fixed seeds per workload and per stream, so runs are repeatable and threads never replay
// each other's operations. The warmup stream has its own seed so warming never pre-plays the timed ops.
inline std::uint64_t stream_seed(KeyDistribution distribution, unsigned write_percent, std::uint64_t stream) {
    SplitMix64 mix((static_cast<std::uint64_t>(distribution) << 32) ^ (std::uint64_t{write_percent} << 16) ^ stream);
    return mix.next();
}

inline constexpr std::uint64_t kWarmupStream = 1000;

struct OpCounts {
    std::uint64_t reads = 0;
    std::uint64_t hits = 0;
    std::uint64_t writes = 0;
    std::uint64_t miss_puts = 0;
};

// Load on miss: a read that misses puts the key, as an application does when it falls back to the
// slower store behind the cache. Hit rate then measures how well a design keeps the keys the workload
// asks for. A write replaces the value unconditionally.
template <typename Cache>
inline void apply(Cache& cache, Op op, OpCounts& counts) {
    const std::uint64_t key = key_of(op);
    if (is_write(op)) {
        cache.put(key, key);
        ++counts.writes;
        return;
    }
    ++counts.reads;
    if (cache.get(key).has_value()) {
        ++counts.hits;
    } else {
        cache.put(key, key);
        ++counts.miss_puts;
    }
}

template <typename Cache>
inline void warm_up(Cache& cache, const std::vector<Op>& warmup_ops, std::size_t length) {
    OpCounts ignored;
    for (std::size_t i = 0; i < length; ++i) {
        apply(cache, warmup_ops[i], ignored);
    }
}

// Designs under test. Each names itself for reports and knows how to build a cache of a given capacity.
struct V1Design {
    using Cache = lru::LruCache<std::uint64_t, std::uint64_t>;
    static std::string name() { return "v1_LruCache"; }
    static std::unique_ptr<Cache> make(std::size_t capacity) { return std::make_unique<Cache>(capacity); }
};

struct V2Design {
    using Cache = lru::IntrusiveLru<std::uint64_t, std::uint64_t>;
    static std::string name() { return "v2_IntrusiveLru"; }
    static std::unique_ptr<Cache> make(std::size_t capacity) { return std::make_unique<Cache>(capacity); }
};

struct V3Design {
    using Cache = lru::ShardedLru<std::uint64_t, std::uint64_t>;
    static std::string name() { return "v3_ShardedLru" + std::to_string(kMatrixShardCount); }
    static std::unique_ptr<Cache> make(std::size_t capacity) {
        return std::make_unique<Cache>(capacity, kMatrixShardCount);
    }
};

}  // namespace lru_bench
