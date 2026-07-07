from __future__ import annotations

import base64
from dataclasses import dataclass
from typing import Iterable, List, Sequence


def read_u8(data: bytes, offset: int) -> int:
    return data[offset]


def read_u16_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=False)


def read_s16_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little", signed=True)


def read_u32_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little", signed=False)


def read_s32_le(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little", signed=True)


def write_u8(value: int) -> bytes:
    return int(value & 0xFF).to_bytes(1, "little", signed=False)


def write_u16_le(value: int) -> bytes:
    return int(value & 0xFFFF).to_bytes(2, "little", signed=False)


def write_s16_le(value: int) -> bytes:
    return int(value).to_bytes(2, "little", signed=True)


def write_u32_le(value: int) -> bytes:
    return int(value & 0xFFFFFFFF).to_bytes(4, "little", signed=False)


def write_s32_le(value: int) -> bytes:
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
    chunks = [v.encode("latin1", errors="replace") for v in values]
    out = bytearray()
    for chunk in chunks:
        out.extend(chunk)
        out.extend(b"\x00")
    if len(out) < fixed_size:
        out.extend(b"\x00" * (fixed_size - len(out)))
    return bytes(out[:fixed_size])


def ensure_padding(data: bytes, fixed_size: int) -> bytes:
    if len(data) > fixed_size:
        raise ValueError(f"payload is too large: {len(data)} > {fixed_size}")
    return data + b"\x00" * (fixed_size - len(data))


def to_base64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def from_base64(value: str) -> bytes:
    return base64.b64decode(value.encode("ascii"))


def bytes_to_u8_list(data: bytes) -> List[int]:
    return list(data)


def u8_list_to_bytes(values: Iterable[int]) -> bytes:
    return bytes(int(v) & 0xFF for v in values)


@dataclass(frozen=True)
class OffsetTracker:
    offset: int = 0

    def take(self, count: int) -> "OffsetTracker":
        return OffsetTracker(self.offset + count)
