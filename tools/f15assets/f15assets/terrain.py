from __future__ import annotations

from typing import Any, Dict, List

from .io import from_base64, read_s16_le, read_u16_le, to_base64, write_s16_le, write_u16_le

SIGNATURE_3DT = 0x3131
SIGNATURE_3DG = 0x3232


def parse_3dt(data: bytes) -> Dict[str, Any]:
    """Parse 3dt."""
    if len(data) < 12:
        raise ValueError(".3DT data too short")

    offset = 0
    signature = read_u16_le(data, offset)
    if signature != SIGNATURE_3DT:
        raise ValueError(f"bad .3DT signature: 0x{signature:04x}")
    offset += 2

    # The file has five LOD/terrain levels. Each level first stores per-tile
    # object counts, followed by packed object records for all tiles.
    counts = [read_u16_le(data, offset + i * 2) for i in range(5)]
    offset += 10

    tile_sizes: List[List[int]] = []
    for count in counts:
        row = []
        for _ in range(count):
            row.append(read_u16_le(data, offset))
            offset += 2
        tile_sizes.append(row)

    level_records: List[Dict[str, Any]] = []
    for level, objects_per_tile in enumerate(tile_sizes):
        tiles = []
        for tile, object_count in enumerate(objects_per_tile):
            objects = []
            for _ in range(object_count):
                if offset + 8 > len(data):
                    raise ValueError("truncated .3DT tile object")
                x = read_s16_le(data, offset)
                y = read_s16_le(data, offset + 2)
                z = read_s16_le(data, offset + 4)
                shape_word = read_u16_le(data, offset + 6)
                offset += 8
                objects.append(
                    {
                        "x": x,
                        "y": y,
                        "z": z,
                        "shape_word": shape_word,
                    }
                )
            tiles.append({"tile_index": tile, "objects": objects})
        level_records.append({"level": level, "objects": tiles})

    return {
        "format": "3DT",
        "version": 1,
        "signature": signature,
        "tile_counts": counts,
        "levels": level_records,
        "trailing_bytes": to_base64(data[offset:]),
    }


def build_3dt(payload: Dict[str, Any]) -> bytes:
    """Build 3dt from normalized asset data."""
    if payload.get("format") != "3DT":
        raise ValueError("invalid payload format")

    levels = payload["levels"]
    counts = [len(level["objects"]) for level in levels]

    out = bytearray()
    out.extend(write_u16_le(SIGNATURE_3DT))
    for count in counts:
        out.extend(write_u16_le(int(count)))

    for level in levels:
        for tile in level["objects"]:
            out.extend(write_u16_le(len(tile["objects"])))

    for level in levels:
        for tile in level["objects"]:
            for obj in tile["objects"]:
                out.extend(write_s16_le(int(obj["x"])))
                out.extend(write_s16_le(int(obj["y"])))
                out.extend(write_s16_le(int(obj["z"])))
                out.extend(write_u16_le(int(obj["shape_word"])))

    trailing = payload.get("trailing_bytes")
    if trailing:
        out.extend(from_base64(trailing))
    return bytes(out)


def parse_3dg(data: bytes) -> Dict[str, Any]:
    """Parse 3dg."""
    if len(data) < 1810:
        raise ValueError(f"unexpected .3DG size: {len(data)}")

    if read_u16_le(data, 0) != SIGNATURE_3DG:
        raise ValueError(f"bad .3DG signature: 0x{read_u16_le(data, 0):04x}")

    # stterr.c lookupGridCell() uses these as a five-level hierarchy:
    # level 4 is a 4x4 top grid, level 3 is a 16x16 grid, and levels 2..0 are
    # recursive 4x4 subgrid lookup tables selected by their parent cell.
    offset = 2
    level4_top_grid = list(data[offset : offset + 16])
    offset += 16
    level3_grid = list(data[offset : offset + 256])
    offset += 256
    level2_subgrid = list(data[offset : offset + 512])
    offset += 512
    level1_subgrid = list(data[offset : offset + 512])
    offset += 512
    level0_subgrid = list(data[offset : offset + 512])
    offset += 512

    trailing = data[offset:]

    return {
        "format": "3DG",
        "version": 1,
        "signature": SIGNATURE_3DG,
        "level4_top_grid": level4_top_grid,
        "level3_grid": level3_grid,
        "level2_subgrid": level2_subgrid,
        "level1_subgrid": level1_subgrid,
        "level0_subgrid": level0_subgrid,
        "trailing_bytes": to_base64(trailing),
    }


def build_3dg(payload: Dict[str, Any]) -> bytes:
    """Build 3dg from normalized asset data."""
    if payload.get("format") != "3DG":
        raise ValueError("invalid payload format")

    level4_top_grid = payload["level4_top_grid"]
    level3_grid = payload["level3_grid"]
    level2_subgrid = payload["level2_subgrid"]
    level1_subgrid = payload["level1_subgrid"]
    level0_subgrid = payload["level0_subgrid"]

    if (
        len(level4_top_grid) != 16
        or len(level3_grid) != 256
        or len(level2_subgrid) != 512
        or len(level1_subgrid) != 512
        or len(level0_subgrid) != 512
    ):
        raise ValueError(".3DG grid size mismatch")

    out = bytearray()
    out.extend(write_u16_le(SIGNATURE_3DG))
    out.extend(bytes(int(x) & 0xFF for x in level4_top_grid))
    out.extend(bytes(int(x) & 0xFF for x in level3_grid))
    out.extend(bytes(int(x) & 0xFF for x in level2_subgrid))
    out.extend(bytes(int(x) & 0xFF for x in level1_subgrid))
    out.extend(bytes(int(x) & 0xFF for x in level0_subgrid))

    trailing = payload.get("trailing_bytes")
    if trailing:
        out.extend(from_base64(trailing))

    return bytes(out)
