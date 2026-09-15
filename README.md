# lru-bench

Three thread-safe LRU caches in C++20, each built to remove a measured cost of the one before it, all verified by one
shared test suite.

[![CI](https://github.com/Shaheer12345678/lru-bench/actions/workflows/ci.yml/badge.svg)](https://github.com/Shaheer12345678/lru-bench/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)

**At a glance**
- **v1 to v2: 148,384 allocations to 0.** Same 200,000-operation workload on a full cache, counted by replacing global
  `operator new`, with v1 as the control that proves the counter works.
- **v2 to v3: sharding costs at most 0.0003 hit rate** (Zipfian keys, 10,000 entries, 64 shards). What it gives up is
  global LRU order, and the test suite is split in two because of that.
- **The tests were tested.** One suite instantiated against all three designs; mutation testing showing which planted
  bug each test catches; ASan, UBSan and TSan each shown to fire before being trusted.
- **No throughput numbers are published.** The benchmark harness is here and runs in CI, but a 15 W laptop could not
  hold frequency long enough to measure the designs rather than the laptop. [Why](results/README.md#why-no-throughput-numbers).

```cpp
#include "lru/ShardedLru.hpp"

lru::ShardedLru<std::string, int> cache(/*capacity=*/100000, /*shard_count=*/16);
cache.put("answer", 42);
if (std::optional<int> hit = cache.get("answer")) { /* copy taken under the shard lock */ }
cache.visit("answer", [](const int& value) { /* runs under the lock; works for move-only values */ });
```

## Why this exists

"Implement an LRU cache" usually ends at a hash map and a linked list behind a mutex. This project takes that
design and improves it in steps. Each step is justified by a number, not by intuition, and each has to pass the same
behavioural contract as the one before it. The goal is to show what each change buys, what it costs, and how you
know the faster design is still correct.

## Quickstart

Requires CMake 3.25 or newer and a C++20 compiler (tested with GCC 11 and 13, Clang 14 and 18). GoogleTest and Google
Benchmark are fetched by CMake.

```bash
git clone https://github.com/Shaheer12345678/lru-bench.git && cd lru-bench
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

The library is header only: add the repository with `add_subdirectory` and link `lru::lru`.

## Architecture

All three satisfy one concept, `lru::LruCacheLike` (`include/lru/Policy.hpp`): `put`, `get`, `visit`, `size`,
`capacity`. Reads go through `visit`, which runs a callback under the lock, because returning a reference would
dangle as soon as another thread evicted the entry.

| | Design | What it bought (measured) | What it costs |
|---|---|---|---|
| **v1** `LruCache` | `std::list` plus `std::unordered_map` of list iterators, one mutex | The baseline | Two heap allocations per new key (a 32-byte list node and a 24-byte map node): 148,384 allocations in the 200,000-operation test |
| **v2** `IntrusiveLru` | Preallocated node arena, 32-bit indices, intrusive recency list and hash chains, one mutex | **0** steady-state allocations. At 500,000 entries, 18,097,288 bytes requested in total against v1's 32,162,056 when full | The whole arena is requested at construction, used or not. ASan cannot see use-after-destroy inside the arena (see Testing) |
| **v3** `ShardedLru` | N independent v2 shards, each cache-line aligned with its own mutex; shard chosen by a remixed hash | Still 0 allocations; hit rate within 0.0003 of exact LRU. By design, threads on different shards take different locks; the throughput effect is not measured here | **Global LRU order.** Eviction removes the least recent entry of the key's shard, and shards fill unevenly: 500,000 distinct inserts leave 499,146 entries |

v3's hit-rate cost is small because each shard still holds hundreds of entries, and per-shard LRU approximates global
LRU closely at that size. Its throughput benefit is the part this repository does not quantify (see Results).

Full figures, inputs and seeds are in [results/README.md](results/README.md).

## Testing

104 tests per build; CI runs gcc and clang in Debug and Release, ASan, UBSan and TSan, and a benchmark smoke job on
every push. The full record is in [tests/README.md](tests/README.md).

**One suite, three designs.** `tests/test_correctness.hpp` is written once and instantiated for every design. It is two
suites because sharding breaks one of them:
- The **contract suite** (10 tests) must hold for any design: size bounds, update semantics, capacity 0, move-only
  values, no copies, and every key retrievable right after its `put`. v3 runs it at 1, 2, 7, 16 and 32 shards.
- The **strict-order suite** (6 tests) checks exact global LRU eviction. v3 runs it with one shard only, because with
  more it is supposed to fail, and its multi-shard ordering has per-shard tests instead.

**Mutation testing.** Planted bugs, and which tests caught them:
- Each recency bug is caught by the test named for that behaviour: a `get` that does not refresh recency fails
  `GetRefreshesRecency`, a miss that reorders fails `MissDoesNotMutateOrder`, and so on, identically on all three designs.
- It found a real gap. A full cache that silently drops new keys instead of evicting passed the entire contract suite.
  `JustInsertedKeyIsAlwaysRetrievable` closes it; it was checked against a simulated sharded cache at 1 to 64 shards
  first, so it cannot fail a correct sharded design.
- Three v3-only bugs (a capacity split that loses the remainder, a missing shard-count clamp, `visit` routing on a
  different hash) were each caught by a v3-specific test, and the first two pass the whole contract suite.

**Corruption that size checks miss.** The concurrency test stores records that must agree with the key they are read
under. An update written to the wrong entry produced **36,309 corrupt reads** on v1 (37,176 on v2, 35,801 on v3) while
every size assertion passed.

**Sanitizers, each shown to fire on a planted bug first:**
- **TSan:** removing v1's lock in `put` gave a race on `std::list`'s element count between the capacity check and
  `push_front`; a statistics counter in v3's `shard_for` gave a race between two shard-selecting threads; an unlocked
  `size()` raced with `put` through `ShardedLru::size()`.
- **ASan:** a use-after-free on eviction in v1 failed 10 of 16 tests. The same ordering bug in v2 and v3 passes every
  test unseen, because the arena's memory stays allocated. That blind spot is documented as a cost of the arena
  design, not worked around.
- **UBSan:** lowering v2's bucket minimum to 1 gave `shift exponent 64` in `bucket_of`. In normal builds it only
  segfaulted, so the arithmetic now has a direct regression test that fails with an assertion.

## Results

Published results are the ones that do not depend on the machine: hit rate by shard count, exact-LRU agreement,
allocation counts and memory footprint, each reproducible from fixed seeds and identical between gcc and clang builds.
v1, v2 and v3 with one shard produce **identical hit counts on all 12 workloads**, which CI rechecks on every push.

Throughput is not published. Measurement on the only available machine (Intel i3-1215U, 15 W) showed throughput stepping
down about 10% within seconds of sustained load and halving on battery. A clean run would have described the laptop,
not the designs. The benchmarking attempts, and what each one found, are written up in
[results/README.md](results/README.md).

One lesson from that work applies to any benchmark: Google Benchmark's random interleaving only protects comparisons
between benchmarks in the same process. The first version of this harness ran single-threaded and multi-threaded cells
in separate processes, so on a throttling CPU the scaling curve would have recorded run order. The harness now takes
`--lru_threads=1,2,4,8` to interleave every thread count in one process, and records a wall-clock timestamp for each
repetition so run order can be checked.

## Roadmap

- Run the throughput matrix on hardware that holds its frequency; the harness, stability checks and run-order fix are ready.
- Hash each key once in v3; it is currently hashed for shard selection and again inside the shard.
- A pybind11 binding benchmarked against Python's `functools.lru_cache`.

## License

MIT. See [LICENSE](LICENSE).
