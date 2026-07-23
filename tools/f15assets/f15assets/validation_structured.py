"""Structured WLD/3DT/3DG JSON replacement validation."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Callable

from .terrain import build_3dg, build_3dt
from .validation_compare import compare_structured_rebuild
from .world import build_wld

__all__ = ["validate_structured_json_replacements"]


def _format_from_structured_json_path(path: Path) -> str | None:
    """Perform the format from structured json path asset-processing operation."""
    name = path.name.upper()
    for fmt in ("WLD", "3DT", "3DG"):
        if name.endswith(f".{fmt}.JSON"):
            return fmt
    return None


def _validate_structured_json_loadability(path: Path, fmt: str, builder: Callable[[dict], bytes]) -> tuple[int, int]:
    """Validate structured json loadability against runtime requirements."""
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
        rebuilt = builder(payload)
    except Exception as exc:
        print(f"{path}: {exc}", file=sys.stderr)
        return 1, 1
    if not rebuilt:
        print(f"{path}: rebuilt {fmt} is empty", file=sys.stderr)
        return 1, 1
    return 1, 0


def _legacy_structured_json_path(relative: Path, output_root: Path) -> Path:
    """Perform the legacy structured json path asset-processing operation."""
    legacy_base = output_root / relative
    return legacy_base.with_name(legacy_base.name + ".json")


def _first_existing_path(primary: Path, *fallbacks: Path) -> Path:
    """Return the first candidate path that exists."""
    for path in (primary, *fallbacks):
        if path.exists():
            return path
    return primary


def validate_structured_json_replacements(
    input_root: Path,
    output_root: Path,
    recursive: bool,
    require_all: bool,
    iter_asset_paths: Callable[[Path, bool], list[Path]],
    detect_format: Callable[[Path], str],
    structured_json_path: Callable[[Path, Path, Path, Path, str], Path],
    loadability_only: bool = False,
) -> tuple[int, int]:
    """Validate structured json replacements against runtime requirements."""
    builders = {
        "WLD": build_wld,
        "3DT": build_3dt,
        "3DG": build_3dg,
    }
    checked = 0
    failed = 0
    checked_json_paths: set[Path] = set()

    if input_root.exists():
        for src in iter_asset_paths(input_root, recursive=recursive):
            fmt = detect_format(src)
            if fmt not in builders:
                continue

            relative = src.relative_to(input_root)
            json_path = _first_existing_path(
                structured_json_path(src, relative, input_root, output_root, fmt),
                _legacy_structured_json_path(relative, output_root),
            )
            if not json_path.exists():
                if require_all:
                    print(f"missing replacement JSON: {json_path}", file=sys.stderr)
                    failed += 1
                continue

            checked += 1
            checked_json_paths.add(json_path.resolve())
            try:
                payload = json.loads(json_path.read_text(encoding="utf-8"))
                rebuilt = builders[fmt](payload)
                if not rebuilt:
                    print(f"{json_path}: rebuilt {fmt} is empty", file=sys.stderr)
                    failed += 1
                    continue
                if loadability_only:
                    continue
                original = src.read_bytes()
            except Exception as exc:
                print(f"{json_path}: {exc}", file=sys.stderr)
                failed += 1
                continue

            rebuild_error = compare_structured_rebuild(str(json_path), fmt, original, rebuilt)
            if rebuild_error:
                print(rebuild_error, file=sys.stderr)
                failed += 1

    if loadability_only:
        for json_path in sorted(output_root.rglob("*.json")):
            fmt = _format_from_structured_json_path(json_path)
            if fmt not in builders:
                continue
            if json_path.resolve() in checked_json_paths:
                continue
            extra_checked, extra_failed = _validate_structured_json_loadability(json_path, fmt, builders[fmt])
            checked += extra_checked
            failed += extra_failed

    return checked, failed
