"""PIC/SPR PNG replacement validation."""

from __future__ import annotations

from pathlib import Path
from typing import Callable

from .io import from_base64
from .pic import decode_pic_asset, decode_title640_pic_asset, expected_runtime_pic_palette
from .validation_compare import compare_png_palette, compare_png_pixels

__all__ = ["validate_png_replacement_loadability", "validate_pic_png_replacement"]


def _open_png_for_validation(path: Path):
    """Open png for validation."""
    try:
        from PIL import Image
    except Exception as exc:
        raise RuntimeError("Pillow is required for replacement validation") from exc

    return Image.open(path)


def _validate_png_loadable_like_runtime(path: Path) -> None:
    """Validate png loadable like runtime against runtime requirements."""
    image = _open_png_for_validation(path)
    try:
        if image.size[0] <= 0 or image.size[1] <= 0:
            raise ValueError(f"PNG has invalid dimensions: {path}")
        if image.mode == "P" and not image.getpalette():
            raise ValueError(f"expected embedded palette in indexed PNG replacement: {path}")
    finally:
        image.close()


def validate_png_replacement_loadability(path: Path) -> list[str]:
    """Validate a PNG with the same broad rules the runtime PIC/SPR loader uses."""

    try:
        _validate_png_loadable_like_runtime(path)
    except Exception as exc:
        return [f"{path}: {exc}"]
    return []


def _read_indexed_png_pixels_and_palette(path: Path, width: int, height: int) -> tuple[bytes, bytes]:
    """Read indexed png pixels and palette."""
    image = _open_png_for_validation(path)
    if image.mode != "P":
        raise ValueError(f"expected indexed PNG replacement for byte-for-byte comparison: {path}")
    if image.size != (width, height):
        raise ValueError(f"expected {width}x{height} PNG replacement: {path} is {image.size[0]}x{image.size[1]}")
    palette = image.getpalette() or []
    if not palette:
        raise ValueError(f"expected embedded palette in indexed PNG replacement: {path}")
    palette_bytes = bytes((palette[:768] + [0] * max(0, 768 - len(palette[:768])))[:768])
    return bytes(image.tobytes()), palette_bytes


def validate_pic_png_replacement(
    src: Path,
    png_path: Path,
    read_binary: Callable[[Path], bytes],
    loadability_only: bool = False,
) -> list[str]:
    """Validate pic png replacement against runtime requirements."""
    errors: list[str] = []
    is_title640 = src.name.upper() == "TITLE640.PIC"
    data = read_binary(src)
    payload = (
        decode_title640_pic_asset(data, source_name=src.name)
        if is_title640
        else decode_pic_asset(data, source_name=src.name)
    )
    width = int(payload.get("decoded_width", 320))
    height = int(payload.get("decoded_height", 200))
    if loadability_only:
        _validate_png_loadable_like_runtime(png_path)
        return errors
    legacy_pixels = from_base64(payload["pixels_base64"])[: width * height]
    modern_pixels, modern_palette = _read_indexed_png_pixels_and_palette(png_path, width, height)
    pixel_error = compare_png_pixels(str(src), legacy_pixels, modern_pixels)
    if pixel_error:
        errors.append(pixel_error)
    try:
        index_bit_depth = int(payload.get("palette_profile", {}).get("index_bit_depth", 8))
    except (TypeError, ValueError):
        index_bit_depth = 8
    legacy_palette_b64 = payload.get("palette_rgb8_base64")
    if isinstance(legacy_palette_b64, str):
        legacy_palette = from_base64(legacy_palette_b64)[:768]
    else:
        expected_palette = expected_runtime_pic_palette(src.name, index_bit_depth)
        legacy_palette = bytes(expected_palette["palette_rgb8"][:768])
    legacy_palette = legacy_palette + (b"\x00" * max(0, 768 - len(legacy_palette)))
    palette_error = compare_png_palette(str(src), legacy_palette[:768], modern_palette)
    if palette_error:
        errors.append(palette_error)
    return errors
