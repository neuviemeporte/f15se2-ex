#!/usr/bin/env python3
from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"failed: {message}")


def main() -> int:
    tool = Path(sys.argv[1])
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as f:
        f.write("F15SE2_BLACKBOX 7\n")
        f.write("seed 7\n")
        f.write("build_version test\n")
        f.write("mutable_file HallFame 0 -\n")
        f.write("phase 1 start\n")
        f.write("key 2 17 1f73\n")
        f.write("axes 3 64 128 128 192\n")
        f.write("rng_seed 4 7\n")
        f.write("rng 5 123\n")
        f.write("frame 6 0 1a2b3c4d\n")
        f.write("marker 6 view_change 0 132 0\n")
        f.write("state 6 2 camera abcdef01\n")
        f.write("render_hash 6 3 1 7 2 12345678\n")
        log_path = Path(f.name)
    try:
        text = subprocess.check_output(
            [sys.executable, str(tool), str(log_path), "--around-tick", "3", "--before", "1", "--after", "1"],
            text=True,
        )
        event_lines = [line for line in text.splitlines() if line.lstrip()[:1].isdigit()]
        require(any(" key " in line for line in event_lines) and
                any(" axes " in line for line in event_lines) and
                not any(" phase " in line for line in event_lines),
                "text window includes only events around the requested tick")

        raw = subprocess.check_output(
            [sys.executable, str(tool), str(log_path), "--from-tick", "4", "--to-tick", "5", "--json"],
            text=True,
        )
        data = json.loads(raw)
        require(data["seed"] == 7, "JSON output reports the log seed")
        require([event["kind"] for event in data["events"]] == ["rng_seed", "rng"],
                "JSON output preserves ordered events in the requested window")
        frame_text = subprocess.check_output(
            [sys.executable, str(tool), str(log_path), "--from-tick", "6", "--to-tick", "6"],
            text=True,
        )
        require("frame=0 hash=0x1a2b3c4d" in frame_text,
                "text output formats recorded frame hashes for divergence investigation")
        require("view_change(0,132,0)" in frame_text and
                "subsystem=camera hash=0xabcdef01" in frame_text and
                "objects=7 lines=2 hash=0x12345678" in frame_text,
                "text output formats diagnostic markers, subsystem hashes, and render hashes")
    finally:
        log_path.unlink(missing_ok=True)
    print("blackbox_inspect_tool_tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
