# Tests

## What is here

| File | What it tests |
|---|---|
| `test_correctness.hpp` | The shared suite, written once as GoogleTest typed-parameterized tests: 10 contract tests and 6 strict-order tests |
| `test_lru_v1.cpp` | Both suites against `LruCache` |
| `test_lru_v2.cpp` | Both suites against `IntrusiveLru`, plus regression tests for its bucket arithmetic |
| `test_lru_v3.cpp` | The contract suite against `ShardedLru` at 1, 2, 7, 16 and 32 shards; strict order at 1 shard; per-shard ordering, capacity split, clamping and a 20,000-operation comparison against `IntrusiveLru` with one shard |
| `test_concurrent.cpp` | 8 threads, mixed get/put/visit, v1, v2 and v3 with 16 shards; checks size bounds and that every value read is one that was written |
| `test_allocations.cpp` | Replaces global `operator new` and counts steady-state allocations, with v1 as the control |
| `factories.hpp` | One adapter per design, so the shared suites construct caches without knowing which design they test |
| `mutation/` | The mutation campaign: 28 planted bugs, each declaring the tests it must break, and the runner that checks it still does |

### Why two suites

The **contract suite** holds for every design, sharded or not: round trip, misses do not insert, updates do not grow,
size never exceeds capacity, capacity 0 stores nothing, move-only values work, no value copies, and every key is
retrievable straight after its `put`. It uses a capacity of 64 so any shard count up to 32 leaves each shard at least
two slots.

The **strict-order suite** checks exact global LRU eviction. A sharded cache cannot promise that: it evicts the least
recently used entry of the key's shard, not of the whole cache. So v3 runs the strict-order suite with one shard, and
its multi-shard ordering is covered by per-shard tests instead. Passing the identical contract suite is the evidence
that the three designs are interchangeable where it matters.

## Running

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j && ctest --test-dir build --output-on-failure

# Sanitizers (one per build directory). TSan is given fewer stress operations because it is several times slower.
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++ \
    -DLRU_SANITIZER=thread -DLRU_CONCURRENT_OPS=250000
cmake --build build-tsan -j && ctest --test-dir build-tsan --output-on-failure
```

A build has 104 tests (103 with a sanitizer, where the allocation test is not built: sanitizer runtimes supply their own
`operator new`). CI runs gcc and clang in Debug and Release, ASan, UBSan and TSan, and a benchmark smoke job on every
push. On kernels with high mmap randomisation, clang's ASan and TSan crash at startup; CI lowers
`vm.mmap_rnd_bits` to 28, and locally `setarch x86_64 -R` works without changing system settings.

## Mutation testing

A test suite that has never been seen to fail is not evidence. `mutation/` plants 28 known bugs in the cache headers
and checks that the tests named for that behaviour actually fail:

```bash
tests/mutation/run.sh
```

Each mutation is a declaration: the file it changes, the exact text it replaces, the build configuration it needs,
and the tests expected to fail. The runner copies the sources to a scratch directory outside the repository, applies
one mutation at a time, builds and runs only the affected tests, and compares what failed against what was declared.
A mutation that stops being caught, or one that breaks a test it should not, is reported as a discrepancy and makes
the run exit non-zero. It never touches the repository's own sources.

The committed run is [mutation/results/results.md](mutation/results/results.md), with the full per-test detail and a
machine-readable `results.json` beside it: **28 mutations, 0 discrepancies, 348 seconds** on gcc 11.4 and clang 14,
CMake 4.4.3. The tables below summarise it. Every test named runs as its own process with a timeout, so a crash is
attributed to the test that crashed.

### Recency and capacity, v1 (`LruCache.hpp`)

| Mutation | Tests that failed |
|---|---|
| `get()` looks the key up without splicing the entry to the front | EvictsLeastRecentlyUsedFirst, GetRefreshesRecency |
| `visit()` does not splice (so neither does `get()`) | EvictsLeastRecentlyUsedFirst, GetRefreshesRecency, VisitRefreshesRecency |
| Updating an existing key does not splice | UpdateRefreshesRecencyWithoutGrowing only |
| A miss splices the least recently used entry to the front | MissDoesNotMutateOrder only |
| Eviction block removed (capacity 0 guard kept) | SizeNeverExceedsCapacity, MoveOnlyValueWorks, all 6 strict-order |
| Eviction block and capacity 0 guard removed | The above plus CapacityZeroStoresNothing |
| A full cache drops new keys instead of evicting | JustInsertedKeyIsAlwaysRetrievable, all 6 strict-order |

Every strict-order test except one failed on at least one recency mutation. `CapacityOneKeepsOnlyMostRecent` survived
all four: with one slot, recency cannot change which entry is kept. It stays, with a comment that it covers the
capacity-1 edge case only.

### The same bugs on v2, and what v3 inherits (`IntrusiveLru.hpp`)

v3 is built from v2 shards, so every mutation here is run against the v2 and the v3 suites together.

| Mutation | v2 tests that failed | v3 tests that failed |
|---|---|---|
| `get()` without move to front | 2 strict-order | 2 strict-order at 1 shard, 2 per-shard |
| `visit()` without move to front | 3 strict-order | 3 strict-order at 1 shard, 3 per-shard |
| Update without move to front | UpdateRefreshesRecency | the same at 1 shard, and per shard |
| A miss moves the LRU entry to the front | MissDoesNotMutateOrder | the same at 1 shard, and per shard |
| A full cache drops new keys | JustInserted, all 6 strict-order, bucket round trip | JustInserted at all 5 shard counts, all 6 strict-order, 5 of 6 per-shard |
| Slot bound off by one (`<=` for `<`) | SizeNeverExceeds, MoveOnly, all 6 strict-order, bucket round trip | SizeNeverExceeds and MoveOnly at all 5 shard counts, all 6 strict-order, all 6 per-shard, capacity sum |
| Capacity 0 guard removed | CapacityZeroStoresNothing | CapacityZeroStoresNothing at all 5 shard counts, capacity sum |
| Bucket minimum 1 instead of 2 | KeepTheShiftDefined, bucket round trip, CapacityZero, CapacityOne | CapacityZero at all 5 shard counts, CapacityOne at 1 shard, capacity sum |

The four recency mutations fail exactly the same tests on v2 as on v1. v1's unbounded-growth mutation cannot be
written for v2, whose arena holds exactly `capacity` entries; the nearest real bug is the off-by-one slot bound, which
writes one entry too many and overwrites the sentinel node. No mutation failed a test on v1 but not on v2.

`EvictionInOneShardLeavesOtherShardsUntouched` is the one per-shard test that survives the drop-new-keys mutation: an
insert that is dropped does not evict a bystander, which is exactly what that test watches for.

The last two rows fail by crashing rather than by asserting: without the capacity 0 guard, `put()` on an empty arena
evicts the sentinel and reads outside the node array, and all 7 failures are segfaults. The report records the reason
ctest gives for every failure, so a crash is never read as a diagnosis.

### v3 only (`ShardedLru.hpp`)

| Mutation | Tests that failed |
|---|---|
| Capacity split drops the remainder | ShardCapacitiesSumToTheRequestedCapacity only |
| Capacity split rounds every shard up | Capacity sum; SizeNeverExceeds and MoveOnly at 7 shards |
| Shard count not clamped to capacity | ShardCountIsClampedToCapacity only |
| `visit()` routes on a different hash from `put()` | All 6 per-shard tests; MoveOnly and NoValueCopies at 7, 16 and 32 shards |

This is why v3 has its own tests. Dropping the remainder and skipping the clamp pass the entire contract suite, and
routing `visit()` on a different hash passes it at 1 and 2 shards, where both routes happen to agree for the keys the
suite uses. Only the capacity split that rounds up is visible to the contract suite, and only at 7 shards, the one
instantiated shard count that leaves a remainder for a capacity of 64.

### The gap mutation testing found

An earlier campaign (suite as of commit `4feccc5`) ran the drop-new-keys mutation and it failed all 6 strict-order
tests and **no contract test**. That mattered because v3 runs the strict-order suite with one shard only: on a real
sharded cache the bug would have gone unseen.

`JustInsertedKeyIsAlwaysRetrievable` closes it (commit `85393df`). It inserts 640 distinct keys into a cache of 64 and
reads each one back immediately. Eviction always makes room before an insert, so this holds for a sharded cache too;
before adding it, it was checked against a simulated sharded cache built from independent v1 caches at 1, 4, 7, 16, 32
and 64 shards, and passed at every shard count on correct code. It is now the contract test that catches the mutation
on v1, on v2, and on v3 at all five shard counts.

The bucket arithmetic has its own regression test for the same reason. Lowering the bucket minimum from 2 to 1 gives a
shift of 64 in `bucket_of`, which is undefined behaviour: it showed up only as segfaults, with nothing naming the
cause. `IntrusiveLruBuckets.SmallestCapacitiesKeepTheShiftDefined` now checks the arithmetic directly and fails with a
plain assertion.

### Corruption the size checks cannot see

The concurrency test stores records whose fields must agree with each other and with the key they are read under. An
update that writes its value into the most recently used entry instead of the entry it found:

| Design | Corrupt reads | Corrupt entries at rest | Size assertions |
|---|---|---|---|
| v1 | 36,400 | 329 | all passed |
| v2 | 36,411 | 309 | all passed |
| v3, 16 shards | 35,908 | 308 | all passed |

1,000,000 operations, 8 threads, Release. These counts depend on thread interleaving and differ between runs; earlier
campaigns on the same mutation recorded 35,801 to 37,176 corrupt reads, and the two reruns that produced this
table's figures fell in the same band. What does not vary is that the size assertions
stay silent: the runner checks for their failure text and reports its presence as a discrepancy, so "all passed" above
is a checked claim rather than an observation from one run.

## Sanitizers, each shown to fire

These are the sanitizer mutations in the same campaign, so the reports below are rechecked whenever it is rerun: each
one declares the text the sanitizer must print, and a run where it does not appear is a discrepancy.

**ThreadSanitizer** (clang, 250,000 operations), each failing only the designs that share the mutated code:
- v1, lock removed from `put()`: a data race on `std::list`'s element count, read by one thread in
  `if (entries_.size() == capacity_)` while another incremented it in `entries_.push_front(...)`.
- v2 and v3, lock removed from `IntrusiveLru::size()`: a race between `++size_` in `put()` and the read in `size()`.
  On v3 the read came through `ShardedLru::size()`, which sums shards while other threads write.
- v2 and v3, lock removed from `IntrusiveLru::put()`: a race inside `IntrusiveLru`, reported on both designs.
- v3, a plain statistics counter incremented in `ShardedLru::shard_for`: a race between two threads in `shard_for`,
  the kind of shared state a sharded design must not have. v1 and v2 pass, since only v3 selects a shard.

**AddressSanitizer:**
- v1, eviction that keeps a reference to a list node's key across `pop_back()`: `heap-use-after-free`, failing 10 of
  the 16 tests in the v1 suite and the v1 concurrency test. The 6 that pass are the ones that never evict.
- v2 and v3, the same ordering bug (entry destroyed before its key is read to unlink it): every test passes and ASan
  reports nothing. The arena's storage stays allocated for the cache's lifetime, so the read is inside a live block.
  This is a cost of the arena design, documented rather than worked around. The mutation stays in the campaign with
  "no failures and no report" as its declared expectation, so if that ever changes the run says so.

**UndefinedBehaviorSanitizer:**
- v2, bucket minimum lowered from 2 to 1: `shift exponent 64 is too large for 64-bit type` in `bucket_of` at capacities
  0 and 1, failing 11 tests. In a gcc 11 Debug build the same mutation fails the same 11 tests, but only
  `SmallestCapacitiesKeepTheShiftDefined` fails with a diagnosis; the other 10 crash. The campaign records those 10 as
  allowed to fail either way, because whether undefined behaviour crashes is not a property the tests can pin down.
