"""Structured asset rebuild comparison helpers for WLD/3DT/3DG JSON."""

from __future__ import annotations

from .validation_compare_core import compare_byte_sequence


def compare_structured_rebuild(path: str, fmt: str, original: bytes, rebuilt: bytes) -> str | None:
    """Compare structured rebuild and report semantic mismatches."""
    return compare_byte_sequence(path, original, rebuilt, "original", "json", f"rebuilt {fmt} differs")
