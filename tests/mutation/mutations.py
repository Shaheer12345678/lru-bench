"""The mutation campaign, as data.

Each mutation plants one bug in one header and states which tests must fail because of it. The runner
(run_mutations.py) applies it to a copy of the sources outside the repository, builds, runs the selected tests, and
reports any difference between the failures it sees and the failures listed here. A mutation that stops being caught
shows up as a discrepancy instead of passing quietly.

Test names are written the way ctest lists them, with the GoogleTest type parameter shortened:
    V1/LruContractTest.SizeNeverExceedsCapacity              (v1 and v2 suites keep their prefix only)
    V3/LruContractTest.SizeNeverExceedsCapacity[x7]          (v3 suites carry the shard count)
    LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]
"""

# ---------------------------------------------------------------------------------------------------------------------
# Build configurations. Each is configured once in its own scratch copy and rebuilt incrementally per mutation.
# "cxx" uses the compiler given by --cxx (default: $CXX, then c++); "sanitizer" uses --sanitizer-cxx (default clang++).

CONFIGS = {
    "debug": {"compiler": "cxx", "cmake": ["-DCMAKE_BUILD_TYPE=Debug"], "env": {}},
    "release": {"compiler": "cxx", "cmake": ["-DCMAKE_BUILD_TYPE=Release"], "env": {}},
    "tsan": {
        "compiler": "sanitizer",
        "cmake": ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DLRU_SANITIZER=thread", "-DLRU_CONCURRENT_OPS=250000"],
        "env": {"TSAN_OPTIONS": "halt_on_error=1:second_deadlock_stack=1"},
    },
    "asan": {
        "compiler": "sanitizer",
        "cmake": ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DLRU_SANITIZER=address"],
        "env": {"ASAN_OPTIONS": "detect_leaks=1"},
    },
    "ubsan": {
        "compiler": "sanitizer",
        "cmake": ["-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DLRU_SANITIZER=undefined"],
        "env": {"UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"},
    },
}

# ---------------------------------------------------------------------------------------------------------------------
# Test selections: the executables to build and the ctest regex of tests to run. Every selection is first run on the
# unmodified copy, and must pass completely, before any mutation that uses it.

SELECTIONS = {
    "v1": {"targets": ["lru_test_v1"], "tests": r"^V1/"},
    "v1+concurrent": {"targets": ["lru_test_v1", "lru_test_concurrent"],
                      "tests": r"^(V1/|LruConcurrentTest.*V1Factory)"},
    "v2+v3": {"targets": ["lru_test_v2", "lru_test_v3"], "tests": r"^(V2/|V3/|IntrusiveLruBuckets|ShardedLru)"},
    "v3": {"targets": ["lru_test_v3"], "tests": r"^(V3/|ShardedLru)"},
    "concurrent": {"targets": ["lru_test_concurrent"], "tests": r"^LruConcurrentTest"},
}

# ---------------------------------------------------------------------------------------------------------------------
# Name helpers, so expectations read as sentences rather than long string lists.

SHARD_COUNTS = [1, 2, 7, 16, 32]
STRICT = ["EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency", "VisitRefreshesRecency", "MissDoesNotMutateOrder",
          "UpdateRefreshesRecencyWithoutGrowing", "CapacityOneKeepsOnlyMostRecent"]
PER_SHARD = ["EvictsLeastRecentlyUsedFirstWithinAShard", "GetRefreshesRecencyWithinAShard",
             "VisitRefreshesRecencyWithinAShard", "UpdateRefreshesRecencyWithinAShard",
             "MissDoesNotMutateOrderWithinAShard", "EvictionInOneShardLeavesOtherShardsUntouched"]
CONCURRENT = "LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact"


def v1_contract(*tests):
    return [f"V1/LruContractTest.{t}" for t in tests]


def v1_strict(*tests):
    return [f"V1/LruStrictOrderTest.{t}" for t in tests]


def v2_contract(*tests):
    return [f"V2/LruContractTest.{t}" for t in tests]


def v2_strict(*tests):
    return [f"V2/LruStrictOrderTest.{t}" for t in tests]


def v3_contract(tests, shards=SHARD_COUNTS):
    return [f"V3/LruContractTest.{t}[x{n}]" for t in tests for n in shards]


def v3_strict(*tests):
    return [f"V3/LruStrictOrderTest.{t}[x1]" for t in tests]


def per_shard(*tests):
    return [f"ShardedLruPerShardOrder.{t}" for t in tests]


def concurrent(*designs):
    return [f"{CONCURRENT}[{d}]" for d in designs]


# Text the mutations replace, shared where the same edit is applied to more than one design.
GET_BODY = """        std::optional<V> result;
        visit(key, [&result](const V& value) { result.emplace(value); });
        return result;
"""
LOCKED_PUT = """    void put(K key, V value) {
        std::lock_guard lock(mutex_);
"""
UNLOCKED_PUT = """    void put(K key, V value) {
"""
V1_EVICTION = """        if (entries_.size() == capacity_) {
            index_.erase(entries_.back().key);
            entries_.pop_back();
        }
"""

# Captures the value-integrity counters from the concurrency test's failure messages.
CORRUPTION_CAPTURE = {
    "corrupt_reads": r"corrupt_reads\.load\(\)\s+Which is: (\d+)",
    "corrupt_at_rest": r"corrupt_at_rest\s+Which is: (\d+)",
}
# GoogleTest prints these fragments only when the corresponding size assertion fails, so their absence is the
# evidence that the size checks stayed silent while the value checksums caught the bug.
SIZE_ASSERTIONS = ["size_violations.load()", "never reached shard", "cache.capacity()", "\n    reachable\n"]

UB_CRASH = "undefined behaviour: whether it crashes depends on compiler, optimiser and platform"

# ---------------------------------------------------------------------------------------------------------------------
# The mutations.

MUTATIONS = [
    # ---- v1 LruCache: recency --------------------------------------------------------------------------------------
    {
        "id": "v1-get-does-not-refresh",
        "file": "include/lru/LruCache.hpp",
        "description": "get() finds the entry but does not move it to the front of the recency list",
        "config": "debug", "selection": "v1",
        "replace": [(GET_BODY, """        std::lock_guard lock(mutex_);
        auto found = index_.find(key);
        if (found == index_.end()) {
            return std::nullopt;
        }
        return found->second->value;
""")],
        "expect_fail": v1_strict("EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency"),
    },
    {
        "id": "v1-visit-does-not-refresh",
        "file": "include/lru/LruCache.hpp",
        "description": "visit() does not move the entry to the front; get() is built on visit(), so neither refreshes",
        "config": "debug", "selection": "v1",
        "replace": [("        entries_.splice(entries_.begin(), entries_, found->second);\n        std::invoke",
                     "        std::invoke")],
        "expect_fail": v1_strict("EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency", "VisitRefreshesRecency"),
    },
    {
        "id": "v1-update-does-not-refresh",
        "file": "include/lru/LruCache.hpp",
        "description": "put() on an existing key replaces the value but does not move the entry to the front",
        "config": "debug", "selection": "v1",
        "replace": [("            found->second->value = std::move(value);\n"
                     "            entries_.splice(entries_.begin(), entries_, found->second);\n",
                     "            found->second->value = std::move(value);\n")],
        "expect_fail": v1_strict("UpdateRefreshesRecencyWithoutGrowing"),
    },
    {
        "id": "v1-miss-reorders",
        "file": "include/lru/LruCache.hpp",
        "description": "a lookup that misses moves the least recently used entry to the front",
        "config": "debug", "selection": "v1",
        "replace": [("        if (found == index_.end()) {\n            return false;\n",
                     "        if (found == index_.end()) {\n"
                     "            if (!entries_.empty()) {\n"
                     "                entries_.splice(entries_.begin(), entries_, std::prev(entries_.end()));\n"
                     "            }\n"
                     "            return false;\n")],
        "expect_fail": v1_strict("MissDoesNotMutateOrder"),
    },
    # ---- v1 LruCache: capacity -------------------------------------------------------------------------------------
    {
        "id": "v1-never-evicts",
        "file": "include/lru/LruCache.hpp",
        "description": "put() never evicts, so the cache grows past capacity (the capacity 0 guard is kept)",
        "config": "debug", "selection": "v1",
        "replace": [(V1_EVICTION, "")],
        "expect_fail": v1_contract("SizeNeverExceedsCapacity", "MoveOnlyValueWorks") + v1_strict(*STRICT),
    },
    {
        "id": "v1-ignores-capacity",
        "file": "include/lru/LruCache.hpp",
        "description": "put() never evicts and the capacity 0 guard is removed",
        "config": "debug", "selection": "v1",
        "replace": [(V1_EVICTION, ""), ("        if (capacity_ == 0) {\n            return;\n        }\n", "")],
        "expect_fail": v1_contract("SizeNeverExceedsCapacity", "MoveOnlyValueWorks", "CapacityZeroStoresNothing")
        + v1_strict(*STRICT),
    },
    {
        "id": "v1-full-cache-drops-new-keys",
        "file": "include/lru/LruCache.hpp",
        "description": "a full cache returns from put() instead of evicting, silently dropping the new key",
        "config": "debug", "selection": "v1",
        "replace": [(V1_EVICTION, "        if (entries_.size() == capacity_) {\n            return;\n        }\n")],
        "expect_fail": v1_contract("JustInsertedKeyIsAlwaysRetrievable") + v1_strict(*STRICT),
        "note": "The size-bound contract tests cannot see this bug; JustInsertedKeyIsAlwaysRetrievable exists for it.",
    },
    # ---- v1 LruCache: concurrency, memory safety, corruption -------------------------------------------------------
    {
        "id": "v1-put-without-lock",
        "file": "include/lru/LruCache.hpp",
        "description": "put() runs without taking the mutex",
        "config": "tsan", "selection": "concurrent",
        "replace": [(LOCKED_PUT, UNLOCKED_PUT)],
        "expect_fail": concurrent("v1"),
        "expect_output": ["WARNING: ThreadSanitizer: data race", "LruCache<"],
    },
    {
        "id": "v1-use-after-free-on-eviction",
        "file": "include/lru/LruCache.hpp",
        "description": "eviction keeps a reference to the list node's key, pops the node, then uses the reference",
        "config": "asan", "selection": "v1+concurrent",
        "replace": [("            index_.erase(entries_.back().key);\n            entries_.pop_back();\n",
                     "            const K& victim_key = entries_.back().key;\n"
                     "            entries_.pop_back();\n"
                     "            index_.erase(victim_key);\n")],
        "expect_fail": v1_contract("SizeNeverExceedsCapacity", "JustInsertedKeyIsAlwaysRetrievable", "MoveOnlyValueWorks",
                                   "NoValueCopiesOnPutUpdateVisitOrEviction")
        + v1_strict(*STRICT) + concurrent("v1"),
        "expect_output": ["ERROR: AddressSanitizer: heap-use-after-free"],
        "note": "Every test that evicts fails; tests that never evict pass.",
    },
    {
        "id": "v1-update-writes-wrong-entry",
        "file": "include/lru/LruCache.hpp",
        "description": "an update writes the new value into the most recently used entry instead of the entry found",
        "config": "release", "selection": "concurrent",
        "replace": [("            found->second->value = std::move(value);\n",
                     "            entries_.front().value = std::move(value);\n")],
        "expect_fail": concurrent("v1"),
        "capture": CORRUPTION_CAPTURE,
        "expect_no_output": SIZE_ASSERTIONS,
        "note": "Only the value checksum can see this: every size assertion still passes. Counts vary per run.",
    },
    # ---- v2 IntrusiveLru (v3 is built from v2 shards, so the v3 tests run too) --------------------------------------
    {
        "id": "v2-get-does-not-refresh",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "get() finds the entry but does not move it to the front",
        "config": "debug", "selection": "v2+v3",
        "replace": [(GET_BODY, """        std::lock_guard lock(mutex_);
        const std::uint32_t found = find(bucket_of(key), key);
        if (found == kNil) {
            return std::nullopt;
        }
        return entry(found)->value;
""")],
        "expect_fail": v2_strict("EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency")
        + v3_strict("EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency")
        + per_shard("EvictsLeastRecentlyUsedFirstWithinAShard", "GetRefreshesRecencyWithinAShard"),
    },
    {
        "id": "v2-visit-does-not-refresh",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "visit() does not move the entry to the front, so neither does get()",
        "config": "debug", "selection": "v2+v3",
        "replace": [("        move_to_front(found);\n        std::invoke", "        std::invoke")],
        "expect_fail": v2_strict("EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency", "VisitRefreshesRecency")
        + v3_strict("EvictsLeastRecentlyUsedFirst", "GetRefreshesRecency", "VisitRefreshesRecency")
        + per_shard("EvictsLeastRecentlyUsedFirstWithinAShard", "GetRefreshesRecencyWithinAShard",
                    "VisitRefreshesRecencyWithinAShard"),
    },
    {
        "id": "v2-update-does-not-refresh",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "put() on an existing key replaces the value but does not move the entry to the front",
        "config": "debug", "selection": "v2+v3",
        "replace": [("            entry(found)->value = std::move(value);\n            move_to_front(found);\n",
                     "            entry(found)->value = std::move(value);\n")],
        "expect_fail": v2_strict("UpdateRefreshesRecencyWithoutGrowing")
        + v3_strict("UpdateRefreshesRecencyWithoutGrowing") + per_shard("UpdateRefreshesRecencyWithinAShard"),
    },
    {
        "id": "v2-miss-reorders",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "a lookup that misses moves the least recently used entry to the front",
        "config": "debug", "selection": "v2+v3",
        "replace": [("        if (found == kNil) {\n            return false;\n",
                     "        if (found == kNil) {\n"
                     "            if (size_ > 0) {\n"
                     "                move_to_front(nodes_[sentinel_].prev);\n"
                     "            }\n"
                     "            return false;\n")],
        "expect_fail": v2_strict("MissDoesNotMutateOrder") + v3_strict("MissDoesNotMutateOrder")
        + per_shard("MissDoesNotMutateOrderWithinAShard"),
    },
    {
        "id": "v2-full-cache-drops-new-keys",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "a full cache returns from put() instead of evicting",
        "config": "debug", "selection": "v2+v3",
        "replace": [("        const std::uint32_t slot = acquire_slot();\n",
                     "        if (size_ == capacity_) {\n"
                     "            return;\n"
                     "        }\n"
                     "        const std::uint32_t slot = acquire_slot();\n")],
        "expect_fail": v2_contract("JustInsertedKeyIsAlwaysRetrievable") + v2_strict(*STRICT)
        + ["IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange"]
        + v3_contract(["JustInsertedKeyIsAlwaysRetrievable"]) + v3_strict(*STRICT) + per_shard(*PER_SHARD[:5]),
        "note": "EvictionInOneShardLeavesOtherShardsUntouched passes: a dropped insert does not evict a bystander.",
    },
    {
        "id": "v2-slot-bound-off-by-one",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "the never-used slot bound is <= instead of <, so a capacity + 1 entry overwrites the sentinel",
        "config": "debug", "selection": "v2+v3",
        "replace": [("        if (next_unused_ < capacity_) {\n", "        if (next_unused_ <= capacity_) {\n")],
        "expect_fail": v2_contract("SizeNeverExceedsCapacity", "MoveOnlyValueWorks") + v2_strict(*STRICT)
        + ["IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange"]
        + v3_contract(["SizeNeverExceedsCapacity", "MoveOnlyValueWorks"]) + v3_strict(*STRICT) + per_shard(*PER_SHARD)
        + ["ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity"],
    },
    {
        "id": "v2-capacity-zero-guard-removed",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "the capacity 0 guard is removed, so put() on an empty arena evicts the sentinel",
        "config": "debug", "selection": "v2+v3",
        "replace": [("        if (capacity_ == 0) {\n            return;\n        }\n\n"
                     "        const std::uint32_t slot = acquire_slot();\n",
                     "        const std::uint32_t slot = acquire_slot();\n")],
        "expect_fail": v2_contract("CapacityZeroStoresNothing") + v3_contract(["CapacityZeroStoresNothing"])
        + ["ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity"],
        "note": "These tests fail by crashing: the mutated code reads outside the node array.",
    },
    {
        "id": "v2-bucket-minimum-one",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "the bucket minimum is 1 instead of 2, so capacity 0 or 1 gives a shift of 64 in bucket_of",
        "config": "debug", "selection": "v2+v3",
        "replace": [("    return std::bit_ceil(std::max<std::size_t>(capacity, 2));\n",
                     "    return std::bit_ceil(std::max<std::size_t>(capacity, 1));\n")],
        "expect_fail": ["IntrusiveLruBuckets.SmallestCapacitiesKeepTheShiftDefined"],
        "may_fail": {
            name: UB_CRASH for name in (
                ["IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange"]
                + v2_contract("CapacityZeroStoresNothing") + v2_strict("CapacityOneKeepsOnlyMostRecent")
                + v3_contract(["CapacityZeroStoresNothing"]) + v3_strict("CapacityOneKeepsOnlyMostRecent")
                + ["ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity"])
        },
        "note": "The arithmetic test fails with an assertion in any build; the others hit the undefined shift.",
    },
    {
        "id": "v2-bucket-minimum-one-ubsan",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "the same bucket-minimum bug, built with UndefinedBehaviorSanitizer",
        "config": "ubsan", "selection": "v2+v3",
        "replace": [("    return std::bit_ceil(std::max<std::size_t>(capacity, 2));\n",
                     "    return std::bit_ceil(std::max<std::size_t>(capacity, 1));\n")],
        "expect_fail": ["IntrusiveLruBuckets.SmallestCapacitiesKeepTheShiftDefined",
                        "IntrusiveLruBuckets.SmallestCapacitiesRoundTripKeysAcrossTheHashRange"]
        + v2_contract("CapacityZeroStoresNothing") + v2_strict("CapacityOneKeepsOnlyMostRecent")
        + v3_contract(["CapacityZeroStoresNothing"]) + v3_strict("CapacityOneKeepsOnlyMostRecent")
        + ["ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity"],
        "expect_output": ["runtime error: shift exponent 64"],
    },
    {
        "id": "v2-size-without-lock",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "size() reads the entry count without taking the mutex",
        "config": "tsan", "selection": "concurrent",
        "replace": [("    std::size_t size() const {\n        std::lock_guard lock(mutex_);\n        return size_;\n",
                     "    std::size_t size() const {\n        return size_;\n")],
        "expect_fail": concurrent("v2", "v3x16"),
        "expect_output": ["WARNING: ThreadSanitizer: data race", "IntrusiveLru<"],
        "note": "On v3 the unlocked read is reached through ShardedLru::size(), which sums the shards.",
    },
    {
        "id": "v2-put-without-lock",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "put() runs without taking the mutex",
        "config": "tsan", "selection": "concurrent",
        "replace": [(LOCKED_PUT, UNLOCKED_PUT)],
        "expect_fail": concurrent("v2", "v3x16"),
        "expect_output": ["WARNING: ThreadSanitizer: data race", "IntrusiveLru<"],
    },
    {
        "id": "v2-update-writes-wrong-entry",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "an update writes the new value into the most recently used entry instead of the entry found",
        "config": "release", "selection": "concurrent",
        "replace": [("            entry(found)->value = std::move(value);\n",
                     "            entry(nodes_[sentinel_].next)->value = std::move(value);\n")],
        "expect_fail": concurrent("v2", "v3x16"),
        "capture": CORRUPTION_CAPTURE,
        "expect_no_output": SIZE_ASSERTIONS,
        "note": "Counts vary per run.",
    },
    {
        "id": "v2-use-after-destroy-on-eviction",
        "file": "include/lru/IntrusiveLru.hpp",
        "description": "eviction destroys the victim entry before reading its key to unlink it from its hash chain",
        "config": "asan", "selection": "v2+v3",
        "replace": [("        unlink_from_bucket(victim);\n        unlink(victim);\n        std::destroy_at(entry(victim));\n",
                     "        std::destroy_at(entry(victim));\n        unlink_from_bucket(victim);\n        unlink(victim);\n")],
        "expect_fail": [],
        "expect_no_output": ["AddressSanitizer"],
        "note": "Expected to go undetected, and recorded as a cost of the arena design: the arena's storage stays "
                "allocated for the cache's lifetime, so AddressSanitizer has no freed memory to report. v1's "
                "equivalent is caught (v1-use-after-free-on-eviction).",
    },
    # ---- v3 ShardedLru ---------------------------------------------------------------------------------------------
    {
        "id": "v3-split-drops-remainder",
        "file": "include/lru/ShardedLru.hpp",
        "description": "shard capacities are capacity / shards, losing the remainder",
        "config": "debug", "selection": "v3",
        "replace": [("    return capacity / shards + (index < capacity % shards ? 1 : 0);\n",
                     "    (void)index;\n    return capacity / shards;\n")],
        "expect_fail": ["ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity"],
        "note": "The contract suite cannot see this: it only bounds size from above.",
    },
    {
        "id": "v3-split-rounds-up",
        "file": "include/lru/ShardedLru.hpp",
        "description": "every shard gets capacity / shards rounded up, so the shards together exceed capacity",
        "config": "debug", "selection": "v3",
        "replace": [("    return capacity / shards + (index < capacity % shards ? 1 : 0);\n",
                     "    (void)index;\n    return capacity / shards + (capacity % shards != 0 ? 1 : 0);\n")],
        "expect_fail": ["ShardedLruShards.ShardCapacitiesSumToTheRequestedCapacity"]
        + v3_contract(["SizeNeverExceedsCapacity", "MoveOnlyValueWorks"], shards=[7]),
        "note": "Only 7 of the contract suite's shard counts leaves a remainder for capacity 64.",
    },
    {
        "id": "v3-shard-count-not-clamped",
        "file": "include/lru/ShardedLru.hpp",
        "description": "the shard count is not clamped to the capacity, allowing zero-capacity shards",
        "config": "debug", "selection": "v3",
        "replace": [("    return std::clamp<std::size_t>(requested, 1, std::max<std::size_t>(capacity, 1));\n",
                     "    (void)capacity;\n    return std::max<std::size_t>(requested, 1);\n")],
        "expect_fail": ["ShardedLruShards.ShardCountIsClampedToCapacity"],
        "note": "The contract suite cannot see this: its capacity of 64 never goes below the shard count.",
    },
    {
        "id": "v3-visit-uses-different-shard",
        "file": "include/lru/ShardedLru.hpp",
        "description": "visit() picks the shard from the unmixed hash, so it can look in a different shard from put()",
        "config": "debug", "selection": "v3",
        "replace": [("        return shard(key).lru.visit(key, std::forward<F>(fn));\n",
                     "        return shards_[static_cast<std::size_t>(hash_(key)) % shard_count_].lru.visit(key, "
                     "std::forward<F>(fn));\n")],
        "expect_fail": per_shard(*PER_SHARD)
        + v3_contract(["MoveOnlyValueWorks", "NoValueCopiesOnPutUpdateVisitOrEviction"], shards=[7, 16, 32]),
        "note": "At 1 and 2 shards both routes agree for the keys the contract suite uses, so it passes there.",
    },
    {
        "id": "v3-unsynchronised-selection-counter",
        "file": "include/lru/ShardedLru.hpp",
        "description": "shard_for() increments a plain counter shared by all shards",
        "config": "tsan", "selection": "concurrent",
        "replace": [("    std::size_t shard_for(const K& key) const {\n",
                     "    std::size_t shard_for(const K& key) const {\n        ++selections_;\n"),
                    ("    Shard* shards_ = nullptr;\n",
                     "    Shard* shards_ = nullptr;\n    mutable std::size_t selections_ = 0;\n")],
        "expect_fail": concurrent("v3x16"),
        "expect_output": ["WARNING: ThreadSanitizer: data race", "shard_for"],
    },
]
