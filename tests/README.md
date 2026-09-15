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

A test suite that has never been seen to fail is not evidence. Each mutation below was applied to a scratch copy of the
source outside the repository, the tests were built and run, and the failing tests recorded. Unless noted, each test
ran as its own process with a timeout, so a crash was attributed to the test that crashed.

The mutations are described precisely enough to reapply by hand. The logs of these runs were working notes and are not
part of the repository.

### Recency and capacity, v1 (suite as of commit `a291622`: 9 contract and 6 strict-order tests)

| Mutation in `LruCache.hpp` | Tests that failed |
|---|---|
| `get()` looks the key up without splicing the entry to the front | EvictsLeastRecentlyUsedFirst, GetRefreshesRecency |
| `visit()` does not splice (so neither does `get()`) | EvictsLeastRecentlyUsedFirst, GetRefreshesRecency, VisitRefreshesRecency |
| Updating an existing key does not splice | UpdateRefreshesRecencyWithoutGrowing only |
| A miss splices the least recently used entry to the front | MissDoesNotMutateOrder only |
| Eviction block removed (capacity 0 guard kept) | SizeNeverExceedsCapacity, MoveOnlyValueWorks, all 6 strict-order tests |
| Eviction block and capacity 0 guard removed | The above plus CapacityZeroStoresNothing |

Every strict-order test except one failed on at least one recency mutation. `CapacityOneKeepsOnlyMostRecent` survived
all four: with one slot, recency cannot change which entry is kept. It stays, with a comment that it covers the
capacity-1 edge case only.

### The same mutations on v2 (suite as of commit `4feccc5`)

The four recency mutations failed exactly the same tests on v2 as on v1. v1's unbounded-growth mutation cannot be
written for v2, whose arena holds exactly `capacity` entries; the nearest real bug, an off-by-one in the slot bound
that writes one extra entry into the sentinel node, failed the same 8 tests. No mutation failed a test on v1 but not
on v2.

One mutation exposed a gap: a full cache that **drops new keys instead of evicting** failed all 6 strict-order tests
and **no contract test**. The contract suite would have been blind to it on v3, which runs strict order only with one
shard.

### The gap closed (commit `85393df`)

`JustInsertedKeyIsAlwaysRetrievable` inserts 640 distinct keys into a cache of 64 and reads each one back immediately.
Eviction always makes room before an insert, so this holds for a sharded cache too. Before adding it, it was checked
against a simulated sharded cache (independent v1 caches) at 1, 4, 7, 16, 32 and 64 shards: it passed at every shard
count on correct code. The drop-new-keys mutation then failed it on v1 (`key 64 missing straight after put`), on v2,
and on the simulated sharded cache at every shard count, while every other contract test still passed.

### v2 and v3 (suite as of commit `555ef9a`)

| Mutation | v2 tests that failed | v3 tests that failed |
|---|---|---|
| `get()` without move to front | 2 strict-order | 2 strict-order at 1 shard, 2 per-shard |
| `visit()` without move to front | 3 strict-order | 3 strict-order at 1 shard, 3 per-shard |
| Update without move to front | UpdateRefreshesRecency | the same at 1 shard, and per shard |
| A miss moves the LRU entry to the front | MissDoesNotMutateOrder | the same at 1 shard, and per shard |
| A full cache drops new keys | JustInserted, all 6 strict-order, bucket round trip | JustInserted at all 5 shard counts, all 6 strict-order, 5 of 6 per-shard |
| Slot bound off by one | SizeNeverExceeds, MoveOnly, all strict-order, bucket round trip | SizeNeverExceeds and MoveOnly at all 5 shard counts, all strict-order, all per-shard, capacity sum; the allocation test hung |
| v3 capacity split drops the remainder | n/a | ShardCapacitiesSumToTheRequestedCapacity only |
| v3 capacity split rounds up | n/a | Capacity sum; SizeNeverExceeds and MoveOnly at 7 shards |
| v3 shard count not clamped to capacity | n/a | ShardCountIsClampedToCapacity only |
| v3 `visit()` routes on a different hash from `put()` | n/a | All 6 per-shard tests; MoveOnly and NoValueCopies at 7, 16 and 32 shards |

The v3-specific mutations show why v3 has its own tests: dropping the remainder and skipping the clamp pass the entire
contract suite, and routing `visit()` on a different hash passes it at 1 and 2 shards.

### Corruption the size checks cannot see

The concurrency test stores records whose fields must agree with each other and with the key they are read under. An
update that writes its value into the most recently used entry instead of the entry it found:

| Design | Corrupt reads | Corrupt entries at rest | Size assertions |
|---|---|---|---|
| v1 | 36,309 | 294 | all passed |
| v2 | 37,176 | 301 | all passed |
| v3, 16 shards | 35,801 | 309 | all passed |

(1,000,000 operations, 8 threads, Release. The counts depend on thread interleaving and differ between runs.)

## Sanitizers, each shown to fire

**ThreadSanitizer** (clang, 250,000 operations):
- v1, lock removed from `put()`: a data race on `std::list`'s element count, read by one thread in
  `if (entries_.size() == capacity_)` while another incremented it in `entries_.push_front(...)`.
- v3, a plain statistics counter incremented in `ShardedLru::shard_for`: a race between two threads in `shard_for`,
  the kind of shared state a sharded design must not have.
- v2 and v3, lock removed from `IntrusiveLru::size()`: a race between `++size_` in `put()` and the read in `size()`.
  On v3 the read came through `ShardedLru::size()`, which sums shards while other threads write.
- v2 and v3, lock removed from `IntrusiveLru::put()`: on v2 a race on `size_` between `put()` and `size()`; on v3 a
  race between two threads on `next_unused_` in `acquire_slot()`.

**AddressSanitizer:**
- v1, eviction that keeps a reference to a list node's key across `pop_back()`: `heap-use-after-free`, failing 10 of
  16 tests.
- v2 and v3, the same ordering bug (entry destroyed before its key is read to unlink it): every test passes and ASan
  reports nothing. The arena's storage stays allocated for the cache's lifetime, so the read is inside a live block.
  This is a cost of the arena design, documented rather than worked around.

**UndefinedBehaviorSanitizer:**
- v2, bucket minimum lowered from 2 to 1: `shift exponent 64 is too large for 64-bit type` in `bucket_of` at capacities
  0 and 1. In ordinary builds the same bug showed up only as segfaults, so the bucket arithmetic now lives in
  `lru::detail` functions and `IntrusiveLruBuckets.SmallestCapacitiesKeepTheShiftDefined` checks it directly; the
  mutation fails that test with a plain assertion in gcc Debug and Release, clang Release and UBSan builds.
