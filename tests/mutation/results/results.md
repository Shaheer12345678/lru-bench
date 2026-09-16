# Mutation campaign results

- Run: 2026-09-16T00:15:58Z
- Compiler: `c++` (c++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0)
- Sanitizer compiler: `clang++` (Ubuntu clang version 14.0.0-1ubuntu1.1)
- CMake: cmake version 4.4.3
- Mutations run: 28; discrepancies: 0
- Wall time: 348 s

| Mutation | Config | Tests that failed | Verdict |
|---|---|---|---|
| `v1-get-does-not-refresh` | debug | V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency | as expected |
| `v1-visit-does-not-refresh` | debug | V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency, V1/LruStrictOrderTest.VisitRefreshesRecency | as expected |
| `v1-update-does-not-refresh` | debug | V1/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing | as expected |
| `v1-miss-reorders` | debug | V1/LruStrictOrderTest.MissDoesNotMutateOrder | as expected |
| `v1-never-evicts` | debug | 8 test(s) | as expected |
| `v1-ignores-capacity` | debug | 9 test(s) | as expected |
| `v1-full-cache-drops-new-keys` | debug | 7 test(s) | as expected |
| `v2-get-does-not-refresh` | debug | ShardedLruPerShardOrder.EvictsLeastRecentlyUsedFirstWithinAShard, ShardedLruPerShardOrder.GetRefreshesRecencyWithinAShard, V2/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V2/LruStrictOrderTest.GetRefreshesRecency, V3/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst[x1], V3/LruStrictOrderTest.GetRefreshesRecency[x1] | as expected |
| `v2-visit-does-not-refresh` | debug | 9 test(s) | as expected |
| `v2-update-does-not-refresh` | debug | ShardedLruPerShardOrder.UpdateRefreshesRecencyWithinAShard, V2/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V3/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing[x1] | as expected |
| `v2-miss-reorders` | debug | ShardedLruPerShardOrder.MissDoesNotMutateOrderWithinAShard, V2/LruStrictOrderTest.MissDoesNotMutateOrder, V3/LruStrictOrderTest.MissDoesNotMutateOrder[x1] | as expected |
| `v2-full-cache-drops-new-keys` | debug | 24 test(s) | as expected |
| `v2-slot-bound-off-by-one` | debug | 32 test(s) | as expected |
| `v2-capacity-zero-guard-removed` | debug | 7 test(s) | as expected |
| `v2-bucket-minimum-one` | debug | 11 test(s) | as expected |
| `v3-split-drops-remainder` | debug | ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity | as expected |
| `v3-split-rounds-up` | debug | ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V3/LruContractTest.MoveOnlyValueWorks[x7], V3/LruContractTest.SizeNeverExceedsCapacity[x7] | as expected |
| `v3-shard-count-not-clamped` | debug | ShardedLruShards.ShardCountIsClampedToCapacity | as expected |
| `v3-visit-uses-different-shard` | debug | 12 test(s) | as expected |
| `v1-update-writes-wrong-entry` | release | LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1] | as expected |
| `v2-update-writes-wrong-entry` | release | LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2], LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16] | as expected |
| `v1-put-without-lock` | tsan | LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1] | as expected |
| `v2-size-without-lock` | tsan | LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2], LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16] | as expected |
| `v2-put-without-lock` | tsan | LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2], LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16] | as expected |
| `v3-unsynchronised-selection-counter` | tsan | LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16] | as expected |
| `v1-use-after-free-on-eviction` | asan | 11 test(s) | as expected |
| `v2-use-after-destroy-on-eviction` | asan | none | as expected |
| `v2-bucket-minimum-one-ubsan` | ubsan | 11 test(s) | as expected |

## `v1-get-does-not-refresh`

get() finds the entry but does not move it to the front of the recency list

Failed (2): V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency

## `v1-visit-does-not-refresh`

visit() does not move the entry to the front; get() is built on visit(), so neither refreshes

Failed (3): V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency, V1/LruStrictOrderTest.VisitRefreshesRecency

## `v1-update-does-not-refresh`

put() on an existing key replaces the value but does not move the entry to the front

Failed (1): V1/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing

## `v1-miss-reorders`

a lookup that misses moves the least recently used entry to the front

Failed (1): V1/LruStrictOrderTest.MissDoesNotMutateOrder

## `v1-never-evicts`

put() never evicts, so the cache grows past capacity (the capacity 0 guard is kept)

Failed (8): V1/LruContractTest.MoveOnlyValueWorks, V1/LruContractTest.SizeNeverExceedsCapacity, V1/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency, V1/LruStrictOrderTest.MissDoesNotMutateOrder, V1/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V1/LruStrictOrderTest.VisitRefreshesRecency

## `v1-ignores-capacity`

put() never evicts and the capacity 0 guard is removed

Failed (9): V1/LruContractTest.CapacityZeroStoresNothing, V1/LruContractTest.MoveOnlyValueWorks, V1/LruContractTest.SizeNeverExceedsCapacity, V1/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency, V1/LruStrictOrderTest.MissDoesNotMutateOrder, V1/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V1/LruStrictOrderTest.VisitRefreshesRecency

## `v1-full-cache-drops-new-keys`

a full cache returns from put() instead of evicting, silently dropping the new key

Note: The size-bound contract tests cannot see this bug; JustInsertedKeyIsAlwaysRetrievable exists for it.

Failed (7): V1/LruContractTest.JustInsertedKeyIsAlwaysRetrievable, V1/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency, V1/LruStrictOrderTest.MissDoesNotMutateOrder, V1/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V1/LruStrictOrderTest.VisitRefreshesRecency

## `v2-get-does-not-refresh`

get() finds the entry but does not move it to the front

Failed (6): ShardedLruPerShardOrder.EvictsLeastRecentlyUsedFirstWithinAShard, ShardedLruPerShardOrder.GetRefreshesRecencyWithinAShard, V2/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V2/LruStrictOrderTest.GetRefreshesRecency, V3/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst[x1], V3/LruStrictOrderTest.GetRefreshesRecency[x1]

## `v2-visit-does-not-refresh`

visit() does not move the entry to the front, so neither does get()

Failed (9): ShardedLruPerShardOrder.EvictsLeastRecentlyUsedFirstWithinAShard, ShardedLruPerShardOrder.GetRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.VisitRefreshesRecencyWithinAShard, V2/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V2/LruStrictOrderTest.GetRefreshesRecency, V2/LruStrictOrderTest.VisitRefreshesRecency, V3/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst[x1], V3/LruStrictOrderTest.GetRefreshesRecency[x1], V3/LruStrictOrderTest.VisitRefreshesRecency[x1]

## `v2-update-does-not-refresh`

put() on an existing key replaces the value but does not move the entry to the front

Failed (3): ShardedLruPerShardOrder.UpdateRefreshesRecencyWithinAShard, V2/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V3/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing[x1]

## `v2-miss-reorders`

a lookup that misses moves the least recently used entry to the front

Failed (3): ShardedLruPerShardOrder.MissDoesNotMutateOrderWithinAShard, V2/LruStrictOrderTest.MissDoesNotMutateOrder, V3/LruStrictOrderTest.MissDoesNotMutateOrder[x1]

## `v2-full-cache-drops-new-keys`

a full cache returns from put() instead of evicting

Note: EvictionInOneShardLeavesOtherShardsUntouched passes: a dropped insert does not evict a bystander.

Failed (24): IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange, ShardedLruPerShardOrder.EvictsLeastRecentlyUsedFirstWithinAShard, ShardedLruPerShardOrder.GetRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.MissDoesNotMutateOrderWithinAShard, ShardedLruPerShardOrder.UpdateRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.VisitRefreshesRecencyWithinAShard, V2/LruContractTest.JustInsertedKeyIsAlwaysRetrievable, V2/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V2/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V2/LruStrictOrderTest.GetRefreshesRecency, V2/LruStrictOrderTest.MissDoesNotMutateOrder, V2/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V2/LruStrictOrderTest.VisitRefreshesRecency, V3/LruContractTest.JustInsertedKeyIsAlwaysRetrievable[x16], V3/LruContractTest.JustInsertedKeyIsAlwaysRetrievable[x1], V3/LruContractTest.JustInsertedKeyIsAlwaysRetrievable[x2], V3/LruContractTest.JustInsertedKeyIsAlwaysRetrievable[x32], V3/LruContractTest.JustInsertedKeyIsAlwaysRetrievable[x7], V3/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent[x1], V3/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst[x1], V3/LruStrictOrderTest.GetRefreshesRecency[x1], V3/LruStrictOrderTest.MissDoesNotMutateOrder[x1], V3/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing[x1], V3/LruStrictOrderTest.VisitRefreshesRecency[x1]

## `v2-slot-bound-off-by-one`

the never-used slot bound is <= instead of <, so a capacity + 1 entry overwrites the sentinel

Failed (32): IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange, ShardedLruPerShardOrder.EvictionInOneShardLeavesOtherShardsUntouched, ShardedLruPerShardOrder.EvictsLeastRecentlyUsedFirstWithinAShard, ShardedLruPerShardOrder.GetRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.MissDoesNotMutateOrderWithinAShard, ShardedLruPerShardOrder.UpdateRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.VisitRefreshesRecencyWithinAShard, ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V2/LruContractTest.MoveOnlyValueWorks, V2/LruContractTest.SizeNeverExceedsCapacity, V2/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V2/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V2/LruStrictOrderTest.GetRefreshesRecency, V2/LruStrictOrderTest.MissDoesNotMutateOrder, V2/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V2/LruStrictOrderTest.VisitRefreshesRecency, V3/LruContractTest.MoveOnlyValueWorks[x16], V3/LruContractTest.MoveOnlyValueWorks[x1], V3/LruContractTest.MoveOnlyValueWorks[x2], V3/LruContractTest.MoveOnlyValueWorks[x32], V3/LruContractTest.MoveOnlyValueWorks[x7], V3/LruContractTest.SizeNeverExceedsCapacity[x16], V3/LruContractTest.SizeNeverExceedsCapacity[x1], V3/LruContractTest.SizeNeverExceedsCapacity[x2], V3/LruContractTest.SizeNeverExceedsCapacity[x32], V3/LruContractTest.SizeNeverExceedsCapacity[x7], V3/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent[x1], V3/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst[x1], V3/LruStrictOrderTest.GetRefreshesRecency[x1], V3/LruStrictOrderTest.MissDoesNotMutateOrder[x1], V3/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing[x1], V3/LruStrictOrderTest.VisitRefreshesRecency[x1]

## `v2-capacity-zero-guard-removed`

the capacity 0 guard is removed, so put() on an empty arena evicts the sentinel

Note: These tests fail by crashing: the mutated code reads outside the node array.

Failed (7): ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V2/LruContractTest.CapacityZeroStoresNothing, V3/LruContractTest.CapacityZeroStoresNothing[x16], V3/LruContractTest.CapacityZeroStoresNothing[x1], V3/LruContractTest.CapacityZeroStoresNothing[x2], V3/LruContractTest.CapacityZeroStoresNothing[x32], V3/LruContractTest.CapacityZeroStoresNothing[x7]

ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity: SEGFAULT
V2/LruContractTest.CapacityZeroStoresNothing: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x16]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x1]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x2]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x32]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x7]: SEGFAULT

## `v2-bucket-minimum-one`

the bucket minimum is 1 instead of 2, so capacity 0 or 1 gives a shift of 64 in bucket_of

Note: The arithmetic test fails with an assertion in any build; the others hit the undefined shift.

Failed (11): IntrusiveLruBuckets.SmallestCapacitiesKeepTheShiftDefined, IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange, ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V2/LruContractTest.CapacityZeroStoresNothing, V2/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V3/LruContractTest.CapacityZeroStoresNothing[x16], V3/LruContractTest.CapacityZeroStoresNothing[x1], V3/LruContractTest.CapacityZeroStoresNothing[x2], V3/LruContractTest.CapacityZeroStoresNothing[x32], V3/LruContractTest.CapacityZeroStoresNothing[x7], V3/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent[x1]

IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange: SEGFAULT
ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity: SEGFAULT
V2/LruContractTest.CapacityZeroStoresNothing: SEGFAULT
V2/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x16]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x1]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x2]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x32]: SEGFAULT
V3/LruContractTest.CapacityZeroStoresNothing[x7]: SEGFAULT
V3/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent[x1]: SEGFAULT

Failed, and allowed either way: IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange, ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V2/LruContractTest.CapacityZeroStoresNothing, V2/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V3/LruContractTest.CapacityZeroStoresNothing[x16], V3/LruContractTest.CapacityZeroStoresNothing[x1], V3/LruContractTest.CapacityZeroStoresNothing[x2], V3/LruContractTest.CapacityZeroStoresNothing[x32], V3/LruContractTest.CapacityZeroStoresNothing[x7], V3/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent[x1]

## `v3-split-drops-remainder`

shard capacities are capacity / shards, losing the remainder

Note: The contract suite cannot see this: it only bounds size from above.

Failed (1): ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity

## `v3-split-rounds-up`

every shard gets capacity / shards rounded up, so the shards together exceed capacity

Note: Only 7 of the contract suite's shard counts leaves a remainder for capacity 64.

Failed (3): ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V3/LruContractTest.MoveOnlyValueWorks[x7], V3/LruContractTest.SizeNeverExceedsCapacity[x7]

## `v3-shard-count-not-clamped`

the shard count is not clamped to the capacity, allowing zero-capacity shards

Note: The contract suite cannot see this: its capacity of 64 never goes below the shard count.

Failed (1): ShardedLruShards.ShardCountIsClampedToCapacity

## `v3-visit-uses-different-shard`

visit() picks the shard from the unmixed hash, so it can look in a different shard from put()

Note: At 1 and 2 shards both routes agree for the keys the contract suite uses, so it passes there.

Failed (12): ShardedLruPerShardOrder.EvictionInOneShardLeavesOtherShardsUntouched, ShardedLruPerShardOrder.EvictsLeastRecentlyUsedFirstWithinAShard, ShardedLruPerShardOrder.GetRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.MissDoesNotMutateOrderWithinAShard, ShardedLruPerShardOrder.UpdateRefreshesRecencyWithinAShard, ShardedLruPerShardOrder.VisitRefreshesRecencyWithinAShard, V3/LruContractTest.MoveOnlyValueWorks[x16], V3/LruContractTest.MoveOnlyValueWorks[x32], V3/LruContractTest.MoveOnlyValueWorks[x7], V3/LruContractTest.NoValueCopiesOnPutUpdateVisitOrEviction[x16], V3/LruContractTest.NoValueCopiesOnPutUpdateVisitOrEviction[x32], V3/LruContractTest.NoValueCopiesOnPutUpdateVisitOrEviction[x7]

## `v1-update-writes-wrong-entry`

an update writes the new value into the most recently used entry instead of the entry found

Note: Only the value checksum can see this: every size assertion still passes. Counts vary per run.

Failed (1): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1]

corrupt_reads in LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1]: 36400
corrupt_at_rest in LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1]: 329

## `v2-update-writes-wrong-entry`

an update writes the new value into the most recently used entry instead of the entry found

Note: Counts vary per run.

Failed (2): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2], LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]

corrupt_reads in LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2]: 36411
corrupt_reads in LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]: 35908
corrupt_at_rest in LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2]: 309
corrupt_at_rest in LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]: 308

## `v1-put-without-lock`

put() runs without taking the mutex

Failed (1): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1]

## `v2-size-without-lock`

size() reads the entry count without taking the mutex

Note: On v3 the unlocked read is reached through ShardedLru::size(), which sums the shards.

Failed (2): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2], LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]

## `v2-put-without-lock`

put() runs without taking the mutex

Failed (2): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v2], LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]

## `v3-unsynchronised-selection-counter`

shard_for() increments a plain counter shared by all shards

Failed (1): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]

## `v1-use-after-free-on-eviction`

eviction keeps a reference to the list node's key, pops the node, then uses the reference

Note: Every test that evicts fails; tests that never evict pass.

Failed (11): LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v1], V1/LruContractTest.JustInsertedKeyIsAlwaysRetrievable, V1/LruContractTest.MoveOnlyValueWorks, V1/LruContractTest.NoValueCopiesOnPutUpdateVisitOrEviction, V1/LruContractTest.SizeNeverExceedsCapacity, V1/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V1/LruStrictOrderTest.EvictsLeastRecentlyUsedFirst, V1/LruStrictOrderTest.GetRefreshesRecency, V1/LruStrictOrderTest.MissDoesNotMutateOrder, V1/LruStrictOrderTest.UpdateRefreshesRecencyWithoutGrowing, V1/LruStrictOrderTest.VisitRefreshesRecency

## `v2-use-after-destroy-on-eviction`

eviction destroys the victim entry before reading its key to unlink it from its hash chain

Note: Expected to go undetected, and recorded as a cost of the arena design: the arena's storage stays allocated for the cache's lifetime, so AddressSanitizer has no freed memory to report. v1's equivalent is caught (v1-use-after-free-on-eviction).

Failed (0): none

## `v2-bucket-minimum-one-ubsan`

the same bucket-minimum bug, built with UndefinedBehaviorSanitizer

Failed (11): IntrusiveLruBuckets.SmallestCapacitiesKeepTheShiftDefined, IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange, ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity, V2/LruContractTest.CapacityZeroStoresNothing, V2/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent, V3/LruContractTest.CapacityZeroStoresNothing[x16], V3/LruContractTest.CapacityZeroStoresNothing[x1], V3/LruContractTest.CapacityZeroStoresNothing[x2], V3/LruContractTest.CapacityZeroStoresNothing[x32], V3/LruContractTest.CapacityZeroStoresNothing[x7], V3/LruStrictOrderTest.CapacityOneKeepsOnlyMostRecent[x1]

