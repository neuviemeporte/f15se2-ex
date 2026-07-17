from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Any, Dict, List, Tuple

FONT_CODEPOINT_START = 0x20
FONT_CODEPOINT_COUNT = 96
# The recovered fonts cover the printable ASCII range only. Export metadata also
# records CP437 source bytes so authors can extend the modern BDF/JSON output to
# Unicode without losing the original byte mapping.
ATLAS_COLUMNS = 16
ATLAS_PADDING = 1

# The original game has no Cyrillic source glyphs. Export Russian glyphs as
# explicit 5x7 bitmap templates instead of borrowing Latin letters: letters such
# as И/Й must have their diagonal in the Cyrillic direction, and shapes such as
# Д/Л/Я/Ю do not have safe Latin equivalents. The templates are downsampled or
# centred into each recovered font's cell size, so every font_<id>.bdf receives
# the same Unicode coverage while preserving its original height/width limits.
CYRILLIC_5X7 = {
    0x0401: ["01010", "11111", "10000", "11110", "10000", "10000", "11111"],
    0x0410: ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    0x0411: ["11111", "10000", "10000", "11110", "10001", "10001", "11110"],
    0x0412: ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    0x0413: ["11111", "10000", "10000", "10000", "10000", "10000", "10000"],
    0x0414: ["01111", "01001", "01001", "01001", "01001", "11111", "10001"],
    0x0415: ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    0x0416: ["10101", "10101", "10101", "01110", "10101", "10101", "10101"],
    0x0417: ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    0x0418: ["10001", "10011", "10101", "10101", "11001", "10001", "10001"],
    0x0419: ["01010", "10001", "10011", "10101", "11001", "10001", "10001"],
    0x041A: ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    0x041B: ["01111", "01001", "01001", "01001", "01001", "01001", "10001"],
    0x041C: ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    0x041D: ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    0x041E: ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    0x041F: ["11111", "10001", "10001", "10001", "10001", "10001", "10001"],
    0x0420: ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    0x0421: ["01111", "10000", "10000", "10000", "10000", "10000", "01111"],
    0x0422: ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    0x0423: ["10001", "10001", "10001", "01111", "00001", "00001", "11110"],
    0x0424: ["00100", "01110", "10101", "10101", "01110", "00100", "00100"],
    0x0425: ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    0x0426: ["10001", "10001", "10001", "10001", "10001", "11111", "00001"],
    0x0427: ["10001", "10001", "10001", "01111", "00001", "00001", "00001"],
    0x0428: ["10101", "10101", "10101", "10101", "10101", "10101", "11111"],
    0x0429: ["10101", "10101", "10101", "10101", "10101", "11111", "00001"],
    0x042A: ["11000", "01000", "01000", "01110", "01001", "01001", "01110"],
    0x042B: ["10001", "10001", "10001", "11101", "10011", "10011", "11101"],
    0x042C: ["10000", "10000", "10000", "11110", "10001", "10001", "11110"],
    0x042D: ["11110", "00001", "00001", "01111", "00001", "00001", "11110"],
    0x042E: ["10010", "10101", "10101", "11101", "10101", "10101", "10010"],
    0x042F: ["01111", "10001", "10001", "01111", "00101", "01001", "10001"],
}

CYRILLIC_LOWER_TO_UPPER = {
    0x0451: 0x0401,
    **{codepoint + 0x20: codepoint for codepoint in range(0x0410, 0x0430)},
}

CYRILLIC_LOWER_5X7 = {
    0x0451: ["01010", "00000", "01110", "10001", "11111", "10000", "01110"],
    0x0430: ["00000", "00000", "01110", "00001", "01111", "10001", "01111"],
    0x0431: ["00111", "01000", "10000", "11110", "10001", "10001", "01110"],
    0x0432: ["00000", "00000", "11110", "10001", "11110", "10001", "11110"],
    0x0433: ["00000", "00000", "11111", "10000", "10000", "10000", "10000"],
    0x0434: ["00000", "00000", "01111", "01001", "01001", "11111", "10001"],
    0x0435: ["00000", "00000", "01110", "10001", "11111", "10000", "01110"],
    0x0436: ["00000", "00000", "10101", "10101", "01110", "10101", "10101"],
    0x0437: ["00000", "00000", "11110", "00001", "01110", "00001", "11110"],
    0x0438: ["00000", "00000", "10001", "10011", "10101", "11001", "10001"],
    0x0439: ["01010", "00100", "10001", "10011", "10101", "11001", "10001"],
    0x043A: ["00000", "00000", "10001", "10010", "11100", "10010", "10001"],
    0x043B: ["00000", "00000", "01111", "01001", "01001", "01001", "10001"],
    0x043C: ["00000", "00000", "10001", "11011", "10101", "10001", "10001"],
    0x043D: ["00000", "00000", "10001", "10001", "11111", "10001", "10001"],
    0x043E: ["00000", "00000", "01110", "10001", "10001", "10001", "01110"],
    0x043F: ["00000", "00000", "11111", "10001", "10001", "10001", "10001"],
    0x0440: ["00000", "00000", "11110", "10001", "11110", "10000", "10000"],
    0x0441: ["00000", "00000", "01111", "10000", "10000", "10000", "01111"],
    0x0442: ["00000", "00000", "11111", "00100", "00100", "00100", "00100"],
    0x0443: ["00000", "00000", "10001", "10001", "01111", "00001", "11110"],
    0x0444: ["00000", "00100", "01110", "10101", "10101", "01110", "00100"],
    0x0445: ["00000", "00000", "10001", "01010", "00100", "01010", "10001"],
    0x0446: ["00000", "00000", "10001", "10001", "10001", "11111", "00001"],
    0x0447: ["00000", "00000", "10001", "10001", "01111", "00001", "00001"],
    0x0448: ["00000", "00000", "10101", "10101", "10101", "10101", "11111"],
    0x0449: ["00000", "00000", "10101", "10101", "10101", "11111", "00001"],
    0x044A: ["00000", "00000", "11000", "01000", "01110", "01001", "01110"],
    0x044B: ["00000", "00000", "10001", "10001", "11101", "10011", "11101"],
    0x044C: ["00000", "00000", "10000", "10000", "11110", "10001", "11110"],
    0x044D: ["00000", "00000", "11110", "00001", "01111", "00001", "11110"],
    0x044E: ["00000", "00000", "10010", "10101", "11101", "10101", "10010"],
    0x044F: ["00000", "00000", "01111", "10001", "01111", "00101", "10001"],
}

CYRILLIC_4X5 = {
    0x0414: ["0111", "0101", "0101", "1111", "1001"],
    0x0416: ["1010", "1010", "0100", "1010", "1010"],
    0x041B: ["0111", "0101", "0101", "0101", "1001"],
    0x041E: ["0110", "1001", "1001", "1001", "0110"],
    0x0421: ["0111", "1000", "1000", "1000", "0111"],
    0x0426: ["1001", "1001", "1001", "1111", "0001"],
    0x0428: ["1001", "1001", "1011", "1011", "1111"],
    0x0429: ["1001", "1001", "1011", "1111", "0001"],
    0x042D: ["1110", "0001", "0111", "0001", "1110"],
    0x042E: ["1010", "1011", "1111", "1011", "1010"],
    0x042F: ["0111", "1001", "0111", "0101", "1001"],
    0x0436: ["1010", "1010", "0100", "1010", "1010"],
    0x0448: ["1001", "1001", "1011", "1011", "1111"],
    0x0449: ["1001", "1001", "1011", "1111", "0001"],
}

LATIN_EXTENDED_4X5 = {
    0x00C1: ["0010", "0110", "1001", "1111", "1001"],  # Á
    0x00E4: ["1010", "0110", "0001", "0111", "0111"],  # ä
    0x00E1: ["0010", "0110", "0001", "0111", "0111"],  # á
    0x00C9: ["0010", "1111", "1000", "1110", "1111"],  # É
    0x00E9: ["0010", "0110", "1111", "1000", "0111"],  # é
    0x00F6: ["1010", "0110", "1001", "1001", "0110"],  # ö
    0x00FC: ["1010", "1001", "1001", "1001", "0111"],  # ü
    0x0104: ["0110", "1001", "1111", "1001", "1011"],  # Ą
    0x0105: ["0110", "0001", "0111", "1001", "0011"],  # ą
    0x0106: ["0010", "0111", "1000", "1000", "0111"],  # Ć
    0x0107: ["0010", "0111", "1000", "1000", "0111"],  # ć
    0x010C: ["1010", "0111", "1000", "1000", "0111"],  # Č
    0x010D: ["1010", "0111", "1000", "1000", "0111"],  # č
    0x0110: ["1110", "1011", "1011", "1011", "1110"],  # Đ
    0x0111: ["0010", "1110", "1011", "1011", "1110"],  # đ
    0x0118: ["1111", "1000", "1110", "1000", "1111"],  # Ę
    0x0119: ["0111", "1000", "1110", "1000", "0111"],  # ę
    0x0141: ["1000", "1010", "1100", "1000", "1111"],  # Ł
    0x0142: ["1000", "1010", "1100", "1000", "1000"],  # ł
    0x0143: ["0010", "1001", "1101", "1011", "1001"],  # Ń
    0x0144: ["0010", "1110", "1001", "1001", "1001"],  # ń
    0x00D3: ["0010", "0110", "1001", "1001", "0110"],  # Ó
    0x00F3: ["0010", "0110", "1001", "1001", "0110"],  # ó
    0x015A: ["0010", "1111", "1000", "0110", "1110"],  # Ś
    0x015B: ["0010", "1110", "1100", "0010", "1110"],  # ś
    0x0160: ["1010", "1111", "1000", "0110", "1110"],  # Š
    0x0161: ["1010", "1110", "1100", "0010", "1110"],  # š
    0x0179: ["0010", "1111", "0010", "0100", "1111"],  # Ź
    0x017A: ["0010", "1111", "0010", "0100", "1111"],  # ź
    0x017B: ["0010", "1111", "0010", "0100", "1111"],  # Ż
    0x017C: ["0010", "1111", "0010", "0100", "1111"],  # ż
    0x017D: ["1010", "1111", "0010", "0100", "1111"],  # Ž
    0x017E: ["1010", "1111", "0010", "0100", "1111"],  # ž
}

LATIN_EXTENDED_8X8 = {
    0x0104: ["00110000", "01001000", "10000100", "11111100", "10000100", "10000100", "10000100", "00001000"],  # Ą
    0x0105: ["00000000", "00000000", "01111000", "00000100", "01111100", "10000100", "01111100", "00001000"],  # ą
    0x0118: ["11111100", "10000000", "10000000", "11111000", "10000000", "10000000", "11111100", "00001000"],  # Ę
    0x0119: ["00000000", "00000000", "01111000", "10000100", "11111100", "10000000", "01111000", "00001000"],  # ę
    0x0110: ["11110000", "10001000", "10001000", "11111000", "10001000", "10001000", "11110000", "00000000"],  # Đ
    0x0111: ["00001000", "00111100", "00001000", "01111000", "10001000", "10001000", "01111000", "00000000"],  # đ
    0x0141: ["10000000", "10000000", "10010000", "10100000", "11000000", "10000000", "11111000", "00000000"],  # Ł
    0x0142: ["10000000", "10000000", "10010000", "10100000", "11000000", "10000000", "10000000", "00000000"],  # ł
    0x0179: ["00010000", "11111100", "00001000", "00010000", "00100000", "01000000", "11111100", "00000000"],  # Ź
    0x017A: ["00010000", "00000000", "11111000", "00010000", "00100000", "01000000", "11111000", "00000000"],  # ź
    0x017B: ["00010000", "11111100", "00001000", "00010000", "00100000", "01000000", "11111100", "00000000"],  # Ż
    0x017C: ["00010000", "00000000", "11111000", "00010000", "00100000", "01000000", "11111000", "00000000"],  # ż
    0x017D: ["00101000", "00010000", "11111100", "00001000", "00010000", "00100000", "11111100", "00000000"],  # Ž
    0x017E: ["00101000", "00010000", "11111000", "00010000", "00100000", "01000000", "11111000", "00000000"],  # ž
}

# German and Polish letters are generated from each font's own ASCII Latin
# glyphs, then accented in-place. This keeps the recovered font style instead
# of replacing letters with a generic Unicode font. The BDF remains the source
# of truth for authors who want to hand-tune any generated glyph.
LATIN_EXTENDED_GLYPHS = {
    # German
    0x00C4: ("A", "diaeresis"), 0x00D6: ("O", "diaeresis"),
    0x00DC: ("U", "diaeresis"), 0x00DF: ("B", "eszett"),
    0x00E4: ("a", "diaeresis"), 0x00F6: ("o", "diaeresis"),
    0x00FC: ("u", "diaeresis"), 0x1E9E: ("B", "eszett"),
    # Acute Latin letters useful for localized custom text.
    0x00C1: ("A", "acute"), 0x00E1: ("a", "acute"),
    0x00C9: ("E", "acute"), 0x00E9: ("e", "acute"),
    # Polish
    0x0104: ("A", "ogonek"), 0x0105: ("a", "ogonek"),
    0x0106: ("C", "acute"), 0x0107: ("c", "acute"),
    # Serbian Latin
    0x010C: ("C", "caron"), 0x010D: ("c", "caron"),
    0x0110: ("D", "slash"), 0x0111: ("d", "slash"),
    0x0118: ("E", "ogonek"), 0x0119: ("e", "ogonek"),
    0x0141: ("L", "slash"), 0x0142: ("l", "slash"),
    0x0143: ("N", "acute"), 0x0144: ("n", "acute"),
    0x00D3: ("O", "acute"), 0x00F3: ("o", "acute"),
    0x015A: ("S", "acute"), 0x015B: ("s", "acute"),
    0x0160: ("S", "caron"), 0x0161: ("s", "caron"),
    0x0179: ("Z", "acute"), 0x017A: ("z", "acute"),
    0x017B: ("Z", "dot"), 0x017C: ("z", "dot"),
    0x017D: ("Z", "caron"), 0x017E: ("z", "caron"),
}


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
                "src/fontdata.h",
                "src/gfx_impl.c",
            ],
            "source_sha256": {
                "src/fontdata.h": _font_source_hash(fontdata_path),
                "src/gfx_impl.c": _font_source_hash(gfx_impl_path),
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
            "Glyph shapes are stored in the BDF and PNG atlas. JSON is only "
            "supplemental atlas/index metadata and must not override BDF/PNG "
            "glyph data."
        ),
        "source_files": font["source_files"],
        "source_sha256": font["source_sha256"],
        "glyphs": glyph_meta,
    }
    return image, metadata


def _glyph_bdf_bitmap(glyph_rows: List[int], width: int) -> List[str]:
    hex_width = max(2, ((width + 7) // 8) * 2)
    return [f"{row & 0xFF:0{hex_width}X}" for row in glyph_rows]


def _cyrillic_template(codepoint: int) -> List[str]:
    if codepoint in CYRILLIC_LOWER_5X7:
        return CYRILLIC_LOWER_5X7[codepoint]
    upper = CYRILLIC_LOWER_TO_UPPER.get(codepoint, codepoint)
    return CYRILLIC_5X7[upper]


def _scale_template_columns(row: str, width: int) -> str:
    if width == len(row):
        return row
    if width <= 0:
        return ""
    scaled = []
    for x in range(width):
        start = int(x * len(row) / width)
        end = int((x + 1) * len(row) / width)
        if end <= start:
            end = start + 1
        scaled.append("1" if "1" in row[start:end] else "0")
    return "".join(scaled)


def _template_to_glyph_rows(template: List[str], cell_width: int, height: int) -> List[int]:
    draw_width = max(1, min(5, cell_width))
    x_offset = max(0, (cell_width - draw_width) // 2)
    rows: List[int] = []
    for y in range(height):
        if len(template) == 7 and height == 6:
            # The 6-row menu fonts need the centre row preserved; simple
            # rounding skips it and damages Э/З/Я-like glyphs.
            src_y = [0, 1, 2, 3, 5, 6][y]
        else:
            src_y = round(y * (len(template) - 1) / max(1, height - 1))
        scaled = _scale_template_columns(template[src_y], draw_width)
        bits = 0
        for x, pixel in enumerate(scaled):
            if pixel == "1" and x + x_offset < 8:
                bits |= 0x80 >> (x + x_offset)
        rows.append(bits)
    return rows


def _cyrillic_glyph(font: Dict[str, Any], codepoint: int) -> Tuple[List[int], int]:
    cell_width = int(font["cell_width"])
    height = int(font["height"])
    template = CYRILLIC_4X5.get(codepoint) if cell_width <= 4 and height <= 5 else None
    if template is None:
        template = _cyrillic_template(codepoint)
        if codepoint in CYRILLIC_LOWER_5X7 and height < 7:
            # Lowercase Russian letters in a 5/6-row font should not spend two
            # rows on empty ascenders; trim them so small letters remain legible.
            while len(template) > 1 and template[0].count("1") == 0:
                template = template[1:]
    rows = _template_to_glyph_rows(template or _cyrillic_template(codepoint), cell_width, height)
    # Match the recovered font's normal uppercase advance where possible. This
    # keeps Russian UI text layout close to existing Latin menu text while BDF
    # still allows authors to tune individual advances by hand.
    advance_idx = ord("M") - FONT_CODEPOINT_START
    advance = int(font["advance_widths"][advance_idx])
    return rows, max(1, min(255, advance))


def _ascii_glyph(font: Dict[str, Any], source_char: str) -> Tuple[List[int], int]:
    idx = ord(source_char) - FONT_CODEPOINT_START
    if idx < 0 or idx >= FONT_CODEPOINT_COUNT:
        idx = ord("?") - FONT_CODEPOINT_START
    return list(font["glyphs"][idx]), int(font["advance_widths"][idx])


def _set_pixel(rows: List[int], x: int, y: int, cell_width: int) -> None:
    if 0 <= y < len(rows) and 0 <= x < min(cell_width, 8):
        rows[y] |= 0x80 >> x


def _overlay_mark(rows: List[int], cell_width: int, mark: str) -> None:
    width = min(cell_width, 8)
    if width <= 0 or not rows:
        return
    mid = width // 2
    right = width - 1
    if mark == "diaeresis":
        _set_pixel(rows, max(0, mid - 2), 0, cell_width)
        _set_pixel(rows, min(right, mid + 1), 0, cell_width)
    elif mark == "acute":
        _set_pixel(rows, min(right, mid + 1), 0, cell_width)
        if len(rows) > 1:
            _set_pixel(rows, mid, 1, cell_width)
    elif mark == "caron":
        lit_columns = [
            x
            for row in rows[1:]
            for x in range(width)
            if row & (0x80 >> x)
        ]
        if lit_columns:
            mid = (min(lit_columns) + max(lit_columns)) // 2
        _set_pixel(rows, max(0, mid - 1), 0, cell_width)
        _set_pixel(rows, mid, min(1, len(rows) - 1), cell_width)
        _set_pixel(rows, min(right, mid + 1), 0, cell_width)
    elif mark == "dot":
        _set_pixel(rows, mid, 0, cell_width)
    elif mark == "ogonek":
        _set_pixel(rows, max(0, right - 1), len(rows) - 1, cell_width)
        if len(rows) > 1:
            _set_pixel(rows, right, len(rows) - 2, cell_width)
    elif mark == "slash":
        # Polish Ł/ł and Serbian Đ/đ use a crossbar/diagonal through the stem.
        # It should sit around the optical middle, slightly high; placing it
        # near the bottom reads like a descender instead of a letter mark.
        y0 = 1 if len(rows) <= 5 else max(1, len(rows) // 2 - 2)
        _set_pixel(rows, min(right, mid + 1), y0, cell_width)
        _set_pixel(rows, mid, min(len(rows) - 1, y0 + 1), cell_width)
        if width > 4:
            _set_pixel(rows, max(0, mid - 1), min(len(rows) - 1, y0 + 2), cell_width)


def _latin_extended_glyph(font: Dict[str, Any], codepoint: int) -> Tuple[List[int], int]:
    source_char, mark = LATIN_EXTENDED_GLYPHS[codepoint]
    cell_width = int(font["cell_width"])
    height = int(font["height"])
    if codepoint in LATIN_EXTENDED_4X5 and cell_width <= 4 and height <= 5:
        rows = _template_to_glyph_rows(LATIN_EXTENDED_4X5[codepoint], cell_width, height)
        _, advance = _ascii_glyph(font, source_char)
        return rows, max(1, min(255, advance))
    if mark == "eszett":
        rows = _template_to_glyph_rows(
            ["01110", "10001", "10000", "11110", "10001", "10001", "11110"],
            cell_width,
            height,
        )
        _, advance = _ascii_glyph(font, source_char)
        return rows, max(1, min(255, advance))
    rows, advance = _ascii_glyph(font, source_char)
    if mark in {"acute", "caron", "diaeresis", "dot"} and len(rows) > 5:
        # Reserve the first row for the accent. Without this, the mark is ORed
        # into an already-lit uppercase row (for example Z/Ž), which makes the
        # accent look shifted right or glued to the letter.
        rows = [0] + rows[:-1]
    _overlay_mark(rows, int(font["cell_width"]), mark)
    return rows, max(1, min(255, advance))


def build_bdf(font: Dict[str, Any]) -> str:
    font_id = int(font["font_id"])
    width = int(font["cell_width"])
    height = int(font["height"])
    cyrillic_codepoints = sorted(CYRILLIC_5X7) + sorted(CYRILLIC_LOWER_TO_UPPER)
    latin_codepoints = sorted(LATIN_EXTENDED_GLYPHS)
    extra_codepoints = cyrillic_codepoints + latin_codepoints
    lines = [
        "STARTFONT 2.1",
        f"FONT -MicroProse-F15SE2-Font{font_id}-Medium-R-Normal--{height * 10}-100-75-75-C-{width * 10}-Unicode",
        f"SIZE {height} 75 75",
        f"FONTBOUNDINGBOX {width} {height} 0 0",
        f"STARTPROPERTIES 2",
        f"FONT_ASCENT {height}",
        "FONT_DESCENT 0",
        "ENDPROPERTIES",
        f"CHARS {FONT_CODEPOINT_COUNT + len(extra_codepoints)}",
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
    for codepoint in extra_codepoints:
        if codepoint in LATIN_EXTENDED_GLYPHS:
            glyph_rows, advance = _latin_extended_glyph(font, codepoint)
        else:
            glyph_rows, advance = _cyrillic_glyph(font, codepoint)
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


def export_fonts(
    repo_root: Path,
    output_root: Path,
    *,
    write_bdf: bool = True,
    include_metadata: bool = False,
) -> Dict[str, Any]:
    output_root.mkdir(parents=True, exist_ok=True)
    fonts = load_font_assets(repo_root)
    exported = []
    for font_id, font in sorted(fonts.items()):
        stem = f"font_{font_id}"
        png_path = output_root / f"{stem}.png"
        json_path = output_root / f"{stem}.json"
        if png_path.exists():
            png_path.unlink()
        if include_metadata:
            _image, metadata = _build_atlas(font)
            json_path.write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        elif json_path.exists():
            json_path.unlink()
        bdf_path = None
        if write_bdf:
            bdf_path = output_root / f"{stem}.bdf"
            bdf_path.write_text(build_bdf(font), encoding="ascii")
        record = {
            "font_id": font_id,
            "source": "repository_font_tables",
            # The index is meant to travel with the exported font folder, so
            # keep paths relative instead of baking in a developer machine's
            # checkout/output directory.
            "bdf": bdf_path.name if bdf_path is not None else None,
        }
        if include_metadata:
            record["json"] = json_path.name
        exported.append(record)
    return {
        "format": "F15_FONT_EXPORT_INDEX",
        "version": 1,
        "source": "repository_font_tables",
        "authoring_source": "font_<id>.bdf",
        "runtime_authoritative": "font_<id>.bdf",
        "notes": (
            "Edit BDF for glyph pixels, Unicode codepoints, and advance widths. "
            "Per-font JSON is optional metadata and is not used by the runtime loader."
        ),
        "exported": exported,
    }
