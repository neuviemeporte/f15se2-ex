#!/usr/bin/env python3
"""Audit source function coverage from unit-test object directories.

The audit compares every instrumented rewrite-source function in the build tree
against the functions covered by C/C++ unit-test object files.  The only allowed
missing function is src/f15.c:main: that copy belongs to the interactive process
entry point, while the same launcher behavior is exercised by
launcher_runtime_behavior_tests with main renamed to f15_program_main.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from enum import IntEnum
from pathlib import Path


class ExitCode(IntEnum):
    OK = 0
    FAILURE = 1


class CoverageThreshold(IntEnum):
    # The goal for this audit is exact function coverage of rewrite sources.
    REQUIRED_PERCENT = 100


ROOT = Path(__file__).resolve().parents[1]

# Only C/C++ test executables produce gcov object directories.  The Python
# original_binary_function_harness validates DOS binary behavior separately.
TEST_OBJECT_DIR_GLOB = "*_tests.dir"

# Limit the report to rewrite sources; CMake compiler-id probes and vendored SDL
# objects are irrelevant to F-15 rewrite coverage.
SOURCE_FILTER = str(ROOT / "src")

# Interactive executable wrapper.  Its behavior is covered through the renamed
# f15_program_main symbol in launcher_runtime_behavior_tests, so this one raw
# app-target entry point is allowed to be absent from unit-test object files.
ALLOWED_MISSING_TEST_FUNCTIONS = {("src/f15.c", "main")}

# Multiple test targets compile the same source file with different stubs.  This
# merge mode keeps duplicate function records by name while accepting line shifts
# caused by macro-renamed entry points such as f15_program_main.
FUNCTION_MERGE_MODE = "merge-use-line-min"


def fail(message: str) -> int:
    print(f"failed: {message}", file=sys.stderr)
    return int(ExitCode.FAILURE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "build_dir",
        type=Path,
        help="coverage-enabled CMake build directory after its tests have run",
    )
    parser.add_argument(
        "--gcovr",
        default="gcovr",
        help="gcovr executable name or path",
    )
    return parser.parse_args()


def test_object_dirs(build_dir: Path) -> list[Path]:
    cmake_files = build_dir / "CMakeFiles"
    return sorted(path for path in cmake_files.glob(TEST_OBJECT_DIR_GLOB) if path.is_dir())


def run_gcovr(gcovr: str, output_json: Path, object_paths: list[Path], object_directory: Path | None = None) -> dict:
    cmd = [
        gcovr,
        f"--merge-mode-functions={FUNCTION_MERGE_MODE}",
        "--root",
        str(ROOT),
        "--filter",
        SOURCE_FILTER,
        "--gcov-ignore-errors=no_working_dir_found",
        "--json",
        str(output_json),
    ]
    if object_directory is not None:
        cmd.extend(["--object-directory", str(object_directory)])
    cmd.extend(map(str, object_paths))
    subprocess.run(cmd, check=True)
    return json.loads(output_json.read_text())


def function_counts(report: dict) -> dict[tuple[str, str], int]:
    counts: dict[tuple[str, str], int] = {}
    for file_report in report.get("files", []):
        for function in file_report.get("functions", []):
            key = (file_report["file"], function["name"])
            counts[key] = max(counts.get(key, 0), int(function.get("execution_count", 0)))
    return counts


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    object_dirs = test_object_dirs(build_dir)
    if not object_dirs:
        return fail(f"no test object directories found under {build_dir / 'CMakeFiles'}")

    with tempfile.TemporaryDirectory(prefix="f15se2-function-coverage-") as temp_dir:
        temp_path = Path(temp_dir)
        all_report = run_gcovr(
            args.gcovr,
            temp_path / "all-source-functions.json",
            [],
            object_directory=build_dir,
        )
        test_report = run_gcovr(
            args.gcovr,
            temp_path / "test-covered-functions.json",
            object_dirs,
        )

    all_functions = set(function_counts(all_report))
    test_counts = function_counts(test_report)
    missing_from_tests = sorted(all_functions - set(test_counts) - ALLOWED_MISSING_TEST_FUNCTIONS)
    if missing_from_tests:
        for file_name, function_name in missing_from_tests:
            print(f"missing from unit-test objects: {file_name}: {function_name}", file=sys.stderr)
        return fail("not every source function is linked into the test coverage audit")

    auditable_functions = sorted(all_functions - ALLOWED_MISSING_TEST_FUNCTIONS)
    uncovered = [
        (file_name, function_name)
        for file_name, function_name in auditable_functions
        if test_counts.get((file_name, function_name), 0) <= 0
    ]

    total = len(auditable_functions)
    covered = total - len(uncovered)

    percent = (covered * 100.0 / total) if total else 0.0
    print(f"function coverage: {covered}/{total} ({percent:.1f}%)")
    for file_name, function_name in sorted(ALLOWED_MISSING_TEST_FUNCTIONS):
        print(f"allowed untested wrapper: {file_name}:{function_name}")
    if uncovered:
        for file_name, function_name in uncovered:
            print(f"uncovered: {file_name}: {function_name}", file=sys.stderr)
        return fail("function coverage is incomplete")
    if percent < int(CoverageThreshold.REQUIRED_PERCENT):
        return fail(f"function coverage below {int(CoverageThreshold.REQUIRED_PERCENT)}%")
    return int(ExitCode.OK)


if __name__ == "__main__":
    raise SystemExit(main())
