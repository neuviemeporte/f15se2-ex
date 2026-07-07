from __future__ import annotations

from pathlib import Path
import re
from typing import Any, Dict, List, Optional

from .io import to_base64, from_base64

PIC_WIDTH = 320
PIC_HEIGHT = 200
TITLE640_WIDTH = 640
TITLE640_HEIGHT = 350

# Demo-only images use a different loader path. They may appear as PIC files by
# extension, but are either M0 wrapped or need byte-indexed decode rather than
# the normal 4-bit packed/nibble interpretation.
_DEMO_8BIT_PIC_MARKER = {b"1.PIC", b"2.PIC", b"3.PIC", b"4.PIC"}
_M0_PIC_HEADER_SIZE = 6
_M0_PIC_PALETTE_SIZE = 256 * 3
_M0_PIC_IMAGE_HEADER_SIZE = 8


class _PICBitReader:
    def __init__(self, data: bytes):
        self._data = data
        # The first word is both the first compressed bits and the source for
        # the format mode byte. The original decoder consumes codes from this
        # stream instead of starting after a separate header.
        self._pos = 2
        if len(data) < 2:
            raise ValueError("PIC payload must include 2-byte header")
        self._word = int.from_bytes(data[:2], "little", signed=False)
        self._remaining = 8

    def read_code(self, code_width: int) -> int:
        bits = self._word >> (16 - self._remaining)
        cl = self._remaining
        if cl < code_width:
            if self._pos >= len(self._data):
                next_word = 0
            elif self._pos + 1 == len(self._data):
                next_word = self._data[self._pos]
            else:
                next_word = int.from_bytes(
                    self._data[self._pos : self._pos + 2], "little", signed=False
                )
            self._word = next_word
            self._pos += 2
            bits |= (next_word << cl)
            cl += 16
        bits &= (1 << code_width) - 1
        cl -= code_width
        self._remaining = cl
        return bits


class _LZWDecoder:
    def __init__(self, bit_reader: _PICBitReader, max_width: int):
        self._reader = bit_reader
        self._max_width = max(9, max_width)
        self._code_width = 9
        self._max_code_at_width = 0x1FF
        self._next_code = 256
        self._prev_code = 0
        self._first_char = 0
        self._prev_repeat = 0
        self._repeat_count = 0
        self._out_buf: List[int] = []
        self._dict_parent = [0xFFFF] * 2048
        self._dict_char = [0x00] * 2048
        self._reset_dictionary()

    def _reset_dictionary(self) -> None:
        for i in range(256):
            self._dict_parent[i] = 0xFFFF
            self._dict_char[i] = i
        self._next_code = 256
        self._code_width = 9
        self._max_code_at_width = 0x1FF

    def _decode_code(self) -> List[int]:
        code = self._reader.read_code(self._code_width)
        orig_code = code
        stack: List[int] = []

        # Classic LZW "KwKwK" case: the stream references the code currently
        # being created, so seed the output with the previous first character.
        if code >= self._next_code:
            stack.append(self._first_char)
            code = self._prev_code

        while code < 2048 and self._dict_parent[code] != 0xFFFF and len(stack) < 4096:
            stack.append(self._dict_char[code])
            code = self._dict_parent[code]

        if code < 2048:
            stack.append(self._dict_char[code])

        if stack:
            self._first_char = stack[-1]
        else:
            self._first_char = 0

        if self._next_code < 2048:
            self._dict_parent[self._next_code] = self._prev_code
            self._dict_char[self._next_code] = self._first_char
            self._next_code += 1

        if self._next_code > self._max_code_at_width:
            self._code_width += 1
            self._max_code_at_width = (1 << self._code_width) - 1
            if self._code_width > self._max_width:
                self._reset_dictionary()

        self._prev_code = orig_code

        for i in range(len(stack) // 2):
            stack[i], stack[-i - 1] = stack[-i - 1], stack[i]
        return stack

    def get_next_byte(self) -> int:
        while len(self._out_buf) == 0:
            self._out_buf.extend(self._decode_code())
        return self._out_buf.pop(0)

    def decode_row(self, count: int) -> bytes:
        out = bytearray()
        safety = 0

        while len(out) < count:
            if self._repeat_count:
                out.append(self._prev_repeat)
                self._repeat_count -= 1
                continue

            ch = self.get_next_byte()
            if ch == 0x90:
                # 0x90 is the post-LZW repeat marker used by this format. A
                # zero count escapes a literal 0x90 byte.
                rep_count = self.get_next_byte()
                if rep_count == 0:
                    out.append(0x90)
                    self._prev_repeat = 0x90
                else:
                    self._repeat_count = rep_count - 1
            else:
                out.append(ch)
                self._prev_repeat = ch

            safety += 1
            if safety > 50000:
                break

        return bytes(out[:count])


class _LZWEncoder:
    def __init__(self, max_width: int):
        self._max_width = max(1, max_width)
        self._code_width = 9
        self._max_code_at_width = 0x1FF
        self._next_code = 256
        self._reset_dictionary()
        self._bit_buffer = 0
        self._bit_count = 0
        self._packed: List[int] = []

    def _reset_dictionary(self) -> None:
        self._next_code = 256
        self._code_width = 9
        self._max_code_at_width = 0x1FF

    def _emit(self, code: int) -> None:
        mask = (1 << self._code_width) - 1
        self._bit_buffer |= (code & mask) << self._bit_count
        self._bit_count += self._code_width
        while self._bit_count >= 8:
            self._packed.append(self._bit_buffer & 0xFF)
            self._bit_buffer >>= 8
            self._bit_count -= 8

    def finalize(self) -> bytes:
        if self._bit_count:
            self._packed.append(self._bit_buffer & 0xFF)
            self._bit_buffer = 0
            self._bit_count = 0
        return bytes(self._packed)

    def encode(self, payload: bytes) -> bytes:
        if not payload:
            return self.finalize()

        for ch in payload:
            # Emit raw byte literals to keep encoder behavior stable with the
            # legacy decoder. This guarantees semantic round-trip even if
            # compression ratio is not optimal.
            self._emit(ch)
            if self._next_code < 2048:
                self._next_code += 1

            if self._next_code > self._max_code_at_width:
                self._code_width += 1
                self._max_code_at_width = (1 << self._code_width) - 1
                if self._code_width > self._max_width:
                    self._reset_dictionary()
        return self.finalize()


def decode_pic_asset(
    data: bytes,
    *,
    row_count: int = PIC_HEIGHT,
    source_name: str | None = None,
    forced_bitstream_mode: str | None = None,
) -> Dict[str, Any]:
    if len(data) < 2:
        raise ValueError("PIC payload is too short")

    if _is_m0_wrapped_pic(data):
        return _decode_m0_wrapped_pic(data, source_name=source_name)

    mode = data[0]
    force_legacy_demo_byte_mode = (
        source_name is not None
        and source_name.upper().encode("ascii") in _DEMO_8BIT_PIC_MARKER
    )
    # Normal PIC/SPR art is 4-bit packed after decompression. The demo VGA
    # images are byte-indexed; forcing byte mode prevents the "interlaced" look
    # caused by wrongly splitting every decoded byte into two pixels.
    if mode & 0x80:
        byte_mode = True
        max_width = (-((mode & 0xFF) - 256))
    else:
        byte_mode = False
        max_width = mode

    if forced_bitstream_mode == "byte":
        byte_mode = True
        if not (mode & 0x80):
            max_width = mode
    elif forced_bitstream_mode == "nibble":
        byte_mode = False
        if mode & 0x80:
            max_width = (-((mode & 0xFF) - 256))
    elif force_legacy_demo_byte_mode:
        byte_mode = True
        if max_width <= 0:
            max_width = 12
    elif forced_bitstream_mode is not None:
        raise ValueError(f"unsupported forced_bitstream_mode: {forced_bitstream_mode}")
    if max_width <= 0:
        max_width = 9

    decoder = _LZWDecoder(_PICBitReader(data), max_width)

    decoded = bytearray()
    if byte_mode:
        for _ in range(row_count):
            decoded.extend(decoder.decode_row(PIC_WIDTH))
    else:
        for _ in range(row_count):
            row_nibble = decoder.decode_row(160)
            for i, value in enumerate(row_nibble):
                decoded.append(value & 0x0F)
                decoded.append((value >> 4) & 0x0F)
    if len(decoded) < PIC_WIDTH * row_count:
        decoded.extend(b"\x00" * (PIC_WIDTH * row_count - len(decoded)))
    decoded = bytes(decoded[: PIC_WIDTH * row_count])

    index_bit_depth = 4 if not byte_mode else 8
    return {
        "format": "PIC",
        "version": 1,
        "decoded_width": PIC_WIDTH,
        "decoded_height": row_count,
        "max_lzw_width": max_width,
        "bitstream_mode": "byte" if byte_mode else "nibble",
        "palette_profile": {
            "source": "embedded_payload_unknown",
            "status": "not_embedded",
            "index_mode": "indexed",
            "index_bit_depth": index_bit_depth,
            "color_mode": "palette_index",
            "notes": "PIC/SPR streams are palette-indexed only; palette tables are loaded externally in-game.",
        },
        "pixels_base64": to_base64(decoded),
        "pixels_format": "row-major-indexed",
        "lzw_payload_size": len(data) - 2,
        "compressed_payload_base64": to_base64(data),
    }


def _is_m0_wrapped_pic(data: bytes) -> bool:
    # M0 stores a small wrapper header, a 256-entry VGA DAC palette, then an
    # inner PIC-like compressed image. It is detected structurally because the
    # game still names these files *.PIC.
    if len(data) < _M0_PIC_HEADER_SIZE + _M0_PIC_PALETTE_SIZE + _M0_PIC_IMAGE_HEADER_SIZE + 2:
        return False
    if data[:2] != b"M0":
        return False
    image_header_offset = _M0_PIC_HEADER_SIZE + _M0_PIC_PALETTE_SIZE
    width = int.from_bytes(data[image_header_offset + 4 : image_header_offset + 6], "little")
    height = int.from_bytes(data[image_header_offset + 6 : image_header_offset + 8], "little")
    return width > 0 and height > 0


def _palette_dac6_to_rgb8(palette_dac6: bytes) -> List[int]:
    # VGA DAC channels are 6-bit values. PNG palettes need 8-bit channels.
    return [_to_png_color_rgb8(value) for value in palette_dac6[:768]]


def _decode_m0_wrapped_pic(data: bytes, *, source_name: str | None = None) -> Dict[str, Any]:
    palette_offset = _M0_PIC_HEADER_SIZE
    image_header_offset = palette_offset + _M0_PIC_PALETTE_SIZE
    inner_offset = image_header_offset + _M0_PIC_IMAGE_HEADER_SIZE

    palette_dac6 = data[palette_offset:image_header_offset]
    image_header = data[image_header_offset:inner_offset]
    width = int.from_bytes(image_header[4:6], "little")
    height = int.from_bytes(image_header[6:8], "little")
    inner = data[inner_offset:]

    payload = decode_pic_asset(inner, row_count=height, forced_bitstream_mode="byte")
    payload["decoded_width"] = width
    payload["decoded_height"] = height
    payload["container"] = {
        "format": "M0",
        "header_base64": to_base64(data[:_M0_PIC_HEADER_SIZE]),
        "image_header_base64": to_base64(image_header),
        "inner_pic_offset": inner_offset,
        "inner_payload_size": len(inner),
    }
    payload["palette_profile"] = {
        "source": "m0_embedded_dac_palette",
        "status": "embedded",
        "index_mode": "indexed",
        "index_bit_depth": payload.get("palette_profile", {}).get("index_bit_depth", 8),
        "color_mode": "palette_index",
        "palette_format": "vga_dac_6bit_rgb_triples",
        "notes": "M0-wrapped demo PIC stores a 256-color DAC palette before the inner PIC stream.",
    }
    payload["palette_dac6_base64"] = to_base64(palette_dac6)
    payload["palette_rgb8_base64"] = to_base64(bytes(_palette_dac6_to_rgb8(palette_dac6)))
    payload["inner_compressed_payload_base64"] = payload["compressed_payload_base64"]
    payload["compressed_payload_base64"] = to_base64(data)
    payload["lzw_payload_size"] = len(inner) - 2
    return payload


def decode_title640_pic_asset(data: bytes, *, source_name: str | None = None) -> Dict[str, Any]:
    # showPic640() switches to BIOS mode 0x10 (640x350, 16-color) before
    # calling the start-module PIC blitter. The decompressor still emits 320
    # bytes per row; empirically row pairs are left/right halves of one
    # 640-pixel scanline.
    raw_payload = decode_pic_asset(
        data,
        row_count=TITLE640_HEIGHT * 2,
        source_name=source_name,
        forced_bitstream_mode="byte",
    )
    raw_pixels = from_base64(raw_payload["pixels_base64"])
    reconstructed = bytearray(TITLE640_WIDTH * TITLE640_HEIGHT)
    for y in range(TITLE640_HEIGHT):
        left = raw_pixels[(y * 2) * PIC_WIDTH : (y * 2 + 1) * PIC_WIDTH]
        right = raw_pixels[(y * 2 + 1) * PIC_WIDTH : (y * 2 + 2) * PIC_WIDTH]
        dst = y * TITLE640_WIDTH
        reconstructed[dst : dst + PIC_WIDTH] = left
        reconstructed[dst + PIC_WIDTH : dst + TITLE640_WIDTH] = right

    raw_payload["decoded_width"] = TITLE640_WIDTH
    raw_payload["decoded_height"] = TITLE640_HEIGHT
    raw_payload["bitstream_mode"] = "byte"
    raw_payload["pixels_base64"] = to_base64(bytes(reconstructed))
    raw_payload["title640_layout"] = {
        "video_mode": "BIOS 0x10 640x350 16-color",
        "decoded_storage_width": PIC_WIDTH,
        "decoded_storage_rows": TITLE640_HEIGHT * 2,
        "output_width": TITLE640_WIDTH,
        "output_height": TITLE640_HEIGHT,
        "row_mapping": "even decoded rows are left halves; odd decoded rows are right halves",
    }
    raw_payload["palette_profile"] = {
        "source": "default_ega16",
        "status": "not_embedded",
        "index_mode": "indexed",
        "index_bit_depth": 4,
        "color_mode": "palette_index",
        "notes": "TITLE640.PIC is shown through the 640x350 16-color title-screen path.",
    }
    return raw_payload


def _pack_nibbles(nibble_pixels: bytes) -> bytes:
    if len(nibble_pixels) & 1:
        nibble_pixels = nibble_pixels + b"\x00"
    out = bytearray()
    for i in range(0, len(nibble_pixels), 2):
        out.append((nibble_pixels[i + 1] << 4) | (nibble_pixels[i] & 0x0F))
    return bytes(out)


def encode_pic_asset(record: Dict[str, Any], *, source_pixels: Optional[bytes] = None) -> bytes:
    preserved_payload = record.get("compressed_payload_base64")
    if source_pixels is None and preserved_payload:
        return from_base64(str(preserved_payload))

    width = int(record.get("decoded_width", PIC_WIDTH))
    height = int(record.get("decoded_height", PIC_HEIGHT))
    if width != PIC_WIDTH or height != PIC_HEIGHT:
        raise ValueError("PIC converter currently supports only 320x200 output")

    mode = record.get("bitstream_mode", "byte")
    max_width = int(record.get("max_lzw_width", 12))
    if max_width <= 0:
        max_width = 12

    if source_pixels is None:
        pixel_data = from_base64(record.get("pixels_base64", ""))
    else:
        pixel_data = source_pixels

    if len(pixel_data) < PIC_WIDTH * PIC_HEIGHT:
        pixel_data = pixel_data + b"\x00" * (PIC_WIDTH * PIC_HEIGHT - len(pixel_data))
    elif len(pixel_data) > PIC_WIDTH * PIC_HEIGHT:
        raise ValueError("PIC pixel payload has unexpected size")

    if mode == "nibble":
        if any((value & 0xF0) != 0 for value in pixel_data):
            raise ValueError("nibble-mode pixels must be 4-bit values (0x00..0x0F)")
        pixel_data = _pack_nibbles(pixel_data)
    elif mode != "byte":
        raise ValueError(f"unsupported bitstream_mode: {mode}")

    rle_input = _rle_encode(pixel_data)
    encoder = _LZWEncoder(max_width)
    packed = encoder.encode(rle_input)

    if mode == "nibble":
        mode_byte = max_width & 0x7F
    else:
        mode_byte = (256 - (max_width & 0x7F)) & 0xFF

    first_data = packed[0] if packed else 0
    return bytes([mode_byte, first_data]) + packed[1:]


def _rle_encode(payload: bytes) -> bytes:
    if not payload:
        return b""

    out = bytearray()
    i = 0
    n = len(payload)

    while i < n:
        current = payload[i]
        run = 1
        while i + run < n and payload[i + run] == current and run < 255:
            run += 1

        if current == 0x90:
            out.extend((0x90, 0x00))
            remaining = run - 1
        else:
            out.append(current)
            remaining = run - 1

        while remaining > 0:
            count = min(255, remaining + 1)
            out.extend((0x90, count))
            remaining -= count - 1
        i += run
    return bytes(out)


def decode_pic_to_raw_rgba(data: bytes, row_count: int = PIC_HEIGHT) -> List[int]:
    doc = decode_pic_asset(data, row_count=row_count)
    pixels = from_base64(doc["pixels_base64"])
    return list(pixels)


DEFAULT_EGA16_PALETTE_6BIT = [
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x2A,
    0x00,
    0x2A,
    0x00,
    0x00,
    0x2A,
    0x2A,
    0x2A,
    0x00,
    0x00,
    0x2A,
    0x00,
    0x2A,
    0x2A,
    0x15,
    0x00,
    0x2A,
    0x2A,
    0x2A,
    0x15,
    0x15,
    0x15,
    0x15,
    0x15,
    0x3F,
    0x15,
    0x3F,
    0x15,
    0x15,
    0x3F,
    0x3F,
    0x3F,
    0x15,
    0x15,
    0x3F,
    0x15,
    0x3F,
    0x3F,
    0x3F,
    0x15,
    0x3F,
    0x3F,
    0x3F,
]

_DEFAULT_VGA256_PALETTE_CACHE: Optional[List[int]] = None


def _to_png_color_rgb8(value_6bit: int) -> int:
    value_8bit = int(value_6bit) & 0x3F
    return max(0, min(255, (value_8bit << 2) | (value_8bit >> 4)))


def _extract_u8_array_from_source(name: str) -> Optional[List[int]]:
    source_path = Path(__file__).resolve().parents[3] / "src" / "egdata.c"
    if not source_path.exists():
        for i in (4, 3, 2, 1):
            candidate = Path(__file__).resolve().parents[i] / "src" / "egdata.c"
            if candidate.exists():
                source_path = candidate
                break
    if not source_path.exists():
        return None

    try:
        source_text = source_path.read_text(encoding="utf-8")
    except Exception:
        return None

    decl_match = re.search(
        rf"{re.escape(name)}\s*\[[0-9]+\]\s*=\s*\{{",
        source_text,
        re.S,
    )
    if not decl_match:
        return None

    body_start = decl_match.end()
    brace_depth = 1
    body_end = None
    for i in range(body_start, len(source_text)):
        ch = source_text[i]
        if ch == "{":
            brace_depth += 1
        elif ch == "}":
            brace_depth -= 1
            if brace_depth == 0:
                body_end = i
                break

    if body_end is None:
        return None

    body = source_text[body_start:body_end]

    tokens = re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body)
    if not tokens:
        return None

    values: List[int] = []
    for token in tokens:
        try:
            values.append(int(token, 0))
        except ValueError:
            return None
    return values


def _build_default_vga256_palette() -> Optional[List[int]]:
    global _DEFAULT_VGA256_PALETTE_CACHE
    if _DEFAULT_VGA256_PALETTE_CACHE is not None:
        return _DEFAULT_VGA256_PALETTE_CACHE

    values_16 = DEFAULT_EGA16_PALETTE_6BIT[:]
    values_256_a = _extract_u8_array_from_source("dacValues1")
    values_256_b = _extract_u8_array_from_source("dacValues")
    if (
        values_16 is None
        or not values_256_a
        or len(values_256_a) < 240
        or not values_256_b
        or len(values_256_b) < 480
    ):
        return None

    values_6bit: List[int] = []
    values_6bit.extend(values_16)
    values_6bit.extend(values_256_a[:240])
    values_6bit.extend(values_256_b[:480])

    palette_8bit = [_to_png_color_rgb8(v) for v in values_6bit[:768]]
    if len(palette_8bit) < 768:
        palette_8bit.extend([0] * (768 - len(palette_8bit)))
    if len(palette_8bit) > 768:
        palette_8bit = palette_8bit[:768]

    _DEFAULT_VGA256_PALETTE_CACHE = palette_8bit
    return palette_8bit


def _fallback_vga_palette(index_bit_depth: int) -> List[int]:
    if index_bit_depth <= 4:
        return [_to_png_color_rgb8(c) for c in DEFAULT_EGA16_PALETTE_6BIT]

    palette = list(_build_default_vga256_palette() or [])
    if len(palette) == 768:
        return palette

    palette_8bit = []
    palette_8bit.extend([_to_png_color_rgb8(c) for c in DEFAULT_EGA16_PALETTE_6BIT])
    levels = (0, 51, 102, 153, 204, 255)
    for r in levels:
        for g in levels:
            for b in levels:
                palette_8bit.extend((r, g, b))
    for i in range(24):
        v = int(round(i * 255 / 23))
        palette_8bit.extend((v, v, v))
    if len(palette_8bit) < 768:
        palette_8bit.extend([0] * (768 - len(palette_8bit)))
    return palette_8bit[:768]


def _default_palette(index_bit_depth: int) -> List[int]:
    return _fallback_vga_palette(index_bit_depth)


def to_png_data(
    bytes_payload: bytes,
    *,
    width: int = PIC_WIDTH,
    height: int = PIC_HEIGHT,
    index_bit_depth: int = 8,
    palette: Optional[List[int]] = None,
) -> "Image.Image":
    try:
        from PIL import Image
    except Exception as exc:
        raise RuntimeError("Pillow is required for PNG conversion") from exc

    if len(bytes_payload) < width * height:
        raise ValueError("pixel payload shorter than requested image size")

    image = Image.frombytes("P", (width, height), bytes_payload[: width * height])
    if palette is None:
        palette = _default_palette(index_bit_depth)
    image.putpalette(palette[:768] + [0] * max(0, 768 - len(palette[:768])))
    return image


def png_to_pixels(path: str) -> bytes:
    try:
        from PIL import Image
    except Exception as exc:
        raise RuntimeError("Pillow is required for PNG conversion") from exc

    image = Image.open(path)
    if image.mode != "P":
        image = image.convert("P")
    width, height = image.size
    if width != PIC_WIDTH or height != PIC_HEIGHT:
        raise ValueError("Expected 320x200 indexed PNG")
    return bytes(image.tobytes())
