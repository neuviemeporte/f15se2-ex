"""Font replacement comparison helpers."""

from __future__ import annotations


def compare_font_glyph96(
    path: str,
    codepoint: int,
    expected_rows: list[int],
    actual_rows: list[int],
    expected_width: int,
    actual_width: int,
) -> str | None:
    """Compare one exported 96-glyph font cell after it has passed loadability checks."""

    if actual_rows != expected_rows:
        return f"{path}: bitmap differs for U+{codepoint:04X}"
    if actual_width != expected_width:
        return (
            f"{path}: advance width differs for U+{codepoint:04X} "
            f"(legacy={expected_width}, bdf={actual_width})"
        )
    return None


def compare_font_atlas_rows(path: str, expected_rows: list[list[int]], actual_rows: list[list[int]]) -> str | None:
    """Compare font atlas rows and report semantic mismatches."""
    if actual_rows != expected_rows:
        return f"{path}: atlas bitmap differs from original font rows"
    return None
