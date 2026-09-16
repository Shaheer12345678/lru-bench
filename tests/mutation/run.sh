#!/usr/bin/env bash
# Entry point for the mutation campaign. Checks the prerequisites, then hands over to run_mutations.py.
#
# Every argument is passed straight through, so `run.sh --list` and `run.sh --only v1-miss-reorders` work.
# Nothing here writes inside the repository: the sources are copied to a scratch directory first.

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

missing=0
need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing prerequisite: $1 ($2)" >&2
        missing=1
    fi
}

need cmake "3.25 or newer, to configure the builds"
need python3 "3.8 or newer, to apply the mutations and compare the results against what each one declares"
need ctest "ships with CMake, and runs each test as its own process"

cxx="${CXX:-c++}"
if ! command -v "$cxx" >/dev/null 2>&1; then
    echo "missing prerequisite: $cxx (a C++20 compiler; set CXX to choose another)" >&2
    missing=1
fi

# The sanitizer mutations are the evidence that ASan, UBSan and TSan fire on a planted bug, so a missing
# sanitizer compiler is reported as a failure rather than quietly skipped.
sanitizer_cxx="${LRU_MUTATION_SANITIZER_CXX:-clang++}"
if ! command -v "$sanitizer_cxx" >/dev/null 2>&1; then
    echo "missing prerequisite: $sanitizer_cxx (needed for the sanitizer mutations; set" \
         "LRU_MUTATION_SANITIZER_CXX to choose another, or pass --config debug release to skip them)" >&2
    missing=1
fi

if [ "$missing" -ne 0 ]; then
    echo "" >&2
    echo "Install the tools above, or see tests/mutation/README.md for the full requirements." >&2
    exit 2
fi

if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)'; then
    echo "python3 is $(python3 -V 2>&1), but 3.8 or newer is required" >&2
    exit 2
fi

exec python3 "$here/run_mutations.py" "$@"
