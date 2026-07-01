#!/usr/bin/env python3
"""Audit source line coverage, allowing only documented structural gaps.

This is intentionally stricter than a percentage threshold.  It fails if any
rewrite-source line is uncovered unless that line is listed below with the
reason it is not reachable as an old-behavior test case in the current C port.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import tempfile
from enum import IntEnum
from itertools import groupby
from pathlib import Path


class ExitCode(IntEnum):
    OK = 0
    FAILURE = 1


ROOT = Path(__file__).resolve().parents[1]
SOURCE_FILTER = str(ROOT / "src")

# Exact remaining non-behavior line gaps.  Comments are part of the audit:
# removing a gap from here means either adding a behavior test or changing the
# translated source so the line no longer exists.
ALLOWED_MISSING_LINES: dict[str, set[int]] = {
    # process3dg() currently has no reachable -1 result through the translated
    # terrain loader, so this retry edge cannot be induced by valid test data.
    "src/eg3dproj.c": {102},

    # 628 and 724 are gcov-counted loop-closing braces.  1431-1432 require
    # >16-bit edge deltas, but the C globals feeding clipAndRasterizeEdge are
    # int16 and truncate before the overflow-halving branch can be reached.
    "src/eg3drast.c": {628, 724, 1431, 1432},

    # updateThreatTargeting() clears projectile ttl before the later
    # player-projectile ground-impact message condition tests ttl > threshold.
    "src/egcombat.c": {379, 380},

    # setupInstrumentLayoutFar() assigns g_halfScaleRender = 0 immediately before
    # checking whether it equals 1; the full-detail setup block is preserved for
    # fidelity but unreachable.
    "src/eghudr.c": set(range(105, 151)),

    # keyDispatch() assigns g_currentWeaponType = 1 immediately before this
    # defensive check, so the condition cannot be true.
    "src/egkeys.c": {149},

    # Label-only lines emitted from translated goto structure.
    "src/egtarget.c": {274},
    "src/egthreat.c": {305},

    # Native SDL failure/log fallback paths and the unsafe legacy low-memory
    # near-pointer fallback.  These are not old DOS behavior assertions.
    "src/gfx_impl.c": {41, 49, 57, 63, 133},

    # restart_40a8 is a label-only line.  escortMissionFlag is int16; assigning
    # 0xffff makes it -1, so both later escortMissionFlag == 0 / != -1 branches
    # are unreachable without changing source behavior.
    "src/stgen.c": {154, 184, 254, 255},
}


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


def ranges(nums: list[int]) -> str:
    parts: list[str] = []
    for _, group in groupby(enumerate(nums), lambda item: item[1] - item[0]):
        values = [line for _, line in group]
        parts.append(str(values[0]) if len(values) == 1 else f"{values[0]}-{values[-1]}")
    return ",".join(parts)


def run_gcovr(gcovr: str, build_dir: Path, output_json: Path) -> dict:
    cmd = [
        gcovr,
        "--root",
        str(ROOT),
        "--filter",
        SOURCE_FILTER,
        "--gcov-ignore-errors=no_working_dir_found",
        "--json",
        str(output_json),
        "--object-directory",
        str(build_dir),
    ]
    subprocess.run(cmd, check=True)
    return json.loads(output_json.read_text())


def uncovered_lines(report: dict) -> dict[str, set[int]]:
    uncovered: dict[str, set[int]] = {}
    for file_report in report.get("files", []):
        file_name = file_report["file"]
        missing = {
            int(line["line_number"])
            for line in file_report.get("lines", [])
            if int(line.get("count", 0)) == 0 and not line.get("gcovr/noncode")
        }
        if missing:
            uncovered[file_name] = missing
    return uncovered


def main() -> int:
    args = parse_args()
    build_dir = args.build_dir.resolve()
    if not build_dir.exists():
        return fail(f"build directory does not exist: {build_dir}")

    with tempfile.TemporaryDirectory(prefix="f15se2-line-coverage-") as temp_dir:
        report = run_gcovr(args.gcovr, build_dir, Path(temp_dir) / "source-lines.json")

    uncovered = uncovered_lines(report)
    unexpected: dict[str, set[int]] = {}
    for file_name, lines in uncovered.items():
        extra = lines - ALLOWED_MISSING_LINES.get(file_name, set())
        if extra:
            unexpected[file_name] = extra

    stale_allowlist: dict[str, set[int]] = {}
    for file_name, allowed in ALLOWED_MISSING_LINES.items():
        stale = allowed - uncovered.get(file_name, set())
        if stale:
            stale_allowlist[file_name] = stale

    covered = sum(
        1
        for file_report in report.get("files", [])
        for line in file_report.get("lines", [])
        if not line.get("gcovr/noncode") and int(line.get("count", 0)) > 0
    )
    total = sum(
        1
        for file_report in report.get("files", [])
        for line in file_report.get("lines", [])
        if not line.get("gcovr/noncode")
    )
    percent = (covered * 100.0 / total) if total else 0.0
    print(f"source line coverage: {covered}/{total} ({percent:.1f}%)")

    if unexpected:
        for file_name, lines in sorted(unexpected.items()):
            print(f"unexpected uncovered lines: {file_name}: {ranges(sorted(lines))}", file=sys.stderr)
        return fail("source line coverage has undocumented gaps")

    if stale_allowlist:
        for file_name, lines in sorted(stale_allowlist.items()):
            print(f"stale allowed-missing lines: {file_name}: {ranges(sorted(lines))}", file=sys.stderr)
        return fail("line coverage allowlist contains covered/nonexistent lines")

    for file_name, lines in sorted(uncovered.items()):
        print(f"allowed structural/native gap: {file_name}: {ranges(sorted(lines))}")
    return int(ExitCode.OK)


if __name__ == "__main__":
    raise SystemExit(main())
