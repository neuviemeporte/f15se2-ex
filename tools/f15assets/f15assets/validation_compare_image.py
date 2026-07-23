"""Image replacement comparison helpers."""

from __future__ import annotations

from .validation_compare_core import compare_byte_sequence, first_byte_difference


def compare_png_pixels(src: str, legacy_pixels: bytes, modern_pixels: bytes) -> str | None:
    return compare_byte_sequence(src, legacy_pixels, modern_pixels, "legacy", "png", "PNG pixels differ")


def compare_png_palette(src: str, legacy_palette: bytes, modern_palette: bytes) -> str | None:
    if legacy_palette == modern_palette:
        return None
    first = first_byte_difference(legacy_palette, modern_palette)
    if first >= 0:
        entry = first // 3
        channel = "rgb"[first % 3]
        return (
            f"{src}: PNG embedded palette differs at entry {entry} channel {channel} "
            f"(legacy={legacy_palette[first]}, png={modern_palette[first]})"
        )
    return f"{src}: PNG embedded palette length differs"
