#!/usr/bin/env python3
"""Runs the mutation campaign defined in mutations.py.

Nothing is built or edited inside the repository. The sources are copied to a scratch directory, each mutation is
applied to that copy, the affected tests are built and run there, and the failures observed are compared against the
failures the mutation declares. Any difference is reported as a discrepancy and makes the run exit non-zero, so a
mutation that stops being caught is visible rather than silent.

Invoked by run.sh, which checks the prerequisites first. Run `run.sh --help` for the options.
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ElementTree
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mutations as catalogue  # noqa: E402  (needs the path entry above)

REPO_ROOT = Path(__file__).resolve().parents[2]
COPIED = ["CMakeLists.txt", "include", "tests"]

# GoogleTest appends the type parameter to every typed test name. Shortening it here keeps the expectations in
# mutations.py readable and makes them independent of how a compiler spells "16ul".
TYPE_SUFFIX = re.compile(r"<lru_test::(V1|V2|V3)Factory(?:<(\d+)[ulUL]*>)?>$")


def canonical_name(raw):
    match = TYPE_SUFFIX.search(raw)
    if match is None:
        return raw
    base, design, shards = raw[: match.start()], match.group(1), match.group(2)
    if base.startswith("LruConcurrentTest"):
        return f"{base}[{'v3x' + shards if design == 'V3' else design.lower()}]"
    if design == "V3" and shards is not None:
        return f"{base}[x{shards}]"
    return base


def fail(message):
    # Flushing first keeps the progress lines and this message in order when output is redirected to a file.
    sys.stdout.flush()
    print(f"error: {message}", file=sys.stderr)
    sys.exit(2)


def run(command, env=None):
    """Runs a command, returning (returncode, combined output)."""
    merged = dict(os.environ)
    merged.update(env or {})
    result = subprocess.run(command, env=merged, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, errors="replace")
    return result.returncode, result.stdout


def have_setarch():
    """setarch -R disables address space randomisation, which clang's ASan and TSan need on kernels that
    randomise more bits than the sanitizer runtimes were built for."""
    if shutil.which("setarch") is None:
        return False
    code, _ = run(["setarch", "x86_64", "-R", "true"])
    return code == 0


class Config:
    """One build directory: a compiler, a set of CMake options and the environment its tests run in."""

    def __init__(self, name, spec, work_dir, source_dir, args, setarch, gtest_source):
        self.name = name
        self.spec = spec
        self.source_dir = source_dir
        self.build_dir = work_dir / f"build-{name}"
        self.compiler = args.cxx if spec["compiler"] == "cxx" else args.sanitizer_cxx
        self.jobs = args.jobs
        self.env = dict(spec["env"])
        self.sanitized = spec["compiler"] == "sanitizer"
        self.prefix = ["setarch", "x86_64", "-R"] if (self.sanitized and setarch) else []
        self.gtest_source = gtest_source
        self.configured = False
        self.baselines = {}

    def configure(self):
        command = self.prefix + ["cmake", "-S", str(self.source_dir), "-B", str(self.build_dir),
                                 f"-DCMAKE_CXX_COMPILER={self.compiler}"] + list(self.spec["cmake"])
        # Reusing the first clone of GoogleTest keeps four of the five configurations off the network.
        if self.gtest_source is not None and self.gtest_source.is_dir():
            command.append(f"-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST={self.gtest_source}")
        code, output = run(command)
        if code != 0:
            fail(f"cmake configure failed for config '{self.name}' with {self.compiler}:\n{output}")
        self.configured = True

    def build(self, targets):
        # The prefix is needed here too, not only for ctest: gtest_discover_tests runs each executable at build
        # time to enumerate its tests, and a sanitized binary aborts at startup without it.
        code, output = run(self.prefix + ["cmake", "--build", str(self.build_dir), "-j", str(self.jobs),
                                          "--target", *targets])
        return code == 0, output

    def ctest(self, regex, junit_path, timeout):
        command = self.prefix + ["ctest", "--test-dir", str(self.build_dir), "-R", regex,
                                 "--output-junit", str(junit_path), "--timeout", str(timeout)]
        code, output = run(command, env=self.env)
        return code, output


def parse_junit(path):
    """Returns {canonical test name: {"status": ..., "output": ...}} from a ctest JUnit file."""
    results = {}
    tree = ElementTree.parse(path)
    for case in tree.iter("testcase"):
        name = canonical_name(case.get("name", ""))
        failure = case.find("failure")
        status = case.get("status", "")
        system_out = case.find("system-out")
        results[name] = {
            "failed": status != "run" or failure is not None,
            "reason": failure.get("message", "Failed") if failure is not None else "",
            "output": system_out.text or "" if system_out is not None else "",
        }
    return results


def apply_replacements(path, replacements):
    """Applies every (old, new) pair to the file, returning the original text so it can be restored."""
    original = path.read_text(encoding="utf-8")
    text = original
    for old, new in replacements:
        count = text.count(old)
        if count != 1:
            path.write_text(original, encoding="utf-8")
            fail(f"the text a mutation replaces in {path.name} occurs {count} times, not once:\n{old}\n"
                 "The mutation definition is out of step with the source and must be updated.")
        text = text.replace(old, new)
    path.write_text(text, encoding="utf-8")
    return original


def prepare_work_dir(work_dir):
    source_dir = work_dir / "source"
    if source_dir.exists():
        shutil.rmtree(source_dir)
    source_dir.mkdir(parents=True)
    for item in COPIED:
        origin = REPO_ROOT / item
        if origin.is_dir():
            # This directory is the campaign itself; the build has no use for it.
            shutil.copytree(origin, source_dir / item,
                            ignore=shutil.ignore_patterns("mutation", "__pycache__"))
        else:
            shutil.copy2(origin, source_dir / item)
    return source_dir


def check_outputs(mutation, results):
    """Checks the expect_output and expect_no_output strings against everything the tests printed."""
    combined = "\n".join(entry["output"] for entry in results.values())
    problems = []
    for needle in mutation.get("expect_output", []):
        if needle not in combined:
            problems.append(f"expected output not found: {needle!r}")
    for needle in mutation.get("expect_no_output", []):
        if needle in combined:
            problems.append(f"output that should not appear was printed: {needle!r}")
    return problems


def capture_values(mutation, results):
    """Pulls numbers out of the test output, keyed by the test that printed them, so a figure taken from a run
    with several designs in it cannot be attributed to the wrong design."""
    captured = {}
    for label, pattern in mutation.get("capture", {}).items():
        found = {}
        for name, entry in results.items():
            matches = re.findall(pattern, entry["output"])
            if matches:
                found[name] = [int(value) for value in matches]
        captured[label] = found
    return captured


def run_selection(config, selection, junit_path, timeout):
    ok, build_output = config.build(selection["targets"])
    if not ok:
        return None, build_output
    config.ctest(selection["tests"], junit_path, timeout)
    if not junit_path.exists():
        return None, "ctest produced no JUnit output"
    return parse_junit(junit_path), ""


def baseline(config, selection_name, selection, work_dir, timeout):
    """Runs the unmutated sources once per configuration and test selection. Everything must pass, otherwise the
    mutation results built on top of it would be meaningless."""
    if selection_name in config.baselines:
        return config.baselines[selection_name]
    junit = work_dir / f"baseline-{config.name}-{selection_name}.xml"
    results, error = run_selection(config, selection, junit, timeout)
    if results is None:
        fail(f"baseline build failed for config '{config.name}':\n{error}")
    failures = sorted(name for name, entry in results.items() if entry["failed"])
    if failures:
        fail(f"baseline run failed for config '{config.name}', selection '{selection_name}'. "
             f"The unmutated sources must pass before any mutation is applied. Failing tests:\n  "
             + "\n  ".join(failures))
    config.baselines[selection_name] = sorted(results)
    return config.baselines[selection_name]


def evaluate(mutation, results, baseline_names):
    expected = set(mutation.get("expect_fail", []))
    tolerated = set(mutation.get("may_fail", {}))
    actual = {name for name, entry in results.items() if entry["failed"]}

    unknown = sorted((expected | tolerated) - set(baseline_names))
    missed = sorted(expected - actual)
    unexpected = sorted(actual - expected - tolerated)
    tolerated_seen = sorted(actual & tolerated)

    problems = []
    if unknown:
        problems.append("expectations name tests that the selection does not run: " + ", ".join(unknown))
    if missed:
        problems.append("the mutation was not caught by: " + ", ".join(missed))
    if unexpected:
        problems.append("tests failed that were not expected to: " + ", ".join(unexpected))
    return {
        "expected": sorted(expected),
        "actual": sorted(actual),
        # ctest distinguishes an assertion from a crash or a timeout, which is worth keeping: a mutation whose
        # effect is undefined behaviour is caught very differently from one a test diagnoses.
        "reasons": {name: results[name]["reason"] for name in sorted(actual)},
        "missed": missed,
        "unexpected": unexpected,
        "tolerated_seen": tolerated_seen,
        "problems": problems,
    }


def markdown_report(records, meta):
    lines = ["# Mutation campaign results", "",
             f"- Run: {meta['timestamp']}",
             f"- Compiler: `{meta['cxx']}` ({meta['cxx_version']})",
             f"- Sanitizer compiler: `{meta['sanitizer_cxx']}` ({meta['sanitizer_cxx_version']})",
             f"- CMake: {meta['cmake_version']}",
             f"- Mutations run: {len(records)}; discrepancies: {meta['discrepancies']}",
             f"- Wall time: {meta['seconds']:.0f} s", "",
             "| Mutation | Config | Tests that failed | Verdict |", "|---|---|---|---|"]
    for record in records:
        verdict = "as expected" if not record["problems"] else "DISCREPANCY"
        actual = record["actual"]
        cell = f"{len(actual)} test(s)" if len(actual) > 6 else (", ".join(actual) if actual else "none")
        lines.append(f"| `{record['id']}` | {record['config']} | {cell} | {verdict} |")
    lines.append("")
    for record in records:
        lines.append(f"## `{record['id']}`")
        lines.append("")
        lines.append(record["description"])
        lines.append("")
        if record["note"]:
            lines.append(f"Note: {record['note']}")
            lines.append("")
        lines.append(f"Failed ({len(record['actual'])}): " + (", ".join(record["actual"]) or "none"))
        # Anything other than a plain assertion failure is named, so a crash is never mistaken for a diagnosis.
        crashes = {name: why for name, why in record["reasons"].items() if why not in ("", "Failed")}
        if crashes:
            lines.append("")
            for name, why in crashes.items():
                lines.append(f"{name}: {why}")
        if record["tolerated_seen"]:
            lines.append("")
            lines.append("Failed, and allowed either way: " + ", ".join(record["tolerated_seen"]))
        if record["captured"]:
            lines.append("")
            for label, per_test in record["captured"].items():
                for name, values in per_test.items():
                    lines.append(f"{label} in {name}: " + ", ".join(str(value) for value in values))
                if not per_test:
                    lines.append(f"{label}: no test printed it")
        if record["problems"]:
            lines.append("")
            for problem in record["problems"]:
                lines.append(f"DISCREPANCY: {problem}")
        lines.append("")
    return "\n".join(lines)


def tool_version(command):
    code, output = run(command)
    return output.strip().splitlines()[0] if code == 0 and output.strip() else "unknown"


def main():
    parser = argparse.ArgumentParser(description="Run the LRU cache mutation campaign.")
    parser.add_argument("--work-dir", help="scratch directory, created if absent; must be outside the repository")
    parser.add_argument("--report-dir", help="where to write results.json and results.md (default: the work directory)")
    parser.add_argument("--cxx", default=os.environ.get("CXX") or "c++", help="compiler for the non-sanitizer builds")
    parser.add_argument("--sanitizer-cxx", default=os.environ.get("LRU_MUTATION_SANITIZER_CXX") or "clang++",
                        help="compiler for the sanitizer builds")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="parallel build jobs")
    parser.add_argument("--timeout", type=int, default=600, help="per-test timeout in seconds")
    parser.add_argument("--only", nargs="+", metavar="ID", help="run only these mutations")
    parser.add_argument("--config", nargs="+", metavar="NAME", help="run only mutations in these configurations")
    parser.add_argument("--list", action="store_true", help="list the mutations and exit")
    parser.add_argument("--keep-work-dir", action="store_true", help="keep the scratch directory after the run")
    args = parser.parse_args()

    selected = catalogue.MUTATIONS
    if args.only:
        known = {mutation["id"] for mutation in catalogue.MUTATIONS}
        unknown = [name for name in args.only if name not in known]
        if unknown:
            fail("unknown mutation id(s): " + ", ".join(unknown))
        selected = [mutation for mutation in selected if mutation["id"] in args.only]
    if args.config:
        selected = [mutation for mutation in selected if mutation["config"] in args.config]
    if not selected:
        fail("no mutations selected")

    if args.list:
        for mutation in selected:
            print(f"{mutation['id']:<36} {mutation['config']:<8} {mutation['description']}")
        return 0

    if args.work_dir:
        work_dir = Path(args.work_dir).resolve()
        if work_dir == REPO_ROOT or REPO_ROOT in work_dir.parents:
            fail(f"the work directory must be outside the repository, and {work_dir} is inside {REPO_ROOT}")
        work_dir.mkdir(parents=True, exist_ok=True)
        temporary = False
    else:
        work_dir = Path(tempfile.mkdtemp(prefix="lru-mutation-"))
        temporary = True
    report_dir = Path(args.report_dir).resolve() if args.report_dir else work_dir
    report_dir.mkdir(parents=True, exist_ok=True)

    print(f"work directory: {work_dir}", flush=True)
    source_dir = prepare_work_dir(work_dir)
    setarch = have_setarch()
    if not setarch:
        print("note: setarch -R is unavailable; on kernels with high mmap randomisation the sanitizer builds "
              "may abort at startup")

    # Configurations are run in a fixed order and each is configured at most once, since configuring costs more
    # than the incremental rebuild a single mutation needs.
    order = [name for name in catalogue.CONFIGS if any(m["config"] == name for m in selected)]
    gtest_source = work_dir / "googletest-src"
    configs = {}
    for name in order:
        configs[name] = Config(name, catalogue.CONFIGS[name], work_dir, source_dir, args, setarch,
                               gtest_source if gtest_source.is_dir() else None)
        configs[name].configure()
        # The first configuration to run clones GoogleTest; keep that clone for the rest.
        if not gtest_source.is_dir():
            cloned = configs[name].build_dir / "_deps" / "googletest-src"
            if cloned.is_dir():
                shutil.copytree(cloned, gtest_source)

    started = time.time()
    records = []
    discrepancies = 0
    for name in order:
        config = configs[name]
        for mutation in [m for m in selected if m["config"] == name]:
            selection_name = mutation["selection"]
            selection = catalogue.SELECTIONS[selection_name]
            baseline_names = baseline(config, selection_name, selection, work_dir, args.timeout)

            target = source_dir / mutation["file"]
            original = apply_replacements(target, mutation["replace"])
            try:
                junit = work_dir / f"mutation-{mutation['id']}.xml"
                results, error = run_selection(config, selection, junit, args.timeout)
            finally:
                target.write_text(original, encoding="utf-8")

            if results is None:
                record = {"id": mutation["id"], "config": name, "description": mutation["description"],
                          "note": mutation.get("note", ""), "expected": sorted(mutation.get("expect_fail", [])),
                          "actual": [], "reasons": {}, "missed": [], "unexpected": [], "tolerated_seen": [],
                          "captured": {},
                          "problems": [f"the mutated sources did not build or run: {error.strip()[-2000:]}"]}
            else:
                verdict = evaluate(mutation, results, baseline_names)
                verdict["problems"] += check_outputs(mutation, results)
                record = {"id": mutation["id"], "config": name, "description": mutation["description"],
                          "note": mutation.get("note", ""), "captured": capture_values(mutation, results), **verdict}
            records.append(record)
            if record["problems"]:
                discrepancies += 1
            status = "DISCREPANCY" if record["problems"] else "ok"
            print(f"[{len(records):>2}/{len(selected)}] {mutation['id']:<36} {name:<8} "
                  f"{len(record['actual']):>3} failed  {status}", flush=True)
            for problem in record["problems"]:
                print(f"    {problem}")

    seconds = time.time() - started
    meta = {
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "cxx": args.cxx, "cxx_version": tool_version([args.cxx, "--version"]),
        "sanitizer_cxx": args.sanitizer_cxx,
        "sanitizer_cxx_version": tool_version([args.sanitizer_cxx, "--version"]),
        "cmake_version": tool_version(["cmake", "--version"]),
        "setarch": setarch, "seconds": seconds, "discrepancies": discrepancies,
        "mutations_run": len(records), "mutations_defined": len(catalogue.MUTATIONS),
    }
    (report_dir / "results.json").write_text(json.dumps({"meta": meta, "mutations": records}, indent=2) + "\n",
                                             encoding="utf-8")
    (report_dir / "results.md").write_text(markdown_report(records, meta) + "\n", encoding="utf-8")

    print(f"\n{len(records)} mutation(s) in {seconds / 60:.1f} min; {discrepancies} discrepancy(ies)")
    print(f"reports: {report_dir / 'results.md'}")
    if temporary and not args.keep_work_dir and discrepancies == 0:
        shutil.rmtree(work_dir, ignore_errors=True)
    return 1 if discrepancies else 0


if __name__ == "__main__":
    sys.exit(main())
