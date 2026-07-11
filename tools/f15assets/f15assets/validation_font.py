"""Bitmap font BDF/PNG replacement validation."""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any, Dict

from .fonts import FONT_CODEPOINT_COUNT, FONT_CODEPOINT_START, load_font_assets
from .validation_compare import compare_font_atlas_rows, compare_font_glyph96

__all__ = ["validate_font_replacements"]

RUNTIME_FONT_ATLAS_DIMS = {
    # These are the same runtime slots and atlas cell sizes used by gfx_impl.c.
    # They let --loadability-only validate PNG-only custom font packs even when
    # the captured in-repo font tables are not available for equivalence proof.
    0: (4, 5),
    1: (8, 8),
    3: (6, 6),
    4: (8, 7),
    5: (6, 6),
}


def _parse_bdf_font(path: Path) -> Dict[int, Dict[str, Any]]:
    glyphs: Dict[int, Dict[str, Any]] = {}
    current: Dict[str, Any] | None = None
    bitmap_rows: list[int] = []
    in_bitmap = False

    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        if line.startswith("STARTCHAR "):
            current = {"advance_width": 0, "bitmap_rows": []}
            bitmap_rows = []
            in_bitmap = False
        elif current is not None and line.startswith("ENCODING "):
            current["encoding"] = int(line.split()[1], 0)
        elif current is not None and line.startswith("DWIDTH "):
            parts = line.split()
            if len(parts) >= 2:
                current["advance_width"] = int(parts[1], 0)
        elif current is not None and line.startswith("BITMAP"):
            bitmap_rows = []
            in_bitmap = True
        elif current is not None and line.startswith("ENDCHAR"):
            encoding = current.get("encoding")
            if isinstance(encoding, int):
                current["bitmap_rows"] = bitmap_rows
                glyphs[encoding] = current
            current = None
            bitmap_rows = []
            in_bitmap = False
        elif current is not None and in_bitmap and line:
            bitmap_rows.append(int(line, 16) & 0xFF)

    return glyphs


def _read_font_png_rows(path: Path, cell_width: int, height: int) -> list[list[int]]:
    try:
        from PIL import Image
    except Exception as exc:
        raise RuntimeError("Pillow is required for font PNG validation") from exc

    image = Image.open(path)
    converted = None
    try:
        if image.mode not in {"P", "L", "RGB", "RGBA"}:
            converted = image.convert("RGBA")
            image.close()
            image = converted
        rows: list[list[int]] = []
        cell_stride_x = cell_width + 1
        cell_stride_y = height + 1
        expected_w = 16 * cell_stride_x - 1
        expected_h = 6 * cell_stride_y - 1
        if image.size[0] < expected_w or image.size[1] < expected_h:
            raise ValueError(f"font atlas too small: {image.size[0]}x{image.size[1]}, expected at least {expected_w}x{expected_h}")

        pix = image.load()
        for idx in range(FONT_CODEPOINT_COUNT):
            x0 = (idx % 16) * cell_stride_x
            y0 = (idx // 16) * cell_stride_y
            glyph_rows: list[int] = []
            for y in range(height):
                bits = 0
                for x in range(cell_width):
                    value = pix[x0 + x, y0 + y]
                    if isinstance(value, int):
                        lit = value != 0
                    else:
                        lit = any(int(channel) != 0 for channel in value[:3])
                        if len(value) >= 4:
                            lit = lit and int(value[3]) != 0
                    if lit:
                        bits |= 0x80 >> x
                glyph_rows.append(bits)
            rows.append(glyph_rows)
        return rows
    finally:
        image.close()


def _font_id_from_media_path(path: Path, suffix: str) -> int | None:
    name = path.name.lower()
    if not name.startswith("font_") or not name.endswith(suffix):
        return None
    try:
        return int(name[len("font_") : -len(suffix)], 10)
    except ValueError:
        return None


def validate_font_replacements(
    repo_root: Path,
    output_root: Path,
    require_all: bool,
    loadability_only: bool = False,
) -> tuple[int, int]:
    font_root = output_root / "fonts"
    checked = 0
    failed = 0
    checked_font_paths: set[Path] = set()

    try:
        original_fonts = load_font_assets(repo_root)
    except Exception as exc:
        print(f"font validation unavailable: {exc}", file=sys.stderr)
        if not loadability_only:
            return checked, failed + (1 if require_all else 0)
        original_fonts = {}

    for font_id, font in sorted(original_fonts.items()):
        original_rows = font.get("glyphs", [])
        original_widths = font.get("advance_widths", [])
        bdf_path = font_root / f"font_{font_id}.bdf"
        png_path = font_root / f"font_{font_id}.png"
        bdf_errors: list[str] = []
        has_bdf = bdf_path.exists()
        has_png = png_path.exists()
        if not bdf_path.exists():
            if require_all and not has_png:
                bdf_errors.append(f"missing replacement BDF: {bdf_path}")
        else:
            checked += 1
            checked_font_paths.add(bdf_path.resolve())
            try:
                modern_glyphs = _parse_bdf_font(bdf_path)
            except Exception as exc:
                bdf_errors.append(f"{bdf_path}: {exc}")
            else:
                for idx in range(FONT_CODEPOINT_COUNT):
                    codepoint = FONT_CODEPOINT_START + idx
                    modern = modern_glyphs.get(codepoint)
                    if modern is None:
                        bdf_errors.append(f"{bdf_path}: missing glyph U+{codepoint:04X}")
                        continue
                    actual_rows = [int(row) & 0xFF for row in modern.get("bitmap_rows", [])]
                    actual_width = int(modern.get("advance_width", -1))
                    if not actual_rows:
                        bdf_errors.append(f"{bdf_path}: glyph U+{codepoint:04X} has no bitmap rows")
                        continue
                    if actual_width <= 0:
                        bdf_errors.append(f"{bdf_path}: glyph U+{codepoint:04X} has invalid advance width {actual_width}")
                        continue
                    if loadability_only:
                        continue

                    glyph_error = compare_font_glyph96(
                        str(bdf_path),
                        codepoint,
                        [int(row) & 0xFF for row in original_rows[idx]],
                        actual_rows,
                        int(original_widths[idx]),
                        actual_width,
                    )
                    if glyph_error:
                        bdf_errors.append(glyph_error)

        png_valid = False
        if not png_path.exists():
            if require_all and not has_bdf:
                print(f"missing replacement font PNG: {png_path}", file=sys.stderr)
                failed += 1
        else:
            checked += 1
            checked_font_paths.add(png_path.resolve())
            try:
                actual_png_rows = _read_font_png_rows(
                    png_path,
                    int(font["cell_width"]),
                    int(font["height"]),
                )
                png_valid = True
            except Exception as exc:
                print(f"{png_path}: {exc}", file=sys.stderr)
                failed += 1
            else:
                if not loadability_only:
                    expected_png_rows = [
                        [int(row) & 0xFF for row in glyph_rows]
                        for glyph_rows in original_rows
                    ]
                    atlas_error = compare_font_atlas_rows(str(png_path), expected_png_rows, actual_png_rows)
                    if atlas_error:
                        print(atlas_error, file=sys.stderr)
                        failed += 1

        if bdf_errors and (not loadability_only or not png_valid):
            for error in bdf_errors:
                print(error, file=sys.stderr)
            failed += len(bdf_errors)
        elif bdf_errors:
            for error in bdf_errors:
                print(f"warning: {error}; PNG atlas fallback is loadable", file=sys.stderr)

    if loadability_only and font_root.exists():
        for bdf_path in sorted(font_root.glob("font_*.bdf")):
            if bdf_path.resolve() in checked_font_paths:
                continue
            checked += 1
            try:
                modern_glyphs = _parse_bdf_font(bdf_path)
            except Exception as exc:
                print(f"{bdf_path}: {exc}", file=sys.stderr)
                failed += 1
                continue
            for idx in range(FONT_CODEPOINT_COUNT):
                codepoint = FONT_CODEPOINT_START + idx
                glyph = modern_glyphs.get(codepoint)
                if glyph is None:
                    print(f"{bdf_path}: missing glyph U+{codepoint:04X}", file=sys.stderr)
                    failed += 1
                    continue
                actual_rows = [int(row) & 0xFF for row in glyph.get("bitmap_rows", [])]
                actual_width = int(glyph.get("advance_width", -1))
                if not actual_rows:
                    print(f"{bdf_path}: glyph U+{int(codepoint):04X} has no bitmap rows", file=sys.stderr)
                    failed += 1
                    continue
                if actual_width <= 0:
                    print(f"{bdf_path}: glyph U+{int(codepoint):04X} has invalid advance width {actual_width}", file=sys.stderr)
                    failed += 1
                    continue
        for png_path in sorted(font_root.glob("font_*.png")):
            if png_path.resolve() in checked_font_paths:
                continue
            font_id = _font_id_from_media_path(png_path, ".png")
            dims = RUNTIME_FONT_ATLAS_DIMS.get(font_id if font_id is not None else -1)
            if dims is None:
                print(f"warning: {png_path}: no runtime font atlas dimensions for this font id; skipping source-free PNG loadability check", file=sys.stderr)
                continue
            checked += 1
            try:
                _read_font_png_rows(png_path, dims[0], dims[1])
            except Exception as exc:
                print(f"{png_path}: {exc}", file=sys.stderr)
                failed += 1

    return checked, failed
