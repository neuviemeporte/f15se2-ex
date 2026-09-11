#!/usr/bin/env python3
"""Analyze game sources using CMake's compiler definitions and include paths."""

import argparse
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--tool", choices=["cppcheck", "clang"], default="cppcheck")
    parser.add_argument("--report-only", action="store_true",
                        help="Report cppcheck findings without failing; tool errors still fail")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build.resolve()
    commands = json.loads((build / "compile_commands.json").read_text())
    # Tests compile some sources a second time with different definitions.
    # Keep those configurations, but exclude tests and third-party source files.
    commands = [entry for entry in commands
                if (Path(entry["directory"]) / entry["file"]).resolve()
                .is_relative_to(root / "src")]
    if not commands:
        raise SystemExit("No game sources in the compilation database")
    report = build / "quality"
    report.mkdir(exist_ok=True)
    database = report / "compile_commands.json"
    database.write_text(json.dumps(commands, indent=2) + "\n")
    print(f"Checking {len(commands)} game compilation commands", flush=True)
    if args.tool == "clang":
        sources = sorted({str((Path(entry["directory"]) / entry["file"]).resolve())
                          for entry in commands})
        with (report / "clang.txt").open("w") as diagnostics:
            result = subprocess.run([
                "clang-check-18", "--analyze", "-p", str(report), *sources,
            ], cwd=root, stdout=diagnostics, stderr=subprocess.STDOUT)
        print(f"Clang diagnostics: {report / 'clang.txt'}")
        raise SystemExit(result.returncode)
    with (report / "cppcheck.xml").open("w") as diagnostics:
        result = subprocess.run([
            "cppcheck", f"--project={database}", "--enable=warning,performance",
            "--language=c++", "--std=c++20",
            "--inline-suppr", "--xml", "--xml-version=2", "--error-exitcode=1",
            "-j2",
        ], cwd=root, stderr=diagnostics)
    diagnostics = ET.parse(report / "cppcheck.xml").findall("./errors/error")
    for diagnostic in diagnostics:
        location = diagnostic.find("location")
        file = location.get("file", "") if location is not None else ""
        line = location.get("line", "") if location is not None else ""
        print(f"{file}:{line}: {diagnostic.get('id')}: {diagnostic.get('msg')}")
    tool_failed = result.returncode not in (0, 1) or any(
        item.get("id") in {"syntaxError", "internalError", "preprocessorErrorDirective"}
        for item in diagnostics)
    if tool_failed or (result.returncode and not diagnostics):
        raise SystemExit("Cppcheck did not complete successfully")
    print(f"Cppcheck: {len(diagnostics)} findings")
    if args.report_only:
        return
    raise SystemExit(result.returncode)


if __name__ == "__main__":
    main()
