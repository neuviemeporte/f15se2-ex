from __future__ import annotations

import base64
from dataclasses import dataclass
from typing import Iterable, List, Sequence


def read_u8(data: bytes, offset: int) -> int:
    """Read u8."""
    return data[offset]


def read_u16_le(data: bytes, offset: int) -> int:
    """Read u16 le."""
    return int.from_bytes(data[offset : offset + 2], "little", signed=False)


def read_s16_le(data: bytes, offset: int) -> int:
    """Read s16 le."""
    return int.from_bytes(data[offset : offset + 2], "little", signed=True)


def read_u32_le(data: bytes, offset: int) -> int:
    """Read u32 le."""
    return int.from_bytes(data[offset : offset + 4], "little", signed=False)


def read_s32_le(data: bytes, offset: int) -> int:
    """Read s32 le."""
    return int.from_bytes(data[offset : offset + 4], "little", signed=True)


def write_u8(value: int) -> bytes:
    """Write u8."""
    return int(value & 0xFF).to_bytes(1, "little", signed=False)


def write_u16_le(value: int) -> bytes:
    """Write u16 le."""
    return int(value & 0xFFFF).to_bytes(2, "little", signed=False)


def write_s16_le(value: int) -> bytes:
    """Write s16 le."""
    return int(value).to_bytes(2, "little", signed=True)


def write_u32_le(value: int) -> bytes:
    """Write u32 le."""
    return int(value & 0xFFFFFFFF).to_bytes(4, "little", signed=False)


def write_s32_le(value: int) -> bytes:
    """Write s32 le."""
    return int(value).to_bytes(4, "little", signed=True)


def split_c_strings(data: bytes) -> List[str]:
    """Split a fixed-length C-string table into UTF-8 strings."""
    out: List[str] = []
    start = 0
    for i, b in enumerate(data):
        if b == 0:
            out.append(data[start:i].decode("latin1", errors="replace"))
            start = i + 1
    if start < len(data):
        out.append(data[start:].decode("latin1", errors="replace"))
    return out


def join_c_strings(values: Sequence[str], fixed_size: int) -> bytes:
    """Perform the join c strings asset-processing operation."""
    chunks = [v.encode("latin1", errors="replace") for v in values]
    out = bytearray()
    for chunk in chunks:
        out.extend(chunk)
        out.extend(b"\x00")
    if len(out) < fixed_size:
        out.extend(b"\x00" * (fixed_size - len(out)))
    return bytes(out[:fixed_size])


def ensure_padding(data: bytes, fixed_size: int) -> bytes:
    """Perform the ensure padding asset-processing operation."""
    if len(data) > fixed_size:
        raise ValueError(f"payload is too large: {len(data)} > {fixed_size}")
    return data + b"\x00" * (fixed_size - len(data))


def to_base64(data: bytes) -> str:
    """Convert normalized asset data to base64."""
    return base64.b64encode(data).decode("ascii")


def from_base64(value: str) -> bytes:
    """Perform the from base64 asset-processing operation."""
    return base64.b64decode(value.encode("ascii"))


def bytes_to_u8_list(data: bytes) -> List[int]:
    """Perform the bytes to u8 list asset-processing operation."""
    return list(data)


def u8_list_to_bytes(values: Iterable[int]) -> bytes:
    """Perform the u8 list to bytes asset-processing operation."""
    return bytes(int(v) & 0xFF for v in values)


@dataclass(frozen=True)
class OffsetTracker:
    offset: int = 0

    def take(self, count: int) -> "OffsetTracker":
        """Perform the take asset-processing operation."""
        return OffsetTracker(self.offset + count)
