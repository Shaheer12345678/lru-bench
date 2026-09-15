#pragma once

// Registers the benchmark matrix with Google Benchmark: every design x capacity x write ratio x key
// distribution, at the thread counts a given executable asks for.

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "runner.hpp"

namespace lru_bench {

// Pregenerated operations for one workload (distribution and write ratio), shared by every design and
// thread count so all of them replay identical operations.
struct WorkloadStreams {
    std::vector<Op> warmup;
    std::vector<std::vector<Op>> per_thread;
};

// Only called from Setup, which Google Benchmark runs on the main thread before any worker thread starts,
// so the map is never touched concurrently and worker threads only ever read finished streams.
inline const WorkloadStreams& streams_for(KeyDistribution distribution, unsigned write_percent,
                                          std::size_t threads) {
    static std::map<std::pair<int, unsigned>, WorkloadStreams> all;
    WorkloadStreams& streams = all[{static_cast<int>(distribution), write_percent}];
    const WorkloadSpec spec = spec_for(distribution, write_percent);
    if (streams.warmup.empty()) {
        streams.warmup = generate_ops(spec, max_warmup_length(), stream_seed(distribution, write_percent, kWarmupStream));
    }
    while (streams.per_thread.size() < threads) {
        const std::uint64_t index = streams.per_thread.size();
        streams.per_thread.push_back(
            generate_ops(spec, kStreamLength, stream_seed(distribution, write_percent, index)));
    }
    return streams;
}

// The cache and streams for the run in progress. Setup builds them before the threads start and Teardown
// discards them, so every timed run starts from a fresh, identically warmed cache and repetitions are
// independent samples rather than a continuation of one another.
template <typename Design>
struct ActiveRun {
    static inline std::unique_ptr<typename Design::Cache> cache;
    static inline const WorkloadStreams* streams = nullptr;
};

struct CellParams {
    std::size_t capacity;
    unsigned write_percent;
    KeyDistribution distribution;
};

inline CellParams params_of(const benchmark::State& state) {
    return CellParams{capacity_for(static_cast<unsigned>(state.range(0))), static_cast<unsigned>(state.range(1)),
                      state.range(2) != 0 ? KeyDistribution::kZipfian : KeyDistribution::kUniform};
}

template <typename Design>
void setup_run(const benchmark::State& state) {
    const CellParams cell = params_of(state);
    const WorkloadStreams& streams =
        streams_for(cell.distribution, cell.write_percent, static_cast<std::size_t>(state.threads()));
    ActiveRun<Design>::cache = Design::make(cell.capacity);
    warm_up(*ActiveRun<Design>::cache, streams.warmup, warmup_length(cell.capacity));
    ActiveRun<Design>::streams = &streams;
}

template <typename Design>
void teardown_run(const benchmark::State&) {
    ActiveRun<Design>::cache.reset();
    ActiveRun<Design>::streams = nullptr;
}

inline double unix_time_ms() {
    return std::chrono::duration<double, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// One thread of a timed run. Counters are summed across threads by Google Benchmark, so the report holds
// the total operations per second and the raw hit and read counts from which hit rate is computed.
template <typename Design>
void run_thread(benchmark::State& state) {
    typename Design::Cache& cache = *ActiveRun<Design>::cache;
    const std::vector<Op>& ops = ActiveRun<Design>::streams->per_thread[static_cast<std::size_t>(state.thread_index())];

    OpCounts counts;
    std::size_t next = 0;
    const double started_ms = unix_time_ms();
    for ([[maybe_unused]] auto _ : state) {
        apply(cache, ops[next], counts);
        if (++next == ops.size()) {
            next = 0;
        }
    }
    const double ended_ms = unix_time_ms();

    state.SetItemsProcessed(state.iterations());
    // Wall-clock bounds of this timed run, averaged over its threads. Random interleaving does not record the
    // order repetitions ran in, and on a throttling machine that order is what shows whether a result was taken
    // in the same thermal state as the results it is compared with.
    state.counters["start_unix_ms"] = benchmark::Counter(started_ms, benchmark::Counter::kAvgThreads);
    state.counters["end_unix_ms"] = benchmark::Counter(ended_ms, benchmark::Counter::kAvgThreads);
    state.counters["reads"] = benchmark::Counter(static_cast<double>(counts.reads));
    state.counters["hits"] = benchmark::Counter(static_cast<double>(counts.hits));
    state.counters["writes"] = benchmark::Counter(static_cast<double>(counts.writes));
    state.counters["miss_puts"] = benchmark::Counter(static_cast<double>(counts.miss_puts));
}

inline std::vector<std::int64_t> as_args(const auto& values) {
    std::vector<std::int64_t> args;
    for (const auto value : values) {
        args.push_back(static_cast<std::int64_t>(value));
    }
    return args;
}

template <typename Design>
void register_design(int threads) {
    benchmark::RegisterBenchmark(Design::name(), &run_thread<Design>)
        ->ArgsProduct({as_args(kCapacityPercents), as_args(kWritePercents), {0, 1}})
        ->ArgNames({"cap_pct", "write_pct", "zipf"})
        ->Threads(threads)
        ->UseRealTime()
        ->Setup(&setup_run<Design>)
        ->Teardown(&teardown_run<Design>);
}

// Records the workload definition in every JSON report, so a result file explains itself without this
// source at hand.
inline void add_workload_context() {
    benchmark::AddCustomContext("key_space", std::to_string(kKeySpace));
    benchmark::AddCustomContext("zipf_theta", std::to_string(kZipfTheta));
    benchmark::AddCustomContext("stream_length_per_thread", std::to_string(kStreamLength));
    benchmark::AddCustomContext("warmup_ops", "max(1000000, 4 x capacity), load on miss, single threaded");
    benchmark::AddCustomContext("miss_policy", "load on miss: a read that misses puts the key");
    benchmark::AddCustomContext("v3_shard_count", std::to_string(kMatrixShardCount));
    benchmark::AddCustomContext("cxx_compiler", __VERSION__);
#ifdef NDEBUG
    benchmark::AddCustomContext("assertions", "disabled");
#else
    benchmark::AddCustomContext("assertions", "enabled");
#endif
}

// Parses and removes --lru_threads=1,2,4,8 from the command line. Returns false if the flag is malformed.
//
// Registering every thread count in one process matters because Google Benchmark interleaves repetitions only
// within a process. When one executable ran all single-thread cells and a second ran the rest afterwards, thread
// count was tied to how long the machine had already been under load, so a throttling CPU made the scaling curve
// partly a record of run order.
inline bool take_thread_counts(int& argc, char** argv, std::vector<int>& thread_counts) {
    const std::string prefix = "--lru_threads=";
    int kept = 1;
    bool ok = true;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg.rfind(prefix, 0) != 0) {
            argv[kept++] = argv[i];
            continue;
        }
        thread_counts.clear();
        std::size_t begin = prefix.size();
        while (begin <= arg.size()) {
            std::size_t end = arg.find(',', begin);
            if (end == std::string::npos) {
                end = arg.size();
            }
            const std::string item = arg.substr(begin, end - begin);
            const bool digits = !item.empty() && item.find_first_not_of("0123456789") == std::string::npos;
            const long value = digits && item.size() <= 3 ? std::stol(item) : 0;
            if (value < 1 || value > static_cast<long>(kMaxThreads)) {
                ok = false;
            } else if (std::find(thread_counts.begin(), thread_counts.end(), static_cast<int>(value)) ==
                       thread_counts.end()) {
                thread_counts.push_back(static_cast<int>(value));
            }
            begin = end + 1;
        }
    }
    argc = kept;
    argv[argc] = nullptr;
    return ok && !thread_counts.empty();
}

inline int run_matrix(int argc, char** argv, std::initializer_list<int> default_thread_counts) {
    std::vector<int> thread_counts(default_thread_counts);
    if (!take_thread_counts(argc, argv, thread_counts)) {
        std::fprintf(stderr, "--lru_threads expects a comma-separated list of thread counts from 1 to %zu\n",
                     kMaxThreads);
        return 1;
    }
    std::string registered;
    for (const int threads : thread_counts) {
        register_design<V1Design>(threads);
        register_design<V2Design>(threads);
        register_design<V3Design>(threads);
        registered += (registered.empty() ? "" : ",") + std::to_string(threads);
    }
    benchmark::AddCustomContext("thread_counts_in_process", registered);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    add_workload_context();
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}

}  // namespace lru_bench
