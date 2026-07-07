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
    offset = 0
    if len(data) < 2 + 2 + 2 + 2:
        raise ValueError(".WLD data too short")

    unknown_header = data[:2]
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
            "reserved": to_base64(data[offset + 30 : offset + 36]),
        }
        flight_units.append(fields)
        offset += 36

    # These 100-byte tables are preserved as opaque buffers until the remaining
    # mission/world semantics are known.
    if offset + 100 > len(data):
        raise ValueError("truncated wld_buf7")
    wld_buf7 = data[offset : offset + 100]
    offset += 100

    if offset + 100 > len(data):
        raise ValueError("truncated wld_buf8")
    wld_buf8 = data[offset : offset + 100]
    offset += 100

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
        "unknown_header": to_base64(unknown_header),
        "read_item_size": read_item_size,
        "ground_unit_count": ground_unit_count,
        "world_object_count": world_object_count,
        "world_objects": world_objects,
        "flight_unit_count": flight_unit_count,
        "flight_units": flight_units,
        "wld_buf7": to_base64(wld_buf7),
        "wld_buf8": to_base64(wld_buf8),
        "object_type_table": to_base64(object_type_table),
        "terrain_grid": to_base64(terrain_grid),
        "name_table": to_base64(name_table),
        "name_strings": names,
        "trailing_bytes": to_base64(data[offset:]),
    }


def build_wld(payload: Dict[str, Any]) -> bytes:
    if payload.get("format") != "WLD":
        raise ValueError("invalid payload format")

    unknown_header = from_base64(payload["unknown_header"])
    if len(unknown_header) != 2:
        raise ValueError("unknown_header must be 2 bytes")

    world_objects = payload["world_objects"]
    flight_units = payload["flight_units"]

    out = bytearray()
    out.extend(bytes(unknown_header))
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
        out.extend(from_base64(unit["reserved"]))

    for buf in (payload["wld_buf7"], payload["wld_buf8"], payload["object_type_table"]):
        buf_data = from_base64(buf)
        out.extend(buf_data[:100].ljust(100, b"\x00"))

    terrain_grid = from_base64(payload["terrain_grid"])
    out.extend(terrain_grid[:256].ljust(256, b"\x00"))
    out.extend(from_base64(payload["name_table"]))

    trailing = payload.get("trailing_bytes")
    if trailing:
        out.extend(from_base64(trailing))

    return bytes(out)
