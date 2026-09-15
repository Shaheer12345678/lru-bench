# Results

This directory holds the measured results for the three cache designs and the raw data behind every figure.

## What is published, and what it depends on

| Result | Depends on | Published |
|---|---|---|
| Hit rate by design and shard count | The code and the input streams (Zipfian keys go through `std::pow`, so the C math library) | Yes |
| Exact-LRU agreement of v1, v2 and v3 with one shard | The code and the input streams | Yes |
| Steady-state allocation counts | The code and the C++ standard library implementation | Yes |
| Bytes requested and allocation counts at construction and fill | The code and the C++ standard library implementation | Yes |
| Resident memory (RSS) | The operating system, allocator and page size as well | For context only |
| Throughput (ops/sec) and thread scaling | The CPU, its power and thermal limits, the OS scheduler | **No** |

None of the published results depend on how fast the machine is. No ops/sec figure appears anywhere in this repository's
results; the section [Why no throughput numbers](#why-no-throughput-numbers) explains why.

## Hardware-independent results

Run `deterministic-20260915T194531Z` in [`raw/`](raw/deterministic-20260915T194531Z), with `summary.txt` as the
readable form. Provenance, from `environment.txt` in that directory:

- Code hash `c6bfb288c856c322f283b963bfdfac167987f0d8cd6b8bf7acdfd58bc0b6caa2`, computed from a checkout with
  `find include bench tests CMakeLists.txt -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum`.
- Built twice, CMake Release: g++ 11.4.0 and clang 14.0.0, both on glibc 2.35 (Ubuntu 22.04). Every measurement below
  was taken with both builds. The hit-rate sweep output is byte-identical between them; bytes requested, allocation
  counts and final sizes are identical.

### Hit rate versus shard count

`bench/hit_rate_sweep` replays a fixed operation stream single threaded through v1, v2 and v3 at 1 to 64 shards.

Inputs, all fixed in `bench/runner.hpp` and `bench/workloads.hpp`:

- Key space 1,000,000. Uniform keys, or Zipfian keys with theta 0.99 (the YCSB generator).
- Read:write 95:5 or 50:50. A read that misses puts the key (load on miss); a write puts unconditionally.
- Capacity 10,000, 100,000 or 500,000 (1%, 10%, 50% of the key space).
- Streams from splitmix64. The seed for a stream is the first output of splitmix64 seeded with
  `(distribution << 32) ^ (write_percent << 16) ^ stream`, where `distribution` is 0 for uniform and 1 for Zipfian.
  Warmup uses stream 1000 for `max(1,000,000, 4 x capacity)` operations; the measured window is stream 2000 for
  4,000,000 operations.
- v3 shard selection: splitmix64 finaliser of `std::hash<uint64_t>` (the identity), modulo the shard count.

Hit rate over the measured window (hits divided by reads):

| Distribution | Read:write | Capacity | v1 | v2 | v3 x1 | v3 x2 | v3 x4 | v3 x8 | v3 x16 | v3 x32 | v3 x64 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| uniform | 95:5 | 10,000 | 0.0100 | 0.0100 | 0.0100 | 0.0100 | 0.0100 | 0.0100 | 0.0099 | 0.0099 | 0.0100 |
| uniform | 95:5 | 100,000 | 0.1000 | 0.1000 | 0.1000 | 0.1000 | 0.1000 | 0.1000 | 0.1000 | 0.1000 | 0.1000 |
| uniform | 95:5 | 500,000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 |
| uniform | 50:50 | 10,000 | 0.0100 | 0.0100 | 0.0100 | 0.0100 | 0.0099 | 0.0099 | 0.0100 | 0.0100 | 0.0100 |
| uniform | 50:50 | 100,000 | 0.1002 | 0.1002 | 0.1002 | 0.1002 | 0.1002 | 0.1002 | 0.1002 | 0.1002 | 0.1002 |
| uniform | 50:50 | 500,000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5000 | 0.5001 | 0.5000 |
| zipfian | 95:5 | 10,000 | 0.5732 | 0.5732 | 0.5732 | 0.5732 | 0.5732 | 0.5732 | 0.5731 | 0.5731 | 0.5729 |
| zipfian | 95:5 | 100,000 | 0.7701 | 0.7701 | 0.7701 | 0.7701 | 0.7701 | 0.7701 | 0.7700 | 0.7700 | 0.7700 |
| zipfian | 95:5 | 500,000 | 0.9210 | 0.9210 | 0.9210 | 0.9210 | 0.9210 | 0.9210 | 0.9210 | 0.9210 | 0.9209 |
| zipfian | 50:50 | 10,000 | 0.5727 | 0.5727 | 0.5727 | 0.5727 | 0.5726 | 0.5726 | 0.5726 | 0.5725 | 0.5724 |
| zipfian | 50:50 | 100,000 | 0.7695 | 0.7695 | 0.7695 | 0.7695 | 0.7695 | 0.7695 | 0.7695 | 0.7695 | 0.7695 |
| zipfian | 50:50 | 500,000 | 0.9208 | 0.9208 | 0.9208 | 0.9208 | 0.9208 | 0.9208 | 0.9208 | 0.9208 | 0.9208 |

Finding: at these capacities sharding costs almost no hit rate. The largest loss is 0.0003 (Zipfian, 95:5, 10,000
entries, 64 shards: 0.5729 against 0.5732 with one shard), where each shard holds only about 156 entries. Per-shard
LRU approximates global LRU closely once each shard holds more than a few hundred entries.

### Exact-LRU agreement

v1, v2 and v3 with one shard all implement exact LRU, so on identical input they must produce identical hit counts.
`hit_rate_sweep` checks this and exits non-zero on any disagreement. All 12 workloads agree exactly, for example
2,178,548 hits (Zipfian, 95:5, 10,000) and 1,899,515 hits (uniform, 95:5, 500,000). The CI `bench-smoke` job reruns
the check on every push.

### Steady-state allocations

`tests/test_allocations.cpp` replaces the global `operator new` and counts allocations during 200,000 operations
(50% put, 30% get, 20% visit) over 16,384 keys on a full cache of 4,096 entries that has already evicted. The key
generator is xorshift64 seeded with `0x243F6A8885A308D3`.

| Design | Allocations | Bytes |
|---|---|---|
| v1 `LruCache` (control) | 148,384 | 4,154,752 |
| v2 `IntrusiveLru` | 0 | 0 |
| v3 `ShardedLru`, 16 shards | 0 | 0 |

v1's 148,384 allocations are two per insert of a new key: a 32-byte list node and a 24-byte hash map node. The v1
control is what shows the counter works; a counter that was never wired up would also read zero for v2 and v3. The
test runs in CI on every push.

### Memory footprint

`bench/memory_footprint` builds one design per process, fills it with sequential distinct keys, and reports bytes
requested from `operator new` and resident memory. Allocations at construction include the cache object itself.

| Design | Capacity | Construction: bytes (allocations) | Fill to 1% | Fill to capacity | Size after inserting `capacity` keys |
|---|---|---|---|---|---|
| v1 | 10,000 | 82,312 (2) | 5,600 (200) | 554,400 (19,800) | 10,000 |
| v1 | 100,000 | 863,304 (2) | 56,000 (2,000) | 5,544,000 (198,000) | 100,000 |
| v1 | 500,000 | 4,162,056 (2) | 280,000 (10,000) | 27,720,000 (990,000) | 500,000 |
| v2 | 10,000 | 385,672 (3) | 0 (0) | 0 (0) | 10,000 |
| v2 | 100,000 | 3,724,424 (3) | 0 (0) | 0 (0) | 100,000 |
| v2 | 500,000 | 18,097,288 (3) | 0 (0) | 0 (0) | 500,000 |
| v3 x16 | 10,000 | 388,120 (34) | 0 (0) | 0 (0) | 9,800 |
| v3 x16 | 100,000 | 3,726,872 (34) | 0 (0) | 0 (0) | 99,557 |
| v3 x16 | 500,000 | 18,099,736 (34) | 0 (0) | 0 (0) | 499,146 |

- v1 allocates a bucket array at construction (`reserve(capacity)`) and then 56 bytes in two allocations per entry.
- v2 allocates everything at construction: the node array (32 bytes per slot for 64-bit keys and values, plus one
  sentinel) and the bucket array. Filling it allocates nothing. The cost is that a large capacity reserves its whole
  arena up front, used or not.
- v3 adds a 2,048-byte shard array (16 cache-line-aligned shards) and two allocations per shard.
- v3's final size is below capacity: after exactly `capacity` distinct inserts some shards have evicted while others
  still have room. This uneven fill is the mechanism behind any hit-rate cost of sharding.
- Resident memory, for context (gcc build, KiB after construction / 1% / full): v1 at 500,000 4,096 / 4,608 / 43,264;
  v2 2,048 / 2,176 / 17,664; v3 2,304 / 2,432 / 17,792. v2's node array is allocated without being written, so the
  operating system commits its pages only as slots are used.

A cost of the arena designs that does not show up in these numbers: AddressSanitizer cannot see a use-after-destroy
inside the arena. In mutation testing, v1's use-after-free on eviction failed 10 of 16 tests under ASan; the same
ordering bug in v2 and v3 passed every test with no report, because the arena's storage stays allocated for the
cache's whole lifetime.

## The throughput harness

`bench/bench_single_thread` and `bench/bench_multi_thread` are Google Benchmark executables that measure ops/sec and
hit rate for v1, v2 and v3 (16 shards). They are built and smoke-tested in CI on every push, so they compile and run,
but no throughput numbers from them are published here.

### What it measures

- The matrix: 3 designs x capacities 1%, 10%, 50% x read:write 95:5 and 50:50 x uniform and Zipfian keys x 1, 2, 4
  and 8 threads (144 cells). Same inputs as the hit-rate sweep.
- Each timed run builds a fresh cache and warms it single threaded before timing, so repetitions are independent.
  Operation streams are pregenerated, so key generation is never timed.
- Per repetition: `items_per_second`, `hits`, `reads`, `writes`, `miss_puts`, CPU and wall time, and wall-clock
  `start_unix_ms` and `end_unix_ms`.

### Run order: what interleaving does and does not protect

Google Benchmark's `--benchmark_enable_random_interleaving` shuffles repetitions, but only among the benchmarks
registered in one process. On a machine whose speed changes over a run, that makes comparisons between cells in the
same process fair in expectation, since a slowdown lands on every design equally. It does nothing for cells run in
different processes. The first version of this harness ran every single-thread cell in one process and every
multi-thread cell in a second process afterwards, so thread count was tied to how long the machine had been under
load, and a throttling CPU would have written its own decline into the scaling curve.

Two changes address this. `--lru_threads=1,2,4,8` registers every thread count in one process, so thread counts are
interleaved along with designs. `start_unix_ms` and `end_unix_ms` record when each repetition ran, so the order can be
checked afterwards instead of assumed.

### Running it

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLRU_BUILD_BENCH=ON -DLRU_BUILD_TESTS=OFF
cmake --build build -j

# One workload, every design and thread count interleaved in one process.
build/bench/bench_multi_thread --lru_threads=1,2,4,8 \
    --benchmark_filter='/cap_pct:10/write_pct:5/zipf:1/' \
    --benchmark_repetitions=10 --benchmark_min_time=0.5s --benchmark_enable_random_interleaving=true \
    --benchmark_format=json --benchmark_out=result.json

# Deterministic tools
build/bench/hit_rate_sweep > hit_rate_sweep.jsonl
build/bench/memory_footprint v2 100000
```

Report each cell as the median with its interquartile range across repetitions, and treat two cells as
distinguishable only if their IQRs do not overlap.

### What hardware it needs

- A CPU that holds its frequency under sustained load for the length of the run (the full 144-cell matrix at 10
  repetitions took 17 minutes on the machine below): a desktop or server with adequate cooling, not a thin-and-light
  laptop.
- At least 8 physical cores of one type if the 8-thread cells are meant to show scaling. Hybrid performance/efficiency
  cores and SMT siblings put core heterogeneity into the curve.
- A fixed power policy: mains power, a fixed frequency governor, and turbo either disabled or known to be stable.
- About 1 GB of free memory; the full multi-thread matrix peaked at 639,360 KiB RSS.
- A stability check before trusting a run: repeat one cell 30 times and require a low coefficient of variation with
  no step between the first and last repetitions.

## Why no throughput numbers

All attempts ran on an ASUS Vivobook X1504ZA: Intel Core i3-1215U (15 W, 2 performance cores with Hyper-Threading plus
4 efficiency cores, 8 logical CPUs), 8 GB RAM, Windows 11 with WSL2 Ubuntu 22.04 (3.85 GB to WSL), g++ 11.4.0, Google
Benchmark v1.9.5. Windows energy saver is locked on, the power mode is locked to Best Power Efficiency, and the processor
maximum state was set to 99% to cap turbo. The stability check used one cell throughout (v2, 1 thread, 10% capacity,
95:5, Zipfian); figures below are its median ops/sec. Raw data for every attempt listed with a directory is in
[`raw/`](raw). Runs that failed their validity checks were discarded and are not in this repository.

1. **Memory check.** The full multi-thread matrix peaked at 639,360 KiB RSS with no swapping.
2. **Stability check on mains power** (`gate-20260915T074453Z`): median 43.73 M, CV 2.36%. Passed.
3. **First full session (144 cells), discarded.** The charger was disconnected four minutes after the check and
   Windows entered Modern Standby twice during the run. The check cell measured 24.52 M inside the session against 43.73 M
   before it; the median per-cell CV was 7.97%, and 114 of 144 cells were above 5%. The session runner then gained a
   Windows power-event check.
4. **Stability check while charging** (`gate-charging-20260915T101637Z`): median 43.04 M, CV 3.48%, failed on two
   repetitions at 39.4 M and 37.4 M.
5. **Unattended sequence after charging finished.** After 52 minutes idle the check passed (`gate-before-session1-20260915T112901Z`:
   44.39 M, CV 1.81%). The session that followed never started because of a line-ending bug in the local runner
   scripts. The next two checks failed:
   - `gate-before-session1-retry-20260915T112938Z`: CV 9.33%. Repetitions 1-9 at 41.3-43.5 M, then 8 consecutive
     repetitions (about 6 seconds) at 33.6-34.4 M, then recovery to 40.0-43.4 M.
   - `gate-before-session1-retry-20260915T113100Z`: CV 5.17%. Repetitions 1-7 at 43.1-44.7 M, then a step at
     repetition 8 to 37.8-40.2 M for the rest of the run.
   - Diagnostics with one-second power samples (`diag-after-*`): after an initial 42-44 M, a plateau around 38.7 M with
     single-repetition dips to 34.8-36.1 M. Windows processor performance moved from 305% to 215% and back to 272%.
   The chip holds about 44 M for at most tens of seconds from cold, then throttles about 10-13% within seconds of
   sustained load, with further short dips of up to about 20%.
6. **Run-order confound found and fixed.** Reviewing the design against those measurements showed that the original
   matrix ran all single-thread cells before all multi-thread cells (described above), so a declining CPU would have
   distorted the scaling curve. Added `--lru_threads` and per-repetition timestamps, both verified before use.
7. **Reduced matrix on a warm chip.** 54 distinct cells (48 at 10% capacity across all designs, thread counts and
   workloads, plus 1-thread cells at every capacity), each session preheated, checked with CV at most 4.5% and the first 10
   repetitions within 3% of the last 20, and each group of cells bracketed by check runs. Session 1 was invalid: mains
   power dropped for 5 minutes 10 seconds with nobody at the machine. Throughput followed the power source exactly:
   38-42 M on mains, 21.6-23.7 M on battery. The stability check had passed on battery at 22.67 M, and one group was
   discarded twice when mains power returned partway through it.
8. **Battery power, deliberately** (`battery-test-20260915T192803Z`). On battery the platform power limit keeps the
   chip below its thermal limit, so a 10-minute test tracked throughput against charge from 95.2% to 87.6%. By the
   drift rule set before the test it failed: fitted change across the range -3.75% for the 1-thread cell and -5.76%
   for an 8-thread v3 cell, both faster at lower charge. The change sat in the first minute; excluding it, the fitted
   changes were +0.18% and -1.18%. More decisive was what a successful run would have shown: 8-thread v3 at about 6.9 M
   against 1-thread v2 at about 23 M, with 8-thread repetition-to-repetition CV of 5.1-16.4%.

Throughput measurement on this machine was abandoned at that point. A clean run would have produced mostly
indistinguishable comparisons and a scaling curve describing a 15 W laptop's power and thermal envelope rather than
the designs. The harness, its run-order fix and its stability checks are kept so the matrix can be run on hardware
that holds its frequency.
