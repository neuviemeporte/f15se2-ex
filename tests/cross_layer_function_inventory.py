#!/usr/bin/env python3
"""Inventory functions shared by the original binaries and both rewrites.

This is an audit/report tool, not a replacement for behavior tests.  It makes
the cross-layer scope explicit:

* original binary routines come from f15se2-re/map/*.map;
* rewrite functions come from each tree's C/C++ sources;
* original-binary behavior evidence comes from the kvikdos harness;
* rewrite evidence comes from tests referencing the same function names and, for
  f15se2-ex, the gcovr function-coverage audit.

The report intentionally prints remaining gaps instead of pretending that source
coverage of one tree proves original-binary equivalence.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from enum import Enum, IntEnum
from pathlib import Path


class ExitCode(IntEnum):
    OK = 0
    FAILURE = 1


class ReportLimit(IntEnum):
    SAMPLE_GAPS = 40


class Program(Enum):
    EGAME = "egame"
    START = "start"
    END = "end"


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_REPO = Path("/home/xor/games/f15se2-re")

# Map annotations that should not count toward an executable behavior cross-set.
EXCLUDED_ORIGINAL_ANNOTATIONS = {"ignore", "external", "duplicate", "detached"}
EXCLUDED_ORIGINAL_NAMES = {"padding", "setargv", "setenvp"}

# MS C runtime and compiler helper routines are present in the original EXEs,
# but they are not F-15 SE2 behavior.  The maps normally tag these as external;
# the explicit list keeps the audit denominator game-owned if a map annotation is
# missing or later normalized away.
MS_C_RUNTIME_NAMES = {
    "_brkctl",
    "_close",
    "_ctermsub",
    "_exit",
    "_fclose",
    "_fflush",
    "_filbuf",
    "_fopen",
    "_fread",
    "_fwrite",
    "_getch",
    "_inp",
    "_isatty",
    "_lseek",
    "_malloc",
    "_movedata",
    "_open",
    "_putch",
    "_rand",
    "_read",
    "_remove",
    "_srand",
    "_stackavail",
    "_strcmp",
    "_unlink",
    "_write",
    "abs",
    "close",
    "exit",
    "fclose",
    "fopen",
    "fread",
    "memcpy",
    "open",
    "rand",
    "read",
    "srand",
    "strcat",
    "strcpy",
    "strlen",
    "strupr",
    "write",
    "__aNldiv",
    "__aNlmul",
    "__aNlrem",
    "__aNNaldiv",
    "__amalloc",
    "__amallocbrk",
    "__amexpand",
    "__amlink",
    "__amsg_exit",
    "__cXENIXtoDOSmode",
    "__cinit",
    "__dosreturn",
    "__exit",
    "__FF_MSGBANNER",
    "__filbuf",
    "__flsbuf",
    "__freebuf",
    "__getbuf",
    "__getstream",
    "__maperror",
    "__NMSG_TEXT",
    "__NMSG_WRITE",
    "__nfree",
    "__nullcheck",
}

# Unknown library labels in the maps are clustered with the MS C runtime code.
MS_C_RUNTIME_PREFIXES = ("_unk_libc",)

# Explicit known source-name differences.  The left side is the original map
# name; values are the accepted source names in each rewrite tree.
SOURCE_ALIASES: dict[str, set[str]] = {
    "clampValue": {"clampValue", "egClampValue"},
    "forceRange": {"forceRange", "clampValue", "egClampValue"},
}

# Math/fixed-point functions where the goal requires edge-case behavior, not
# just link/line coverage.  Names are original map names.
MATH_EDGE_FUNCTIONS = {
    "fixedMulQ14",
    "cosine",
    "sine",
    "signedRatio16",
    "valueToAngle",
    "complementAngle",
    "isqrt",
    "clampRange",
    "forceRange",
    "clampValue",
    "rangeApprox",
    "computeBearing",
    "sinMul",
    "cosMul",
    "signOf",
    "hudSine",
    "hudPitchScale",
}


@dataclass(frozen=True)
class OriginalRoutine:
    program: Program
    name: str
    segment: str
    call_type: str
    start: int
    end: int
    annotations: frozenset[str]


def fail(message: str) -> int:
    print(f"failed: {message}", file=sys.stderr)
    return int(ExitCode.FAILURE)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--re-root", type=Path, default=DEFAULT_REPO)
    parser.add_argument(
        "--build-dir",
        type=Path,
        help="optional f15se2-ex coverage build directory used to read gcovr function counts",
    )
    parser.add_argument("--gcovr", default="gcovr")
    parser.add_argument(
        "--strict",
        action="store_true",
        help="fail if any same-name cross-set function lacks direct cross-layer behavior evidence",
    )
    return parser.parse_args()


def parse_original_maps(re_root: Path) -> list[OriginalRoutine]:
    routines: list[OriginalRoutine] = []
    pattern = re.compile(
        r"^(?P<name>[A-Za-z_][A-Za-z0-9_]*):\s+"
        r"(?P<segment>\S+)\s+"
        r"(?P<type>NEAR|FAR)\s+"
        r"(?P<start>[0-9a-fA-F]+)-(?P<end>[0-9a-fA-F]+)"
        r"(?P<rest>.*)$"
    )
    for program in Program:
        map_path = re_root / "map" / f"{program.value}.map"
        for line in map_path.read_text(errors="replace").splitlines():
            match = pattern.match(line.strip())
            if not match:
                continue
            rest = match.group("rest")
            annotations = frozenset(
                token
                for token in re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", rest)
                if not re.match(r"^[RU][0-9a-fA-F]+$", token)
            )
            routines.append(
                OriginalRoutine(
                    program=program,
                    name=match.group("name"),
                    segment=match.group("segment"),
                    call_type=match.group("type"),
                    start=int(match.group("start"), 16),
                    end=int(match.group("end"), 16),
                    annotations=annotations,
                )
            )
    return routines


def is_original_auditable(routine: OriginalRoutine) -> bool:
    if routine.name in EXCLUDED_ORIGINAL_NAMES:
        return False
    if is_ms_c_runtime(routine):
        return False
    return not (routine.annotations & EXCLUDED_ORIGINAL_ANNOTATIONS)


def is_ms_c_runtime(routine: OriginalRoutine) -> bool:
    return routine.name in MS_C_RUNTIME_NAMES or routine.name.startswith(MS_C_RUNTIME_PREFIXES)


def parse_source_functions(src_root: Path) -> dict[str, set[Path]]:
    functions: dict[str, set[Path]] = {}
    # This regex intentionally targets normal function definitions in the rewrite
    # sources. It skips declarations, calls, preprocessor lines, and control flow.
    pattern = re.compile(
        r"^\s*(?:static\s+|extern\s+|inline\s+|constexpr\s+|FAR\s+|CDECL\s+|NEAR\s+)*"
        r"(?:[A-Za-z_][A-Za-z0-9_:<>]*[\s\*&]+)+"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*(?:noexcept\s*)?\{"
    )
    excluded_names = {"if", "for", "while", "switch", "return"}
    for path in sorted(src_root.rglob("*")):
        if path.suffix not in {".c", ".cpp", ".h", ".hpp"}:
            continue
        if any(part in {"compat64"} for part in path.parts):
            continue
        for line in path.read_text(errors="replace").splitlines():
            match = pattern.match(line)
            if not match:
                continue
            name = match.group("name")
            if name in excluded_names:
                continue
            functions.setdefault(name, set()).add(path)
    return functions


def source_has(functions: dict[str, set[Path]], original_name: str) -> bool:
    return any(name in functions for name in SOURCE_ALIASES.get(original_name, {original_name}))


def evidence_has(functions: set[str], original_name: str) -> bool:
    return original_name in functions or any(alias in functions for alias in SOURCE_ALIASES.get(original_name, set()))


def parse_original_harness_functions(re_root: Path) -> set[str]:
    harness = re_root / "mzretools" / "original_binary_function_harness.py"
    text = harness.read_text()
    return set(re.findall(r'expect_(?:ax|ax_and_memory|ax_and_words)\(\s*"[^"]+",\s*"([^"]+)"', text, re.S))


def parse_re_original_compare_functions(re_root: Path) -> set[str]:
    test_path = re_root / "tests" / "original_fixed_math_compare_tests.cpp"
    if not test_path.exists():
        return set()
    return set(re.findall(r"\boriginal_([A-Za-z_][A-Za-z0-9_]*)\s*\(", test_path.read_text()))


def parse_ex_test_references() -> set[str]:
    functions: set[str] = set()
    for path in (ROOT / "tests").glob("*"):
        if path.suffix not in {".cpp", ".py"}:
            continue
        text = path.read_text(errors="replace")
        functions.update(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", text))
    return functions


def gcovr_covered_functions(build_dir: Path, gcovr: str) -> set[str]:
    object_dirs = sorted(path for path in (build_dir / "CMakeFiles").glob("*_tests.dir") if path.is_dir())
    if not object_dirs:
        return set()
    with tempfile.TemporaryDirectory(prefix="f15se2-cross-layer-gcovr-") as temp_dir:
        output = Path(temp_dir) / "coverage.json"
        cmd = [
            gcovr,
            "--merge-mode-functions=merge-use-line-min",
            "--root",
            str(ROOT),
            "--filter",
            str(ROOT / "src"),
            "--gcov-ignore-errors=no_working_dir_found",
            "--json",
            str(output),
        ]
        cmd.extend(map(str, object_dirs))
        subprocess.run(cmd, check=True)
        report = json.loads(output.read_text())
    covered: set[str] = set()
    for file_report in report.get("files", []):
        for function in file_report.get("functions", []):
            if int(function.get("execution_count", 0)) > 0:
                covered.add(function["name"].split("(", 1)[0])
    return covered


def print_sample(title: str, values: list[str]) -> None:
    print(f"{title}: {len(values)}")
    for value in values[: int(ReportLimit.SAMPLE_GAPS)]:
        print(f"  {value}")
    if len(values) > int(ReportLimit.SAMPLE_GAPS):
        print(f"  ... {len(values) - int(ReportLimit.SAMPLE_GAPS)} more")


def main() -> int:
    args = parse_args()
    re_root = args.re_root.resolve()
    original_routines = parse_original_maps(re_root)
    runtime_originals = [routine for routine in original_routines if is_ms_c_runtime(routine)]
    auditable_originals = [routine for routine in original_routines if is_original_auditable(routine)]
    original_names = {routine.name for routine in auditable_originals}
    ex_functions = parse_source_functions(ROOT / "src")
    re_functions = parse_source_functions(re_root / "src")

    cross_names = sorted(
        name for name in original_names if source_has(ex_functions, name) and source_has(re_functions, name)
    )
    original_only = sorted(name for name in original_names if name not in cross_names)
    ex_test_refs = parse_ex_test_references()
    original_harness = parse_original_harness_functions(re_root)
    re_compare = parse_re_original_compare_functions(re_root)
    ex_covered = gcovr_covered_functions(args.build_dir.resolve(), args.gcovr) if args.build_dir else set()

    directly_cross_tested = sorted(
        name
        for name in cross_names
        if name in original_harness
        and (name in re_compare or any(alias in re_compare for alias in SOURCE_ALIASES.get(name, set())))
        and (name in ex_test_refs or any(alias in ex_test_refs for alias in SOURCE_ALIASES.get(name, set())))
    )
    edge_math_missing = sorted(name for name in MATH_EDGE_FUNCTIONS if name in cross_names and name not in directly_cross_tested)
    no_original_binary_evidence = sorted(name for name in cross_names if name not in original_harness)
    no_re_compare_evidence = sorted(
        name
        for name in cross_names
        if name not in re_compare and not any(alias in re_compare for alias in SOURCE_ALIASES.get(name, set()))
    )

    print(f"original auditable routines: {len(auditable_originals)} ({len(original_names)} unique names)")
    print(
        "MS C runtime/compiler helper routines excluded: "
        f"{len(runtime_originals)} ({len({routine.name for routine in runtime_originals})} unique names)"
    )
    print(f"f15se2-ex source functions found: {len(ex_functions)}")
    print(f"f15se2-re source functions found: {len(re_functions)}")
    print(f"same-name/aliased original + re + ex cross-set: {len(cross_names)}")
    print(f"direct original-binary kvikdos behavior functions: {len(original_harness)}")
    print(f"direct f15se2-re original-vs-fixed compare functions: {len(re_compare)}")
    print(f"direct cross-layer behavior-tested functions: {len(directly_cross_tested)}")
    if ex_covered:
        covered_cross = sorted(name for name in cross_names if evidence_has(ex_covered, name))
        uncovered_cross = sorted(name for name in cross_names if not evidence_has(ex_covered, name))
        print(f"f15se2-ex gcovr-covered cross-set functions: {len(covered_cross)}/{len(cross_names)}")
        if uncovered_cross:
            print_sample("cross-set functions not seen in f15se2-ex gcovr function data", uncovered_cross)

    print_sample("cross-set functions without original-binary direct-call evidence", no_original_binary_evidence)
    print_sample("cross-set functions without f15se2-re compare evidence", no_re_compare_evidence)
    print_sample("auditable original names not found in both rewrites", original_only)
    if edge_math_missing:
        print_sample("math edge functions missing direct cross-layer evidence", edge_math_missing)
    else:
        print("math edge functions missing direct cross-layer evidence: 0")

    if args.strict and (no_original_binary_evidence or no_re_compare_evidence or edge_math_missing):
        return fail("cross-layer function evidence is incomplete")
    return int(ExitCode.OK)


if __name__ == "__main__":
    raise SystemExit(main())
