from __future__ import annotations

from typing import Any, Dict, List, Optional

from .io import (
    from_base64,
    read_s16_le,
    read_u16_le,
    split_c_strings,
    to_base64,
    write_s16_le,
    write_u16_le,
)

WORLD_BUFSZ = 750


def parse_wld(data: bytes) -> Dict[str, Any]:
    """Parse wld."""
    offset = 0
    if len(data) < 2 + 2 + 2 + 2:
        raise ValueError(".WLD data too short")

    # parseWorld() reads these two bytes into wldReadBuf1. worldImportToEgame()
    # later maps byte 0 to g_landTargetId[0] and byte 1 to g_waterTargetId[0].
    terrain_target_ids = data[:2]
    offset += 2

    read_item_size = read_u16_le(data, offset)
    offset += 2
    ground_unit_count = read_u16_le(data, offset)
    offset += 2
    world_object_count = read_u16_le(data, offset)
    offset += 2

    # The first count is named read_item_size in recovered code and controls how
    # many 16-byte world object records are physically present. It can differ
    # from the gameplay counters that follow it.
    world_objects = []
    for _ in range(read_item_size):
        if offset + 16 > len(data):
            raise ValueError("truncated WorldObject table")
        fields = {
            "unitRef": read_u16_le(data, offset),
            "x_coord": read_u16_le(data, offset + 2),
            "y_coord": read_u16_le(data, offset + 4),
            "unitType": read_s16_le(data, offset + 6),
            "targetFlags": read_s16_le(data, offset + 8),
            "occupantType": read_s16_le(data, offset + 10),
            "patrolCount": read_s16_le(data, offset + 12),
            "objectIdx": read_s16_le(data, offset + 14),
        }
        world_objects.append(fields)
        offset += 16

    if offset + 2 > len(data):
        raise ValueError("truncated world file")
    flight_unit_count = read_u16_le(data, offset)
    offset += 2

    flight_units = []
    for _ in range(flight_unit_count):
        if offset + 36 > len(data):
            raise ValueError("truncated FlightUnit table")
        fields = {
            "waypointIdx": read_s16_le(data, offset),
            "x": read_u16_le(data, offset + 2),
            "y": read_u16_le(data, offset + 4),
            "altitude": read_u16_le(data, offset + 6),
            "xPrecise": int.from_bytes(data[offset + 8 : offset + 12], "little", signed=True),
            "yPrecise": int.from_bytes(data[offset + 12 : offset + 16], "little", signed=True),
            "heading": read_s16_le(data, offset + 16),
            "pitch": read_s16_le(data, offset + 18),
            "roll": read_s16_le(data, offset + 20),
            "planeType": read_s16_le(data, offset + 22),
            "flags": read_s16_le(data, offset + 24),
            "maxSpeed": read_s16_le(data, offset + 26),
            "fuel": read_u16_le(data, offset + 28),
            # These bytes are not padding. worldImportToEgame() copies the
            # 36-byte FlightUnit record directly into SimObject, where offsets
            # +0x1e/+0x20/+0x22 are weaponType, terrainColor, and damage.
            "weaponType": read_s16_le(data, offset + 30),
            "terrainColor": read_s16_le(data, offset + 32),
            "damage": read_s16_le(data, offset + 34),
        }
        flight_units.append(fields)
        offset += 36

    # wld_buf7 is copied to g_shapeTargetCategory and used by missile target
    # compatibility through g_targetCompatTable.
    if offset + 100 > len(data):
        raise ValueError("truncated wld_buf7")
    wld_buf7 = data[offset : offset + 100]
    offset += 100

    # wld_buf8 is copied to g_tileKillTally in EGAME and then to END's
    # worldUnitFlags. Keep the cautious name until all bit meanings are proven.
    if offset + 100 > len(data):
        raise ValueError("truncated wld_buf8")
    wld_buf8 = data[offset : offset + 100]
    offset += 100

    # object_type_table is used by START mission generation to match a target's
    # objectIdx against missionTable[].tensionMask.
    if offset + 100 > len(data):
        raise ValueError("truncated object_type_table")
    object_type_table = data[offset : offset + 100]
    offset += 100

    if offset + 256 > len(data):
        raise ValueError("truncated terrain_grid")
    terrain_grid = data[offset : offset + 256]
    offset += 256

    name_tail = data[offset:]
    if len(name_tail) >= WORLD_BUFSZ:
        name_table = name_tail[:WORLD_BUFSZ]
        offset += WORLD_BUFSZ
    else:
        name_table = name_tail
        offset = len(data)

    names = split_c_strings(name_table)

    return {
        "format": "WLD",
        "version": 1,
        "terrain_target_ids": {
            "land": terrain_target_ids[0],
            "water": terrain_target_ids[1],
        },
        "read_item_size": read_item_size,
        "ground_unit_count": ground_unit_count,
        "world_object_count": world_object_count,
        "world_objects": world_objects,
        "flight_unit_count": flight_unit_count,
        "flight_units": flight_units,
        "shape_target_category_table": to_base64(wld_buf7),
        "kill_tally_or_unit_flags": to_base64(wld_buf8),
        "mission_object_type_table": to_base64(object_type_table),
        "terrain_grid": to_base64(terrain_grid),
        "name_table": to_base64(name_table),
        "name_strings": names,
        "trailing_bytes": to_base64(data[offset:]),
    }


def build_wld(payload: Dict[str, Any]) -> bytes:
    """Build wld from normalized asset data."""
    if payload.get("format") != "WLD":
        raise ValueError("invalid payload format")

    terrain_target_ids = payload["terrain_target_ids"]
    header = bytes(
        [
            int(terrain_target_ids.get("land", 0)) & 0xFF,
            int(terrain_target_ids.get("water", 0)) & 0xFF,
        ]
    )

    world_objects = payload["world_objects"]
    flight_units = payload["flight_units"]

    out = bytearray()
    out.extend(header)
    out.extend(write_u16_le(int(payload["read_item_size"])))
    out.extend(write_u16_le(int(payload["ground_unit_count"])))
    out.extend(write_u16_le(int(payload["world_object_count"])))

    for obj in world_objects:
        out.extend(write_u16_le(obj["unitRef"]))
        out.extend(write_u16_le(obj["x_coord"]))
        out.extend(write_u16_le(obj["y_coord"]))
        out.extend(write_s16_le(obj["unitType"]))
        out.extend(write_s16_le(obj["targetFlags"]))
        out.extend(write_s16_le(obj["occupantType"]))
        out.extend(write_s16_le(obj["patrolCount"]))
        out.extend(write_s16_le(obj["objectIdx"]))

    out.extend(write_u16_le(len(flight_units)))
    for unit in flight_units:
        out.extend(write_s16_le(unit["waypointIdx"]))
        out.extend(write_u16_le(unit["x"]))
        out.extend(write_u16_le(unit["y"]))
        out.extend(write_u16_le(unit["altitude"]))
        out.extend(int(unit["xPrecise"]).to_bytes(4, "little", signed=True))
        out.extend(int(unit["yPrecise"]).to_bytes(4, "little", signed=True))
        out.extend(write_s16_le(unit["heading"]))
        out.extend(write_s16_le(unit["pitch"]))
        out.extend(write_s16_le(unit["roll"]))
        out.extend(write_s16_le(unit["planeType"]))
        out.extend(write_s16_le(unit["flags"]))
        out.extend(write_s16_le(unit["maxSpeed"]))
        out.extend(write_u16_le(unit["fuel"]))
        out.extend(write_s16_le(unit["weaponType"]))
        out.extend(write_s16_le(unit["terrainColor"]))
        out.extend(write_s16_le(unit["damage"]))

    table_keys = (
        "shape_target_category_table",
        "kill_tally_or_unit_flags",
        "mission_object_type_table",
    )
    for key in table_keys:
        buf = payload[key]
        buf_data = from_base64(buf)
        out.extend(buf_data[:100].ljust(100, b"\x00"))

    terrain_grid = from_base64(payload["terrain_grid"])
    out.extend(terrain_grid[:256].ljust(256, b"\x00"))
    out.extend(from_base64(payload["name_table"]))

    trailing = payload.get("trailing_bytes")
    if trailing:
        out.extend(from_base64(trailing))

    return bytes(out)
