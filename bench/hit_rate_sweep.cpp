// Hit rate versus shard count, as JSON lines on stdout.
//
// Hit rate from the timed matrix depends on how many operations each run happened to execute. This tool
// replays a fixed-length operation stream single threaded instead, so every figure is exactly
// reproducible. It covers v1, v2 and v3 at 1 to 64 shards for every workload in the matrix, which turns
// the matrix's single 16-shard point into the full cost curve of sharding.
//
// v1, v2 and v3 with one shard all implement exact LRU, so on identical input they must produce identical
// hit counts. The tool checks that and exits non-zero if they disagree, since any difference would mean a
// correctness bug rather than a design tradeoff.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runner.hpp"

namespace {

using lru_bench::KeyDistribution;
using lru_bench::Op;
using lru_bench::OpCounts;

inline constexpr std::size_t kMeasuredOps = 4'000'000;
inline constexpr std::uint64_t kSweepStream = 2000;
inline constexpr std::array<std::size_t, 7> kShardCounts = {1, 2, 4, 8, 16, 32, 64};

struct Result {
    OpCounts counts;
    std::size_t final_size;
};

template <typename Cache>
Result replay(Cache& cache, const std::vector<Op>& warmup, std::size_t warmup_ops, const std::vector<Op>& measured) {
    lru_bench::warm_up(cache, warmup, warmup_ops);
    Result result{};
    for (const Op op : measured) {
        lru_bench::apply(cache, op, result.counts);
    }
    result.final_size = cache.size();
    return result;
}

void print(KeyDistribution distribution, unsigned write_percent, std::size_t capacity, const std::string& design,
           std::size_t shards, const Result& r) {
    const double hit_rate =
        r.counts.reads == 0 ? 0.0 : static_cast<double>(r.counts.hits) / static_cast<double>(r.counts.reads);
    std::printf("{\"distribution\": \"%s\", \"write_pct\": %u, \"capacity\": %zu, \"design\": \"%s\", "
                "\"shards\": %zu, \"reads\": %llu, \"hits\": %llu, \"hit_rate\": %.6f, \"writes\": %llu, "
                "\"miss_puts\": %llu, \"final_size\": %zu}\n",
                lru_bench::distribution_name(distribution), write_percent, capacity, design.c_str(), shards,
                static_cast<unsigned long long>(r.counts.reads), static_cast<unsigned long long>(r.counts.hits),
                hit_rate, static_cast<unsigned long long>(r.counts.writes),
                static_cast<unsigned long long>(r.counts.miss_puts), r.final_size);
}

}  // namespace

int main() {
    bool exact_lru_agrees = true;

    for (const KeyDistribution distribution : lru_bench::kDistributions) {
        for (const unsigned write_percent : lru_bench::kWritePercents) {
            const lru_bench::WorkloadSpec spec = lru_bench::spec_for(distribution, write_percent);
            const std::vector<Op> warmup = lru_bench::generate_ops(
                spec, lru_bench::max_warmup_length(),
                lru_bench::stream_seed(distribution, write_percent, lru_bench::kWarmupStream));
            const std::vector<Op> measured = lru_bench::generate_ops(
                spec, kMeasuredOps, lru_bench::stream_seed(distribution, write_percent, kSweepStream));

            for (const unsigned capacity_percent : lru_bench::kCapacityPercents) {
                const std::size_t capacity = lru_bench::capacity_for(capacity_percent);
                const std::size_t warmup_ops = lru_bench::warmup_length(capacity);

                lru_bench::V1Design::Cache v1(capacity);
                const Result r1 = replay(v1, warmup, warmup_ops, measured);
                print(distribution, write_percent, capacity, lru_bench::V1Design::name(), 1, r1);

                lru_bench::V2Design::Cache v2(capacity);
                const Result r2 = replay(v2, warmup, warmup_ops, measured);
                print(distribution, write_percent, capacity, lru_bench::V2Design::name(), 1, r2);

                for (const std::size_t shards : kShardCounts) {
                    lru_bench::V3Design::Cache v3(capacity, shards);
                    const Result r3 = replay(v3, warmup, warmup_ops, measured);
                    print(distribution, write_percent, capacity, "v3_ShardedLru", shards, r3);

                    if (shards == 1 && (r3.counts.hits != r1.counts.hits || r2.counts.hits != r1.counts.hits)) {
                        std::fprintf(stderr,
                                     "exact LRU disagreement: %s write_pct %u capacity %zu: v1 %llu, v2 %llu, "
                                     "v3 x1 %llu hits\n",
                                     lru_bench::distribution_name(distribution), write_percent, capacity,
                                     static_cast<unsigned long long>(r1.counts.hits),
                                     static_cast<unsigned long long>(r2.counts.hits),
                                     static_cast<unsigned long long>(r3.counts.hits));
                        exact_lru_agrees = false;
                    }
                }
            }
        }
    }
    return exact_lru_agrees ? 0 : 1;
}
