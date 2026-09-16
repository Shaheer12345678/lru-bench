# Mutation testing

A test suite that has never been seen to fail is not evidence. This directory plants known bugs in the cache headers
and checks that the tests named for that behaviour actually fail.

Each mutation is a declaration: which file it changes, the exact text it replaces, which build configuration it needs,
and which tests are expected to fail because of it. The runner applies it, builds, runs the affected tests, and
compares the failures it sees against the failures declared. A mutation that stops being caught, or one that starts
breaking tests it should not, is reported as a discrepancy and makes the run exit non-zero.

| File | What it is |
|---|---|
| `mutations.py` | The campaign as data: the build configurations, the test selections and the 28 mutations |
| `run_mutations.py` | The runner: copies the sources out of the repository, applies each mutation, builds, runs, compares |
| `run.sh` | Entry point; checks the prerequisites and passes its arguments through |
| `results/` | The report from the committed run |

Nothing here builds or edits anything inside the repository. The runner copies `CMakeLists.txt`, `include/` and
`tests/` into a scratch directory (by default a fresh one under the system temporary directory) and works only on
that copy. It refuses a `--work-dir` that lies inside the repository.

## Requirements

- `bash`
- `cmake` 3.25 or newer, and the `ctest` that ships with it
- A C++20 compiler for the ordinary builds: `c++` by default, or set `CXX`
- `clang++` for the sanitizer builds, or set `LRU_MUTATION_SANITIZER_CXX`. The clang installation needs its
  sanitizer runtimes (on Debian and Ubuntu these are in the `clang` package itself)
- `python3` 3.8 or newer, for the runner. It uses only the standard library

`run.sh` checks all of these before doing any work and names whatever is missing. Nothing else is needed: GoogleTest
is fetched by CMake, which means the first configuration needs network access.

On kernels that randomise more address bits than clang's sanitizer runtimes expect, sanitized binaries abort at
startup. The runner uses `setarch x86_64 -R` when it is available, and says so when it is not.

## Running

```bash
tests/mutation/run.sh                       # the whole campaign
tests/mutation/run.sh --list                # what it would run
tests/mutation/run.sh --only v1-miss-reorders v2-put-without-lock
tests/mutation/run.sh --config debug release   # skip the sanitizer builds
tests/mutation/run.sh --report-dir tests/mutation/results --work-dir ~/lru-mutation
```

The run prints one line per mutation and writes `results.md` and `results.json` to the report directory (by default
the scratch directory). Exit status is 0 when every mutation behaved as declared, 1 when any did not, and 2 when a
prerequisite is missing or the unmutated sources failed their own tests.

## How long it takes

About 5 to 6 minutes for all 28 mutations on a 6-core laptop (Intel i3-1215U, gcc 11 and clang 14); the run recorded
in `results/` took 348 seconds. Roughly half of that is the sanitizer configurations, where the thread sanitizer
mutations each run the concurrency stress test three times over. `--config debug` covers 19 of the 28 mutations in
about 3 minutes.

Each of the five build configurations is configured once and then rebuilt incrementally per mutation, and only the
test executables a mutation can affect are built. The first configuration clones GoogleTest; the other four reuse
that clone.

## Adding a mutation

Append an entry to `MUTATIONS` in `mutations.py`:

```python
{
    "id": "v1-miss-reorders",
    "file": "include/lru/LruCache.hpp",
    "description": "a lookup that misses moves the least recently used entry to the front",
    "config": "debug",              # debug, release, tsan, asan or ubsan
    "selection": "v1",              # which executables to build and which tests to run
    "replace": [(old_text, new_text)],
    "expect_fail": v1_strict("MissDoesNotMutateOrder"),
}
```

`replace` holds exact text, and the runner refuses to continue if a piece of it does not appear exactly once in the
file: that means the source has moved on and the mutation no longer describes it. Optional keys are `may_fail`, for
tests whose failure depends on undefined behaviour and so cannot be relied on either way, `expect_output` and
`expect_no_output` for text a run must or must not print, `capture` for regular expressions pulling numbers out of
the test output, and `note` for a sentence carried into the report.

The expectations name tests the way `ctest -N` lists them, with GoogleTest's type parameter shortened:
`V3/LruContractTest.SizeNeverExceedsCapacity[x7]`, `LruConcurrentTest.MixedOpsKeepSizeBoundedAndValuesIntact[v3x16]`.
Any name that no test in the selection matches is itself reported as a discrepancy, so a renamed test cannot leave a
mutation silently unchecked.
