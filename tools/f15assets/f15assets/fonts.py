from __future__ import annotations

import base64
import json
import re
from pathlib import Path
from typing import Any, Dict, List, Tuple

from .io import to_base64

FONT_CODEPOINT_START = 0x20
FONT_CODEPOINT_COUNT = 96
# The recovered fonts cover the printable ASCII range only. Export metadata also
# records CP437 source bytes so authors can extend the modern BDF/JSON output to
# Unicode without losing the original byte mapping.
ATLAS_COLUMNS = 16
ATLAS_PADDING = 1


def _extract_c_initializer(text: str, decl_pattern: str) -> str | None:
    match = re.search(decl_pattern, text, re.S)
    if not match:
        return None
    pos = match.end()
    # Regex alone is brittle for nested C initializers; after finding the
    # declaration, count braces to extract the exact initializer body.
    depth = 1
    for i in range(pos, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[pos:i]
    return None


def _parse_int_tokens(text: str) -> List[int]:
    return [int(token, 0) for token in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", text)]


def _parse_font_bitmaps(fontdata_text: str) -> Dict[int, List[List[int]]]:
    fonts: Dict[int, List[List[int]]] = {}
    for match in re.finditer(r"g_font(\d+)_bitmaps\s*\[96\]\[(\d+)\]\s*=\s*\{", fontdata_text):
        font_id = int(match.group(1))
        row_size = int(match.group(2))
        body = _extract_c_initializer(
            fontdata_text[match.start() :],
            r"g_font\d+_bitmaps\s*\[96\]\[\d+\]\s*=\s*\{",
        )
        if body is None:
            continue
        values = _parse_int_tokens(body)
        if len(values) < FONT_CODEPOINT_COUNT * row_size:
            continue
        rows = [
            values[i : i + row_size]
            for i in range(0, FONT_CODEPOINT_COUNT * row_size, row_size)
        ]
        fonts[font_id] = rows
    return fonts


def _parse_font_widths(gfx_text: str) -> Dict[int, List[int]]:
    widths: Dict[int, List[int]] = {}
    for font_id in (0, 1, 3, 4, 5):
        body = _extract_c_initializer(
            gfx_text,
            rf"g_font{font_id}_widths\s*\[96\]\s*=\s*\{{",
        )
        if body is None:
            continue
        values = _parse_int_tokens(body)
        if len(values) >= FONT_CODEPOINT_COUNT:
            widths[font_id] = values[:FONT_CODEPOINT_COUNT]
    return widths


def _bitmap_to_pixels(rows: List[int], width: int) -> List[List[int]]:
    pixels: List[List[int]] = []
    for row in rows:
        # Font rows are stored MSB-first, matching the old blitter's bit order.
        pixels.append([(row >> (7 - x)) & 1 for x in range(width)])
    return pixels


def _font_source_hash(path: Path) -> str:
    import hashlib

    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_font_assets(repo_root: Path) -> Dict[int, Dict[str, Any]]:
    fontdata_path = repo_root / "src" / "fontdata.h"
    gfx_impl_path = repo_root / "src" / "gfx_impl.c"
    fontdata_text = fontdata_path.read_text(encoding="utf-8")
    gfx_text = gfx_impl_path.read_text(encoding="utf-8")

    bitmaps = _parse_font_bitmaps(fontdata_text)
    widths = _parse_font_widths(gfx_text)
    fonts: Dict[int, Dict[str, Any]] = {}
    for font_id, glyph_rows in sorted(bitmaps.items()):
        if font_id not in widths:
            continue
        height = len(glyph_rows[0]) if glyph_rows else 0
        advance_widths = widths[font_id]
        cell_width = max(advance_widths) if advance_widths else 0
        fonts[font_id] = {
            "font_id": font_id,
            "glyphs": glyph_rows,
            "advance_widths": advance_widths,
            "cell_width": cell_width,
            "height": height,
            "source_files": [
                str(fontdata_path),
                str(gfx_impl_path),
            ],
            "source_sha256": {
                str(fontdata_path): _font_source_hash(fontdata_path),
                str(gfx_impl_path): _font_source_hash(gfx_impl_path),
            },
        }
    return fonts


def _build_atlas(font: Dict[str, Any]) -> Tuple["Image.Image", Dict[str, Any]]:
    try:
        from PIL import Image
    except Exception as exc:
        raise RuntimeError("Pillow is required for font atlas export") from exc

    cell_width = int(font["cell_width"])
    height = int(font["height"])
    rows = (FONT_CODEPOINT_COUNT + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS
    cell_stride_x = cell_width + ATLAS_PADDING
    cell_stride_y = height + ATLAS_PADDING
    atlas_width = ATLAS_COLUMNS * cell_stride_x - ATLAS_PADDING
    atlas_height = rows * cell_stride_y - ATLAS_PADDING
    image = Image.new("P", (atlas_width, atlas_height), 0)
    # Keep the PNG indexed and two-color so the atlas remains compact and easy
    # to edit as a bitmap font source.
    image.putpalette([0, 0, 0, 255, 255, 255] + [0] * (768 - 6))

    glyph_meta = []
    pix = image.load()
    for idx, glyph_rows in enumerate(font["glyphs"]):
        codepoint = FONT_CODEPOINT_START + idx
        x0 = (idx % ATLAS_COLUMNS) * cell_stride_x
        y0 = (idx // ATLAS_COLUMNS) * cell_stride_y
        for y, row_byte in enumerate(glyph_rows):
            for x in range(cell_width):
                if (row_byte >> (7 - x)) & 1:
                    pix[x0 + x, y0 + y] = 1
        glyph_meta.append(
            {
                "codepoint": codepoint,
                "unicode": f"U+{codepoint:04X}",
                "char": chr(codepoint),
                "source_encoding": "cp437",
                "source_byte": codepoint,
                "atlas_x": x0,
                "atlas_y": y0,
                "bitmap_width": cell_width,
                "bitmap_height": height,
                "advance_width": int(font["advance_widths"][idx]),
                "bitmap_rows_base64": to_base64(bytes(glyph_rows)),
            }
        )
    metadata = {
        "format": "F15_FONT",
        "version": 1,
        "font_id": int(font["font_id"]),
        "encoding": "unicode",
        "source_encoding": "cp437_ascii_subset",
        "unicode_extensible": True,
        "codepoint_start": FONT_CODEPOINT_START,
        "glyph_count": FONT_CODEPOINT_COUNT,
        "cell_width": cell_width,
        "height": height,
        "atlas_columns": ATLAS_COLUMNS,
        "atlas_padding": ATLAS_PADDING,
        "atlas_mode": "indexed_1bit_black_white",
        "authoring_formats": ["bdf", "png_atlas_json"],
        "notes": (
            "Glyphs are exported with Unicode codepoints. Original game bytes "
            "are preserved as source_byte/source_encoding so additional language "
            "glyphs can be added without losing legacy mapping."
        ),
        "source_files": font["source_files"],
        "source_sha256": font["source_sha256"],
        "glyphs": glyph_meta,
    }
    return image, metadata


def _glyph_bdf_bitmap(glyph_rows: List[int], width: int) -> List[str]:
    hex_width = max(2, ((width + 7) // 8) * 2)
    return [f"{row & 0xFF:0{hex_width}X}" for row in glyph_rows]


def build_bdf(font: Dict[str, Any]) -> str:
    font_id = int(font["font_id"])
    width = int(font["cell_width"])
    height = int(font["height"])
    lines = [
        "STARTFONT 2.1",
        f"FONT -MicroProse-F15SE2-Font{font_id}-Medium-R-Normal--{height * 10}-100-75-75-C-{width * 10}-CP437",
        f"SIZE {height} 75 75",
        f"FONTBOUNDINGBOX {width} {height} 0 0",
        f"STARTPROPERTIES 2",
        f"FONT_ASCENT {height}",
        "FONT_DESCENT 0",
        "ENDPROPERTIES",
        f"CHARS {FONT_CODEPOINT_COUNT}",
    ]
    for idx, glyph_rows in enumerate(font["glyphs"]):
        codepoint = FONT_CODEPOINT_START + idx
        advance = int(font["advance_widths"][idx])
        lines.extend(
            [
                f"STARTCHAR U+{codepoint:04X}",
                f"ENCODING {codepoint}",
                f"SWIDTH {advance * 100} 0",
                f"DWIDTH {advance} 0",
                f"BBX {width} {height} 0 0",
                "BITMAP",
                *_glyph_bdf_bitmap(glyph_rows, width),
                "ENDCHAR",
            ]
        )
    lines.extend(["ENDFONT", ""])
    return "\n".join(lines)


def export_fonts(repo_root: Path, output_root: Path, *, write_bdf: bool = True) -> Dict[str, Any]:
    output_root.mkdir(parents=True, exist_ok=True)
    fonts = load_font_assets(repo_root)
    exported = []
    for font_id, font in sorted(fonts.items()):
        image, metadata = _build_atlas(font)
        stem = f"font_{font_id}"
        png_path = output_root / f"{stem}.png"
        json_path = output_root / f"{stem}.json"
        image.save(png_path)
        json_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        bdf_path = None
        if write_bdf:
            bdf_path = output_root / f"{stem}.bdf"
            bdf_path.write_text(build_bdf(font), encoding="ascii")
        exported.append(
            {
                "font_id": font_id,
                "png": str(png_path),
                "json": str(json_path),
                "bdf": str(bdf_path) if bdf_path is not None else None,
            }
        )
    return {
        "format": "F15_FONT_EXPORT_INDEX",
        "version": 1,
        "source": str(repo_root),
        "exported": exported,
    }
