# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html). No release has been tagged yet.

## [Unreleased]

### Added

- Documentation
  - Project README covering the design progression, testing approach and published results.
  - `tests/README.md`, the record of mutation testing and sanitizer verification.
  - This changelog.
- Results and benchmarking
  - Google Benchmark harness (`bench/`) behind the `LRU_BUILD_BENCH` option: a matrix of design, capacity, read:write
    ratio, key distribution and thread count, with load-on-miss reads and a freshly warmed cache for every timed run.
  - `--lru_threads` option to register every thread count in one process, so random interleaving covers thread counts
    as well as designs, and per-repetition wall-clock timestamps (`start_unix_ms`, `end_unix_ms`) to show run order.
  - `hit_rate_sweep`: deterministic hit rate for v1, v2 and v3 at 1 to 64 shards; exits non-zero if v1, v2 and v3 with
    one shard ever disagree on hit counts.
  - `memory_footprint`: bytes requested, allocation counts and resident memory per design and capacity.
  - `results/README.md` and `results/raw/`: hardware-independent results with provenance, the throughput harness guide,
    and the log of benchmarking attempts on a 15 W laptop, including why no throughput numbers are published.
  - CI `bench-smoke` job that builds the harness with gcc and clang and runs each tool.
- v3 `ShardedLru`
  - N `IntrusiveLru` shards, each cache-line aligned with its own mutex. Capacity split as floor plus remainder;
    shard count clamped to capacity; `shard_count()` and `shard_for()` accessors.
  - Contract suite instantiated at 1, 2, 7, 16 and 32 shards, strict-order suite at 1 shard, per-shard ordering tests,
    capacity-split and clamping tests, and an operation-by-operation comparison against `IntrusiveLru`.
  - The concurrency and allocation tests cover v3 with 16 shards; the concurrency test asserts that every thread
    reached every shard.
- v2 `IntrusiveLru`
  - Preallocated node arena with an intrusive recency list and chained hash index; no allocation after construction.
  - Both shared suites instantiated against v2.
  - Allocation test that replaces global `operator new`, with v1 as the non-zero control.
  - Contract test `JustInsertedKeyIsAlwaysRetrievable`, closing a gap found by mutation testing: a full cache that
    dropped new keys passed the contract suite.
  - Regression tests for the bucket count and shift at the smallest capacities.
- Concurrency and sanitizers
  - Concurrency stress test: 8 threads, mixed operations, checksummed records so corrupt or misfiled values are
    detected, not only size violations. Operation count set by `LRU_CONCURRENT_OPS`.
  - `LRU_SANITIZER` option (address, undefined, thread) applied to the whole build, including GoogleTest.
  - CI jobs for ASan, UBSan and TSan.
- v1 `LruCache` and project foundation
  - `LruCache`: `std::list` and `std::unordered_map` behind one mutex, with `put`, `get`, `visit`, `size` and
    `capacity`; works with move-only values.
  - `LruCacheLike` concept describing the contract every design satisfies.
  - Shared correctness suite written once as typed-parameterized tests: a contract suite and a strict-order suite.
  - CMake project with a header-only `lru::lru` target, warnings as errors for the repository's own targets, and
    GoogleTest fetched with FetchContent.
  - CI matrix of gcc and clang in Debug and Release.

### Changed

- v2 bucket arithmetic moved into `lru::detail` functions so tests can check it directly. Behaviour is unchanged.
- The allocation test is not built in sanitizer builds, where the sanitizer runtime supplies `operator new`.
- The CI sanitizer jobs log the runner's `vm.mmap_rnd_bits` before lowering it to 28.

[Unreleased]: https://github.com/Shaheer12345678/lru-bench/commits/main
