"""Shared diff helpers for replacement-asset validation."""

from __future__ import annotations

from typing import Any


def first_byte_difference(expected: bytes, actual: bytes) -> int:
    """Return the first differing offset, or -1 when only the length differs."""

    return next((idx for idx, (a, b) in enumerate(zip(expected, actual)) if a != b), -1)


def compare_byte_sequence(
    label: str,
    expected: bytes,
    actual: bytes,
    expected_name: str,
    actual_name: str,
    differ_text: str,
) -> str | None:
    """Return a single human-readable byte diff message, or None on equality."""

    if expected == actual:
        return None
    first = first_byte_difference(expected, actual)
    if first >= 0:
        return (
            f"{label}: {differ_text} at offset {first} "
            f"({expected_name}={expected[first]}, {actual_name}={actual[first]})"
        )
    return (
        f"{label}: {differ_text} length differs "
        f"({expected_name}={len(expected)}, {actual_name}={len(actual)})"
    )


def compare_mapping_exact(
    path: str,
    what: str,
    actual: dict[str, Any],
    expected: dict[str, Any],
    actual_name: str = "actual",
    expected_name: str = "expected",
) -> str | None:
    """Compare small diagnostic dictionaries such as primitive-count summaries."""

    if actual == expected:
        return None
    return f"{path}: {what} differ ({actual_name}={actual}, {expected_name}={expected})"


def compare_sequence_exact(path: str, what: str, actual: list[Any], expected: list[Any]) -> str | None:
    """Compare compact source-proof lists without embedding bulky geometry dumps."""

    if actual == expected:
        return None
    return f"{path}: {what} differs"


def compare_count_value(
    path: str,
    count_name: str,
    actual: int,
    expected: int,
    actual_name: str = "actual",
    expected_name: str = "expected",
) -> str | None:
    if actual == expected:
        return None
    return (
        f"{path}: {count_name} count differs "
        f"({actual_name}={actual}, {expected_name}={expected})"
    )
