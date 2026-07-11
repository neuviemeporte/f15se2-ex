"""3D3 JSON, GLB, and GLMESH replacement validation/build helpers."""

from __future__ import annotations

import hashlib
import json
import struct
import sys
from pathlib import Path
from typing import Any, Callable, Dict

from .model3d import build_3d3, export_3d3_gltf_to_glb, export_3d3_shape_gltfs, parse_3d3
from .validation_compare import (
    compare_byte_sequence,
    compare_count_value,
    compare_glmesh_primitive_streams,
    compare_mapping_exact,
    compare_sequence_exact,
)

__all__ = ["glb_to_glmesh_bytes", "validate_3d3_glb_replacements"]


def _safe_output_stem(value: object) -> str:
    text = str(value or "").strip()
    safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in text)
    safe = "_".join(part for part in safe.split("_") if part)
    return safe[:96] or "model"


def _first_existing_path(primary: Path, *fallbacks: Path) -> Path:
    for path in (primary, *fallbacks):
        if path.exists():
            return path
    return primary


def _read_glb_json(path: Path) -> Dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("GLB too short")
    magic, version, total_length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError("bad GLB magic")
    if version != 2:
        raise ValueError(f"unsupported GLB version {version}")
    if total_length != len(data):
        raise ValueError(f"GLB length header mismatch: header={total_length}, actual={len(data)}")
    json_length, chunk_type = struct.unpack_from("<II", data, 12)
    if chunk_type != 0x4E4F534A:
        raise ValueError("first GLB chunk is not JSON")
    if 20 + json_length > len(data):
        raise ValueError("truncated GLB JSON chunk")
    return json.loads(data[20 : 20 + json_length].decode("utf-8"))


def _read_glb_doc_and_bin(path: Path) -> tuple[Dict[str, Any], bytes]:
    data = path.read_bytes()
    if len(data) < 20:
        raise ValueError("GLB too short")
    magic, version, total_length = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2 or total_length != len(data):
        raise ValueError("invalid GLB header")
    json_length, json_type = struct.unpack_from("<II", data, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("first GLB chunk is not JSON")
    json_end = 20 + json_length
    if json_end + 8 > len(data):
        raise ValueError("missing GLB BIN chunk")
    bin_length, bin_type = struct.unpack_from("<II", data, json_end)
    if bin_type != 0x004E4942:
        raise ValueError("second GLB chunk is not BIN")
    bin_start = json_end + 8
    if bin_start + bin_length > len(data):
        raise ValueError("truncated GLB BIN chunk")
    return json.loads(data[20:json_end].decode("utf-8")), data[bin_start : bin_start + bin_length]


def _gltf_accessor_values(doc: Dict[str, Any], blob: bytes, accessor_index: int) -> list[Any]:
    accessor = doc["accessors"][accessor_index]
    view = doc["bufferViews"][int(accessor["bufferView"])]
    offset = int(view.get("byteOffset", 0)) + int(accessor.get("byteOffset", 0))
    count = int(accessor["count"])
    component = int(accessor["componentType"])
    type_name = str(accessor["type"])
    dims = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[type_name]
    fmt_map = {
        5121: ("B", 1),
        5123: ("H", 2),
        5125: ("I", 4),
        5126: ("f", 4),
    }
    if component not in fmt_map:
        raise ValueError(f"unsupported GLB component type {component}")
    fmt, size = fmt_map[component]
    stride = int(view.get("byteStride", dims * size))
    values: list[Any] = []
    for i in range(count):
        base = offset + i * stride
        item = struct.unpack_from("<" + fmt * dims, blob, base)
        values.append(item[0] if dims == 1 else item)
    return values


def _material_rgba(doc: Dict[str, Any], material_index: int | None) -> tuple[float, float, float, float]:
    if material_index is None:
        return (1.0, 1.0, 1.0, 1.0)
    materials = doc.get("materials", [])
    if not isinstance(materials, list) or material_index < 0 or material_index >= len(materials):
        return (1.0, 1.0, 1.0, 1.0)
    pbr = materials[material_index].get("pbrMetallicRoughness", {})
    color = pbr.get("baseColorFactor", [1.0, 1.0, 1.0, 1.0])
    if not isinstance(color, list) or len(color) < 4:
        return (1.0, 1.0, 1.0, 1.0)
    return tuple(float(max(0.0, min(1.0, float(v)))) for v in color[:4])


def _source_primitive_meta(prim: Dict[str, Any]) -> tuple[int, int, int, int]:
    extras = prim.get("extras", {})
    if not isinstance(extras, dict):
        # Custom GLBs exported from Blender may not preserve converter extras.
        # Drawing still uses geometry/materials; these defaults only mean the
        # runtime cache cannot prove original .3D3 primitive identity.
        return (0, -1, -1, 0)
    kind = extras.get("source_primitive_kind")
    if kind == "triangle":
        kind_id = 1
    elif kind == "lines":
        kind_id = 2
    elif kind == "points":
        kind_id = 3
    else:
        kind_id = 0
    try:
        source_index = int(extras.get("source_primitive_index", -1))
    except (TypeError, ValueError):
        source_index = -1
    try:
        source_color = int(extras.get("source_color", -1))
    except (TypeError, ValueError):
        source_color = -1
    flags = 1 if extras.get("source_order_sensitive") is True else 0
    return (kind_id, source_index, source_color, flags)


def glb_to_glmesh_bytes(path: Path) -> bytes:
    source = path.read_bytes()
    source_md5 = hashlib.md5(source).hexdigest().encode("ascii")
    doc, blob = _read_glb_doc_and_bin(path)
    primitives: list[tuple[int, int, int, int, int, tuple[float, float, float, float], list[tuple[float, float, float]]]] = []
    for mesh in doc.get("meshes", []):
        if not isinstance(mesh, dict):
            continue
        for prim in mesh.get("primitives", []):
            if not isinstance(prim, dict):
                continue
            mode = int(prim.get("mode", 4))
            if mode not in {0, 1, 4}:
                continue
            attrs = prim.get("attributes", {})
            if not isinstance(attrs, dict) or "POSITION" not in attrs:
                continue
            positions = _gltf_accessor_values(doc, blob, int(attrs["POSITION"]))
            indices = (
                _gltf_accessor_values(doc, blob, int(prim["indices"]))
                if "indices" in prim
                else list(range(len(positions)))
            )
            verts = []
            for index in indices:
                pos = positions[int(index)]
                verts.append((float(pos[0]), float(pos[1]), float(pos[2])))
            kind_id, source_index, source_color, source_flags = _source_primitive_meta(prim)
            primitives.append((mode, kind_id, source_index, source_color, source_flags, _material_rgba(doc, prim.get("material")), verts))

    out = bytearray()
    # F15GLM3 layout:
    #   0x00  8 bytes   magic "F15GLM3\0"
    #   0x08 32 bytes   lowercase ASCII MD5 of the source GLB
    #   0x28  4 bytes   primitive count (uint32 LE)
    #   0x2c...         primitive records:
    #       mode:uint32, vertex_count:uint32,
    #       source_kind:int32, source_index:int32, source_color:int32,
    #       source_flags:uint32, rgba:4*float32, vertices:N*vec3(float32)
    #
    # The source_* fields duplicate GLB extras on purpose. GLMESH is a reduced
    # runtime cache, and these fields let validation prove that reduction did not
    # merge/reorder color- or z-fighting-sensitive .3D3 primitives.
    out.extend(b"F15GLM3\0")
    out.extend(source_md5)
    out.extend(struct.pack("<I", len(primitives)))
    for mode, kind_id, source_index, source_color, source_flags, rgba, verts in primitives:
        out.extend(struct.pack("<IIiiiIffff", mode, len(verts), kind_id, source_index, source_color, source_flags, *rgba))
        for v in verts:
            out.extend(struct.pack("<fff", *v))
    return bytes(out)


def _glmesh_primitive_counts(data: bytes) -> Dict[str, int]:
    if data.startswith(b"F15GLM3\0"):
        pos = 44
        prim_count = struct.unpack_from("<I", data, 40)[0]
        prim_header_size = 40
    elif data.startswith(b"F15GLM2\0"):
        pos = 44
        prim_count = struct.unpack_from("<I", data, 40)[0]
        prim_header_size = 24
    elif data.startswith(b"F15GLM1\0"):
        pos = 12
        prim_count = struct.unpack_from("<I", data, 8)[0]
        prim_header_size = 24
    else:
        raise ValueError("bad GLMESH magic")

    counts = {"triangles": 0, "lines": 0, "points": 0, "primitives": int(prim_count)}
    for _ in range(int(prim_count)):
        if pos + prim_header_size > len(data):
            raise ValueError("truncated GLMESH primitive header")
        mode, nverts = struct.unpack_from("<II", data, pos)
        pos += prim_header_size
        payload_size = int(nverts) * 12
        if pos + payload_size > len(data):
            raise ValueError("truncated GLMESH vertex payload")
        if mode not in {0, 1, 4}:
            raise ValueError(f"unsupported GLMESH primitive mode {mode}")
        if mode == 4 and int(nverts) % 3:
            raise ValueError("triangle GLMESH primitive has a non-multiple-of-3 vertex count")
        if mode == 1 and int(nverts) % 2:
            raise ValueError("line GLMESH primitive has a non-multiple-of-2 vertex count")
        if mode == 4:
            counts["triangles"] += int(nverts) // 3
        elif mode == 1:
            counts["lines"] += int(nverts) // 2
        elif mode == 0:
            counts["points"] += int(nverts)
        pos += payload_size
    if pos != len(data):
        raise ValueError("GLMESH has trailing bytes")
    return counts


def _glmesh_source_meta(data: bytes) -> list[tuple[int, int, int, int]]:
    if not data.startswith(b"F15GLM3\0"):
        return []
    pos = 44
    prim_count = struct.unpack_from("<I", data, 40)[0]
    meta: list[tuple[int, int, int, int]] = []
    for _ in range(int(prim_count)):
        if pos + 40 > len(data):
            raise ValueError("truncated GLMESH primitive header")
        _mode, nverts, kind_id, source_index, source_color, source_flags = struct.unpack_from("<IIiiiI", data, pos)
        meta.append((int(kind_id), int(source_index), int(source_color), int(source_flags)))
        pos += 40
        payload_size = int(nverts) * 12
        if pos + payload_size > len(data):
            raise ValueError("truncated GLMESH vertex payload")
        pos += payload_size
    return meta


def _glmesh_stream(data: bytes) -> list[Dict[str, Any]]:
    if data.startswith(b"F15GLM3\0"):
        pos = 44
        prim_count = struct.unpack_from("<I", data, 40)[0]
        prim_header_size = 40
    elif data.startswith(b"F15GLM2\0"):
        pos = 44
        prim_count = struct.unpack_from("<I", data, 40)[0]
        prim_header_size = 24
    elif data.startswith(b"F15GLM1\0"):
        pos = 12
        prim_count = struct.unpack_from("<I", data, 8)[0]
        prim_header_size = 24
    else:
        raise ValueError("bad GLMESH magic")

    stream: list[Dict[str, Any]] = []
    for _ in range(int(prim_count)):
        if pos + prim_header_size > len(data):
            raise ValueError("truncated GLMESH primitive header")
        mode, nverts = struct.unpack_from("<II", data, pos)
        if prim_header_size == 40:
            kind_id, source_index, source_color, source_flags = struct.unpack_from("<iiiI", data, pos + 8)
            rgba = struct.unpack_from("<ffff", data, pos + 24)
        else:
            kind_id, source_index, source_color, source_flags = 0, -1, -1, 0
            rgba = struct.unpack_from("<ffff", data, pos + 8)
        pos += prim_header_size
        payload_size = int(nverts) * 12
        if pos + payload_size > len(data):
            raise ValueError("truncated GLMESH vertex payload")
        vertices = [
            struct.unpack_from("<fff", data, pos + vertex_offset)
            for vertex_offset in range(0, payload_size, 12)
        ]
        if mode not in {0, 1, 4}:
            raise ValueError(f"unsupported GLMESH primitive mode {mode}")
        if mode == 4 and int(nverts) % 3:
            raise ValueError("triangle GLMESH primitive has a non-multiple-of-3 vertex count")
        if mode == 1 and int(nverts) % 2:
            raise ValueError("line GLMESH primitive has a non-multiple-of-2 vertex count")
        pos += payload_size
        # Keep the parsed stream explicit. The validator compares this reduced
        # runtime cache against a cache rebuilt from the edited GLB, so stale or
        # hand-edited caches cannot silently drop geometry/color/order metadata.
        stream.append(
            {
                "mode": int(mode),
                "vertices": vertices,
                "source_kind": int(kind_id),
                "source_index": int(source_index),
                "source_color": int(source_color),
                "source_flags": int(source_flags),
                "rgba": tuple(float(value) for value in rgba),
            }
        )
    if pos != len(data):
        raise ValueError("GLMESH has trailing bytes")
    return stream


def _compare_glmesh_streams(expected: bytes, actual: bytes, tolerance: float = 1e-5) -> str | None:
    return compare_glmesh_primitive_streams(_glmesh_stream(expected), _glmesh_stream(actual), tolerance)


def _glb_primitive_counts(doc: Dict[str, Any]) -> Dict[str, int]:
    counts = {"triangles": 0, "lines": 0, "points": 0, "primitives": 0}
    accessors = doc.get("accessors", [])
    if not isinstance(accessors, list):
        return counts
    for mesh in doc.get("meshes", []):
        if not isinstance(mesh, dict):
            continue
        for prim in mesh.get("primitives", []):
            if not isinstance(prim, dict):
                continue
            mode = int(prim.get("mode", 4))
            count = 0
            try:
                if "indices" in prim:
                    accessor_index = int(prim["indices"])
                else:
                    attrs = prim.get("attributes", {})
                    if not isinstance(attrs, dict) or "POSITION" not in attrs:
                        continue
                    accessor_index = int(attrs["POSITION"])
                if 0 <= accessor_index < len(accessors) and isinstance(accessors[accessor_index], dict):
                    count = int(accessors[accessor_index].get("count", 0))
            except (TypeError, ValueError):
                continue
            counts["primitives"] += 1
            if mode == 4:
                counts["triangles"] += count // 3
            elif mode == 1:
                counts["lines"] += count // 2
            elif mode == 0:
                counts["points"] += count
    return counts


def _glb_source_meta(doc: Dict[str, Any]) -> list[tuple[int, int, int, int]]:
    meta: list[tuple[int, int, int, int]] = []
    for mesh in doc.get("meshes", []):
        if not isinstance(mesh, dict):
            continue
        for prim in mesh.get("primitives", []):
            if isinstance(prim, dict):
                meta.append(_source_primitive_meta(prim))
    return meta


def _glb_raw_color_usage(doc: Dict[str, Any]) -> Dict[str, Dict[str, int]]:
    extras = doc.get("extras", {})
    if not isinstance(extras, dict):
        return {"faces": {}, "lines": {}, "points": {}}
    usage = extras.get("raw_color_usage", {})
    if not isinstance(usage, dict):
        return {"faces": {}, "lines": {}, "points": {}}
    normalized: Dict[str, Dict[str, int]] = {}
    for key in ("faces", "lines", "points"):
        bucket = usage.get(key, {})
        if not isinstance(bucket, dict):
            normalized[key] = {}
            continue
        normalized[key] = {str(color): int(count) for color, count in sorted(bucket.items())}
    return normalized


def _validate_glb_order_sensitive_primitives(path: Path, doc: Dict[str, Any]) -> list[str]:
    errors: list[str] = []
    meshes = doc.get("meshes", [])
    if not isinstance(meshes, list):
        return [f"{path}: meshes is not a list"]
    # The converter deliberately emits order-sensitive primitives instead of
    # compact material batches. Per-kind monotonic source indices are the cheap
    # invariant that catches accidental optimizer/editor rewrites which would be
    # harmless for most modern meshes but can break this game's coplanar planes,
    # z-fighting decals, line antennas, and color-overlap behavior.
    previous_by_kind = {"triangle": -1, "lines": -1, "points": -1}
    for mesh in meshes:
        if not isinstance(mesh, dict):
            continue
        for primitive_index, prim in enumerate(mesh.get("primitives", [])):
            if not isinstance(prim, dict):
                continue
            extras = prim.get("extras", {})
            if not isinstance(extras, dict):
                errors.append(f"{path}: primitive {primitive_index} missing source extras")
                continue
            if extras.get("source_order_sensitive") is not True:
                errors.append(f"{path}: primitive {primitive_index} is not marked order-sensitive")
            kind = extras.get("source_primitive_kind")
            if kind not in {"triangle", "lines", "points"}:
                errors.append(f"{path}: primitive {primitive_index} has invalid source kind {kind!r}")
            if kind in previous_by_kind:
                try:
                    source_index = int(extras.get("source_primitive_index", -1))
                except (TypeError, ValueError):
                    source_index = -1
                if source_index < 0:
                    errors.append(f"{path}: {kind} primitive {primitive_index} missing source index")
                elif source_index <= previous_by_kind[kind]:
                    errors.append(
                        f"{path}: {kind} primitive {primitive_index} source index is not monotonic "
                        f"({source_index} after {previous_by_kind[kind]})"
                    )
                previous_by_kind[kind] = source_index
    return errors


def _validate_3d3_json_index(
    json_path: Path,
    original_payload: Dict[str, Any],
    original_bytes: bytes,
    require_all: bool,
    loadability_only: bool,
) -> tuple[int, int]:
    """Validate .3D3 JSON slot metadata and optional full-byte rebuild data."""

    if not json_path.exists():
        if require_all:
            print(f"missing .3D3 JSON index: {json_path}", file=sys.stderr)
            return 0, 1
        return 0, 0

    try:
        payload = json.loads(json_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"{json_path}: {exc}", file=sys.stderr)
        return 1, 1

    checked = 1
    failed = 0
    if payload.get("format") != "3D3":
        print(f"{json_path}: expected format=3D3", file=sys.stderr)
        failed += 1

    actual_offsets = payload.get("shape_offsets")
    expected_offsets = original_payload.get("shape_offsets", [])
    if not isinstance(actual_offsets, list) or not all(isinstance(v, int) for v in actual_offsets):
        print(f"{json_path}: shape_offsets must be a list of integers", file=sys.stderr)
        failed += 1
    elif not loadability_only:
        message = compare_sequence_exact(
            str(json_path),
            ".3D3 shape offset table",
            [int(v) for v in actual_offsets],
            [int(v) for v in expected_offsets],
        )
        if message:
            print(message, file=sys.stderr)
            failed += 1

    actual_model_size = payload.get("model_data_size")
    expected_model_size = int(original_payload.get("model_data_size", 0))
    if not isinstance(actual_model_size, int) or actual_model_size < 0:
        print(f"{json_path}: model_data_size must be a non-negative integer", file=sys.stderr)
        failed += 1
    elif not loadability_only:
        message = compare_count_value(
            str(json_path),
            ".3D3 model_data_size",
            int(actual_model_size),
            expected_model_size,
            "json",
            "original",
        )
        if message:
            print(message, file=sys.stderr)
            failed += 1

    if isinstance(actual_offsets, list) and isinstance(actual_model_size, int):
        previous = -1
        for idx, value in enumerate(actual_offsets):
            if not isinstance(value, int):
                continue
            if value < previous:
                print(f"{json_path}: shape_offsets[{idx}] is not monotonic", file=sys.stderr)
                failed += 1
                break
            if value > actual_model_size:
                print(f"{json_path}: shape_offsets[{idx}] exceeds model_data_size", file=sys.stderr)
                failed += 1
                break
            previous = value

    if "model_data" in payload:
        try:
            rebuilt = build_3d3(payload)
        except Exception as exc:
            print(f"{json_path}: failed to rebuild full .3D3 JSON: {exc}", file=sys.stderr)
            failed += 1
        else:
            if not rebuilt:
                print(f"{json_path}: rebuilt .3D3 JSON is empty", file=sys.stderr)
                failed += 1
            elif not loadability_only:
                message = compare_byte_sequence(
                    str(json_path),
                    original_bytes,
                    rebuilt,
                    "original",
                    "json",
                    "rebuilt .3D3 differs",
                )
                if message:
                    print(message, file=sys.stderr)
                    failed += 1
    return checked, failed


def _validate_3d3_json_loadability(json_path: Path) -> tuple[int, int]:
    """Validate a source-free/minimized .3D3 JSON index without original bytes."""

    try:
        payload = json.loads(json_path.read_text(encoding="utf-8"))
    except Exception as exc:
        print(f"{json_path}: {exc}", file=sys.stderr)
        return 1, 1

    failed = 0
    if payload.get("format") != "3D3":
        print(f"{json_path}: expected format=3D3", file=sys.stderr)
        failed += 1

    actual_offsets = payload.get("shape_offsets")
    actual_model_size = payload.get("model_data_size")
    if not isinstance(actual_offsets, list) or not all(isinstance(v, int) for v in actual_offsets):
        print(f"{json_path}: shape_offsets must be a list of integers", file=sys.stderr)
        failed += 1
    if not isinstance(actual_model_size, int) or actual_model_size < 0:
        print(f"{json_path}: model_data_size must be a non-negative integer", file=sys.stderr)
        failed += 1

    if isinstance(actual_offsets, list) and isinstance(actual_model_size, int):
        previous = -1
        for idx, value in enumerate(actual_offsets):
            if not isinstance(value, int):
                continue
            if value < previous:
                print(f"{json_path}: shape_offsets[{idx}] is not monotonic", file=sys.stderr)
                failed += 1
                break
            if value > actual_model_size:
                print(f"{json_path}: shape_offsets[{idx}] exceeds model_data_size", file=sys.stderr)
                failed += 1
                break
            previous = value

    if "model_data" in payload:
        try:
            rebuilt = build_3d3(payload)
        except Exception as exc:
            print(f"{json_path}: failed to rebuild full .3D3 JSON: {exc}", file=sys.stderr)
            failed += 1
        else:
            if not rebuilt:
                print(f"{json_path}: rebuilt .3D3 JSON is empty", file=sys.stderr)
                failed += 1

    return 1, failed


def _validate_glb_loadability(path: Path) -> tuple[int, int]:
    try:
        doc = _read_glb_json(path)
        counts = _glb_primitive_counts(doc)
    except Exception as exc:
        print(f"{path}: {exc}", file=sys.stderr)
        return 1, 1
    if (
        counts.get("triangles", 0) <= 0
        and counts.get("lines", 0) <= 0
        and counts.get("points", 0) <= 0
    ):
        print(f"{path}: GLB contains no triangles, lines, or points", file=sys.stderr)
        return 1, 1
    return 1, 0


def _validate_glmesh_loadability(path: Path) -> tuple[int, int]:
    try:
        counts = _glmesh_primitive_counts(path.read_bytes())
    except Exception as exc:
        print(f"{path}: {exc}", file=sys.stderr)
        return 1, 1
    if (
        counts.get("triangles", 0) <= 0
        and counts.get("lines", 0) <= 0
        and counts.get("points", 0) <= 0
    ):
        print(f"{path}: GLMESH contains no triangles, lines, or points", file=sys.stderr)
        return 1, 1
    return 1, 0


def validate_3d3_glb_replacements(
    input_root: Path,
    output_root: Path,
    recursive: bool,
    require_all: bool,
    iter_asset_paths: Callable[[Path, bool], list[Path]],
    detect_format: Callable[[Path], str],
    asset_output_dir: Callable[[Path, Path, Path, Path, str], Path],
    load_shape_names_for_3d3: Callable[[Path], Dict[str, str]],
    require_generated_cache: bool,
    require_source_proof: bool,
    allow_custom_glb_differences: bool,
    loadability_only: bool,
) -> tuple[int, int]:
    checked = 0
    failed = 0
    checked_json_paths: set[Path] = set()
    checked_glb_paths: set[Path] = set()
    checked_glmesh_paths: set[Path] = set()

    if input_root.exists():
        for src in iter_asset_paths(input_root, recursive=recursive):
            fmt = detect_format(src)
            if fmt != "3D3":
                continue

            relative = src.relative_to(input_root)
            output_base = asset_output_dir(src, relative, input_root, output_root, fmt) / src.name
            legacy_base = output_root / relative
            combined_glb = _first_existing_path(
                output_base.with_name(output_base.name + ".glb"),
                legacy_base.with_name(legacy_base.name + ".glb"),
            )
            shape_output_dir = combined_glb.parent if combined_glb.exists() else output_base.parent
            data = src.read_bytes()
            payload = parse_3d3(data)
            payload["shape_names"] = load_shape_names_for_3d3(src)
            json_path = _first_existing_path(
                output_base.with_name(output_base.name + ".json"),
                legacy_base.with_name(legacy_base.name + ".json"),
            )
            json_checked, json_failed = _validate_3d3_json_index(
                json_path,
                payload,
                data,
                require_all,
                loadability_only,
            )
            if json_path.exists():
                checked_json_paths.add(json_path.resolve())
            checked += json_checked
            failed += json_failed
            expected_shapes = export_3d3_shape_gltfs(payload)
            stale_root_glmesh = sorted(shape_output_dir.glob("shape_*.glmesh"))
            if stale_root_glmesh:
                for stale_path in stale_root_glmesh:
                    print(
                        f"{stale_path}: stale runtime mesh cache outside cache/ directory",
                        file=sys.stderr,
                    )
                failed += len(stale_root_glmesh)

            if not combined_glb.exists():
                if require_source_proof and not loadability_only:
                    print(f"missing replacement GLB: {combined_glb}", file=sys.stderr)
                    failed += 1
            else:
                checked += 1
                checked_glb_paths.add(combined_glb.resolve())
                try:
                    glb = _read_glb_json(combined_glb)
                except Exception as exc:
                    print(f"{combined_glb}: {exc}", file=sys.stderr)
                    failed += 1
                else:
                    extras = glb.get("extras", {})
                    if not isinstance(extras, dict) or extras.get("format") != "3D3":
                        message = f"{combined_glb}: missing 3D3 extras metadata"
                        if require_source_proof and not loadability_only:
                            print(message, file=sys.stderr)
                            failed += 1
                        elif not loadability_only:
                            print(f"warning: {message}", file=sys.stderr)
                    mesh_count = len(glb.get("meshes", [])) if isinstance(glb.get("meshes"), list) else 0
                    if mesh_count != len(expected_shapes):
                        message = (
                            f"{combined_glb}: mesh count differs from renderable shape count "
                            f"(glb={mesh_count}, expected={len(expected_shapes)})"
                        )
                        if require_source_proof and not loadability_only:
                            print(message, file=sys.stderr)
                            failed += 1
                        elif not loadability_only:
                            print(f"warning: {message}", file=sys.stderr)

            for shape_index, shape_name, expected_shape_gltf in expected_shapes:
                label = _safe_output_stem(shape_name)
                base_shape = f"shape_{shape_index:03d}"
                base = base_shape if label == base_shape else f"{base_shape}_{label}"
                shape_glb = shape_output_dir / f"{base}.glb"
                if not shape_glb.exists():
                    if require_all:
                        print(f"missing per-shape GLB: {shape_glb}", file=sys.stderr)
                        failed += 1
                    continue
                checked += 1
                checked_glb_paths.add(shape_glb.resolve())
                try:
                    shape_doc = _read_glb_json(shape_glb)
                except Exception as exc:
                    print(f"{shape_glb}: {exc}", file=sys.stderr)
                    failed += 1
                    continue
                shape_extras = shape_doc.get("extras", {})
                if not isinstance(shape_extras, dict):
                    message = f"{shape_glb}: missing extras metadata"
                    if require_source_proof and not loadability_only:
                        print(message, file=sys.stderr)
                        failed += 1
                    elif not loadability_only:
                        print(f"warning: {message}", file=sys.stderr)
                    shape_extras = {}
                try:
                    glb_shape_index = int(shape_extras.get("source_shape_index", -1))
                except (TypeError, ValueError):
                    glb_shape_index = -1
                if glb_shape_index != int(shape_index):
                    message = (
                        f"{shape_glb}: source_shape_index mismatch "
                        f"(glb={shape_extras.get('source_shape_index')}, expected={shape_index})"
                    )
                    if require_source_proof and not loadability_only:
                        print(message, file=sys.stderr)
                        failed += 1
                    elif not loadability_only:
                        print(f"warning: {message}", file=sys.stderr)
                if shape_extras.get("minimized_per_shape_glb") is not True:
                    message = f"{shape_glb}: per-shape GLB is not marked as minimized"
                    if require_source_proof and not loadability_only:
                        print(message, file=sys.stderr)
                        failed += 1
                    elif not loadability_only:
                        print(f"warning: {message}", file=sys.stderr)
                meshes = shape_doc.get("meshes", [])
                nodes = shape_doc.get("nodes", [])
                if not isinstance(meshes, list) or len(meshes) != 1:
                    print(f"{shape_glb}: expected exactly one mesh", file=sys.stderr)
                    failed += 1
                if not isinstance(nodes, list) or len(nodes) != 1:
                    print(f"{shape_glb}: expected exactly one node", file=sys.stderr)
                    failed += 1
                primitive_errors = _validate_glb_order_sensitive_primitives(shape_glb, shape_doc)
                if primitive_errors and not loadability_only:
                    for error in primitive_errors:
                        if require_source_proof:
                            print(error, file=sys.stderr)
                        else:
                            print(f"warning: {error}", file=sys.stderr)
                    if require_source_proof:
                        failed += len(primitive_errors)
                expected_counts = _glb_primitive_counts(shape_doc)
                if (
                    expected_counts.get("triangles", 0) <= 0
                    and expected_counts.get("lines", 0) <= 0
                    and expected_counts.get("points", 0) <= 0
                ):
                    print(f"{shape_glb}: per-shape GLB contains no triangles, lines, or points", file=sys.stderr)
                    failed += 1
                legacy_generated_counts = _glb_primitive_counts(expected_shape_gltf)
                if not loadability_only:
                    message = compare_mapping_exact(
                        str(shape_glb),
                        "GLB primitive counts",
                        expected_counts,
                        legacy_generated_counts,
                        "glb",
                        "original",
                    )
                    if message:
                        if allow_custom_glb_differences:
                            print(f"warning: {message}", file=sys.stderr)
                        else:
                            print(message, file=sys.stderr)
                            failed += 1
                # This compares compact source identity metadata, not duplicated
                # geometry in JSON. It catches source primitive drops/reordering/color
                # loss in an unmodified conversion while still allowing customizers to
                # edit the GLB itself as the source of truth.
                if not loadability_only:
                    legacy_generated_meta = _glb_source_meta(expected_shape_gltf)
                    glb_meta_from_source = _glb_source_meta(shape_doc)
                    message = compare_sequence_exact(
                        str(shape_glb),
                        "GLB source primitive metadata",
                        glb_meta_from_source,
                        legacy_generated_meta,
                    )
                    if message:
                        if require_source_proof and not allow_custom_glb_differences:
                            print(message, file=sys.stderr)
                            failed += 1
                        else:
                            print(f"warning: {message}", file=sys.stderr)
                    color_message = compare_mapping_exact(
                        str(shape_glb),
                        "GLB raw face/line/point color usage",
                        _glb_raw_color_usage(shape_doc),
                        _glb_raw_color_usage(expected_shape_gltf),
                        "glb",
                        "original",
                    )
                    if color_message:
                        if require_source_proof and not allow_custom_glb_differences:
                            print(color_message, file=sys.stderr)
                            failed += 1
                        else:
                            print(f"warning: {color_message}", file=sys.stderr)
    
                shape_glmesh = shape_output_dir / "cache" / f"{base}.glmesh"
                if not shape_glmesh.exists():
                    if require_generated_cache:
                        print(f"missing per-shape runtime mesh cache: {shape_glmesh}", file=sys.stderr)
                        failed += 1
                    continue
                checked += 1
                checked_glmesh_paths.add(shape_glmesh.resolve())
                try:
                    expected_glmesh = glb_to_glmesh_bytes(shape_glb)
                    actual_glmesh = shape_glmesh.read_bytes()
                    actual_counts = _glmesh_primitive_counts(actual_glmesh)
                    glb_meta = _glb_source_meta(shape_doc)
                    glmesh_meta = _glmesh_source_meta(actual_glmesh)
                    stream_error = _compare_glmesh_streams(expected_glmesh, actual_glmesh)
                except Exception as exc:
                    print(f"{shape_glmesh}: {exc}", file=sys.stderr)
                    failed += 1
                    continue
                if stream_error:
                    print(f"{shape_glmesh}: runtime mesh stream differs from source GLB: {stream_error}", file=sys.stderr)
                    failed += 1
                if glmesh_meta:
                    message = compare_sequence_exact(
                        str(shape_glmesh),
                        "source primitive metadata",
                        glmesh_meta,
                        glb_meta,
                    )
                else:
                    message = None
                if message:
                    if require_source_proof:
                        print(message, file=sys.stderr)
                        failed += 1
                    else:
                        print(f"warning: {message}", file=sys.stderr)
                if not glmesh_meta:
                    message = f"{shape_glmesh}: runtime mesh cache does not preserve source primitive metadata"
                    if require_source_proof:
                        print(message, file=sys.stderr)
                        failed += 1
                    else:
                        print(f"warning: {message}", file=sys.stderr)
                for count_key in ("triangles", "lines", "points"):
                    count_error = compare_count_value(
                        str(shape_glmesh),
                        count_key,
                        actual_counts[count_key],
                        expected_counts[count_key],
                        "glmesh",
                        "glb",
                    )
                    if count_error:
                        print(count_error, file=sys.stderr)
                        failed += 1
                if actual_glmesh != expected_glmesh:
                    print(f"{shape_glmesh}: runtime mesh cache differs from source GLB", file=sys.stderr)
                    failed += 1
    
    if loadability_only:
        for json_path in sorted(output_root.rglob("*.json")):
            if json_path.resolve() in checked_json_paths:
                continue
            if not json_path.name.upper().endswith(".3D3.JSON"):
                continue
            extra_checked, extra_failed = _validate_3d3_json_loadability(json_path)
            checked += extra_checked
            failed += extra_failed
        for glb_path in sorted(output_root.rglob("*.glb")):
            if glb_path.resolve() in checked_glb_paths:
                continue
            extra_checked, extra_failed = _validate_glb_loadability(glb_path)
            checked += extra_checked
            failed += extra_failed
        for glmesh_path in sorted(output_root.rglob("*.glmesh")):
            if glmesh_path.resolve() in checked_glmesh_paths:
                continue
            extra_checked, extra_failed = _validate_glmesh_loadability(glmesh_path)
            checked += extra_checked
            failed += extra_failed

    return checked, failed
