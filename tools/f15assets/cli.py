#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Dict, Optional

try:
    from tools.f15assets.f15assets import (
        decode_pic_asset,
        decode_title640_pic_asset,
        parse_3d3,
        export_3d3_shape_gltfs,
        export_3d3_to_gltf,
        export_3d3_to_glb,
        export_3d3_gltf_to_glb,
        parse_3dg,
        parse_3dt,
        parse_wld,
        build_3d3,
        build_3dg,
        build_3dt,
        build_wld,
        export_fonts,
        export_sounds,
    )
    from tools.f15assets.f15assets.pic import known_runtime_pic_palette, to_png_data
    from tools.f15assets.f15assets.sounds import DEFAULT_SAMPLE_RATE
    from tools.f15assets.f15assets.io import from_base64, to_base64
    from tools.f15assets.f15assets.validation_image import validate_pic_png_replacement, validate_png_replacement_loadability
    from tools.f15assets.f15assets.validation_sound import validate_sound_replacements
    from tools.f15assets.f15assets.validation_font import validate_font_replacements
    from tools.f15assets.f15assets.validation_structured import validate_structured_json_replacements
    from tools.f15assets.f15assets.validation_model3d import (
        glb_to_glmesh_bytes,
        validate_3d3_glb_replacements,
    )
except ModuleNotFoundError:
    from f15assets import (
        decode_pic_asset,
        decode_title640_pic_asset,
        parse_3d3,
        export_3d3_shape_gltfs,
        export_3d3_to_gltf,
        export_3d3_to_glb,
        export_3d3_gltf_to_glb,
        parse_3dg,
        parse_3dt,
        parse_wld,
        build_3d3,
        build_3dg,
        build_3dt,
        build_wld,
        export_fonts,
        export_sounds,
    )
    from f15assets.pic import known_runtime_pic_palette, to_png_data
    from f15assets.sounds import DEFAULT_SAMPLE_RATE
    from f15assets.io import from_base64, to_base64
    from f15assets.validation_image import validate_pic_png_replacement, validate_png_replacement_loadability
    from f15assets.validation_sound import validate_sound_replacements
    from f15assets.validation_font import validate_font_replacements
    from f15assets.validation_structured import validate_structured_json_replacements
    from f15assets.validation_model3d import (
        glb_to_glmesh_bytes,
        validate_3d3_glb_replacements,
    )

ASSET_EXT_FORMATS = {
    ".pic": "PIC",
    ".spr": "PIC",
    ".3d3": "3D3",
    ".3dt": "3DT",
    ".3dg": "3DG",
    ".wld": "WLD",
}

THEATER_WLD_STEM_ALIASES = {
    "LB": "LIBYA",
    "PG": "GULF",
}

F117_PIC_PALETTE_ALIASES = {
    "256LEFT": "FLIGHT.PAL",
    "256PIT": "FLIGHT.PAL",
    "256REAR": "FLIGHT.PAL",
    "256RIGHT": "FLIGHT.PAL",
    "CLIMBIN": "ADV.PAL",
}

# Some tests and external scripts imported this helper from cli.py before the
# per-format validator split. Keep the alias while new code uses the public name.
_glb_to_glmesh_bytes = glb_to_glmesh_bytes


def _iter_asset_paths(root: Path, recursive: bool):
    """Yield supported game asset files beneath the requested source path."""
    root = root
    if not root.exists():
        return []

    if recursive:
        return sorted(
            path
            for path in root.rglob("*")
            if path.is_file() and _detect_format(path)
        )

    return sorted(
        path for path in root.iterdir() if path.is_file() and _detect_format(path)
    )


def _read_binary(path: Path) -> bytes:
    """Read binary from the asset representation."""
    with path.open("rb") as f:
        return f.read()


def _write_json(path: Path, payload: Dict[str, Any], pretty: bool = True, *, media_sidecar: bool = True) -> None:
    # Default converter sidecars are media-first: for 3D3, GLB files are the
    # editable source and JSON is minimized. Full bridge dumps deliberately opt
    # out so runtime can rebuild the legacy byte stream without original .3D3.
    """Write json in the requested modern representation."""
    writable_payload = _sidecar_payload_for_write(payload) if media_sidecar else payload
    ordered_payload = _human_ordered_payload(writable_payload)
    with path.open("w", encoding="utf-8") as f:
        if pretty:
            json.dump(ordered_payload, f, indent=2)
            f.write("\n")
        else:
            json.dump(ordered_payload, f)


def _sidecar_payload_for_write(payload: Dict[str, Any]) -> Dict[str, Any]:
    """Prepare decoded metadata for a deterministic human-readable sidecar."""
    if payload.get("format") == "PIC":
        out = {
            key: payload[key]
            for key in (
                "format",
                "version",
                "source_name",
                "decoded_width",
                "decoded_height",
                "max_lzw_width",
                "bitstream_mode",
                "palette_profile",
                "original_length",
                "decoded_length",
                "stored_height",
                "layout",
                "notes",
            )
            if key in payload
        }
        out["image"] = {
            "preferred": "png",
            "authoritative": True,
            "notes": "Pixel data, dimensions, and palette from the matching indexed PNG override JSON metadata.",
        }
        return out

    if payload.get("format") != "3D3":
        return payload

    # GLB is the preferred model replacement/editing artifact. Keep the JSON
    # sidecar as a small index/manifest instead of duplicating model bytecode.
    out = {
        key: payload[key]
        for key in (
            "format",
            "version",
            "signature",
            "shape_offsets",
            "model_data_size",
            "shape_names",
        )
        if key in payload
    }
    shared_pool = payload.get("shared_vertex_pool")
    if isinstance(shared_pool, dict):
        out["shared_vertex_pool"] = {
            "index_count": len(shared_pool.get("x_indices", [])),
            "x_value_count": len(shared_pool.get("x_values", [])),
            "y_value_count": len(shared_pool.get("y_values", [])),
            "z_value_count": len(shared_pool.get("z_values", [])),
        }
    else:
        out["shared_vertex_pool"] = None
    out["geometry"] = {
        "preferred": "glb",
        "authoritative": True,
        "combined": True,
        "per_shape_files": True,
        "notes": "3D geometry and GLB extras override JSON metadata; JSON is only an index.",
    }
    return out


_LARGE_JSON_KEYS = {
    "compressed_payload_base64",
    "model_data",
    "name_table",
    "palette_dac6_base64",
    "palette_rgb8_base64",
    "pixels_base64",
    "raw_bytes_base64",
    "shape_target_category_table",
    "terrain_grid",
    "trailing_bytes",
    "unknown_bytes_base64",
    "kill_tally_or_unit_flags",
    "mission_object_type_table",
}

_JSON_KEY_ORDER = {
    "WLD": [
        "format",
        "version",
        "terrain_target_ids",
        "read_item_size",
        "ground_unit_count",
        "world_object_count",
        "world_objects",
        "flight_unit_count",
        "flight_units",
        "name_strings",
    ],
    "3D3": [
        "format",
        "version",
        "signature",
        "shape_offsets",
        "shape_names",
        "model_data_size",
        "shared_vertex_pool",
    ],
}

_RECORD_KEY_ORDER = [
    "name",
    "label",
    "unitRef",
    "unitType",
    "objectIdx",
    "x_coord",
    "y_coord",
    "z_coord",
    "startX",
    "startY",
    "startZ",
    "waypointX",
    "waypointY",
    "waypointZ",
    "targetFlags",
    "occupantType",
    "patrolCount",
    "waypointIdx",
    "flags",
    "weaponType",
    "terrainColor",
    "damage",
]


def _is_large_json_key(key: object) -> bool:
    """Return whether large json key."""
    text = str(key)
    return text in _LARGE_JSON_KEYS or text.endswith("_base64")


def _ordered_human_mapping(payload: Dict[str, Any], preferred: list[str] | None = None) -> Dict[str, Any]:
    """Order metadata keys so editable fields precede opaque preservation data."""
    preferred = preferred or []
    out: Dict[str, Any] = {}
    seen = set()

    for key in preferred:
        if key in payload:
            out[key] = _human_ordered_payload(payload[key])
            seen.add(key)

    normal_keys = [
        key for key in payload.keys()
        if key not in seen and not _is_large_json_key(key)
    ]
    large_keys = [
        key for key in payload.keys()
        if key not in seen and _is_large_json_key(key)
    ]
    for key in normal_keys + large_keys:
        out[key] = _human_ordered_payload(payload[key])
    return out


def _human_ordered_payload(value: Any) -> Any:
    """Recursively reorder metadata for stable, readable JSON output."""
    if isinstance(value, list):
        return [_human_ordered_payload(item) for item in value]
    if not isinstance(value, dict):
        return value

    fmt = value.get("format")
    if isinstance(fmt, str) and fmt in _JSON_KEY_ORDER:
        return _ordered_human_mapping(value, _JSON_KEY_ORDER[fmt])

    return _ordered_human_mapping(value, _RECORD_KEY_ORDER)


def _write_gltf(path: Path, payload: Dict[str, Any], pretty: bool = True) -> None:
    """Write gltf in the requested modern representation."""
    with path.open("w", encoding="utf-8") as f:
        if pretty:
            json.dump(payload, f, indent=2, sort_keys=True)
            f.write("\n")
        else:
            json.dump(payload, f)


def _write_gltf_binary(path: Path, payload: Dict[str, Any]) -> None:
    """Write gltf binary in the requested modern representation."""
    glb_data = export_3d3_to_glb(payload)
    path.write_bytes(glb_data)


def _write_gltf_binary_doc(path: Path, gltf: Dict[str, Any]) -> None:
    """Serialize a glTF document and binary payload into one GLB container."""
    glb_data = export_3d3_gltf_to_glb(gltf)
    path.write_bytes(glb_data)


def _remove_if_exists(path: Path) -> None:
    """Implement the remove if exists asset-processing operation."""
    if path.exists():
        try:
            path.unlink()
        except OSError:
            pass


def _detect_format(path: Path) -> str:
    """Identify an asset format from its extension and binary signature."""
    return ASSET_EXT_FORMATS.get(path.suffix.lower(), "")


def _safe_output_stem(value: object) -> str:
    """Return a portable output stem."""
    text = str(value or "").strip()
    safe = "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in text)
    safe = "_".join(part for part in safe.split("_") if part)
    return safe[:96] or "model"


def _related_world_stem_for_theater_asset(source_path: Path) -> str:
    """Map a theater-specific asset name to its owning world directory."""
    stem = source_path.stem
    upper_stem = stem.upper()
    wld_stem = THEATER_WLD_STEM_ALIASES.get(upper_stem, upper_stem)
    if (source_path.with_name(f"{wld_stem}.WLD")).exists() or (
        source_path.with_name(f"{wld_stem}.wld")
    ).exists():
        return wld_stem
    return stem


def _asset_output_dir(src: Path, relative: Path, input_root: Path, output_root: Path, fmt: str) -> Path:
    """Choose the self-contained output directory for one source asset."""
    parent = output_root / relative.parent
    if fmt == "WLD":
        return parent / src.stem
    if fmt in {"3D3", "3DT", "3DG"}:
        return parent / _related_world_stem_for_theater_asset(src)
    return output_root / relative.with_suffix("")


def _write_3d3_shape_models(
    output_dir: Path,
    payload: Dict[str, Any],
    model_format: str,
    pretty: bool,
    write_cache: bool = False,
) -> None:
    """Export each decoded 3D3 shape as an independently editable GLB file."""
    if model_format == "none":
        return
    output_dir.mkdir(parents=True, exist_ok=True)
    for stale_glmesh in output_dir.glob("shape_*.glmesh"):
        _remove_if_exists(stale_glmesh)
    cache_dir = output_dir / "cache"
    if model_format == "glb" and cache_dir.exists():
        for stale_glmesh in cache_dir.glob("shape_*.glmesh"):
            _remove_if_exists(stale_glmesh)
    for shape_index, shape_name, shape_gltf in export_3d3_shape_gltfs(payload):
        label = _safe_output_stem(shape_name)
        base_shape = f"shape_{shape_index:03d}"
        base = base_shape if label == base_shape else f"{base_shape}_{label}"
        if model_format == "glb":
            glb_path = output_dir / f"{base}.glb"
            _write_gltf_binary_doc(glb_path, shape_gltf)
            if write_cache:
                cache_dir.mkdir(parents=True, exist_ok=True)
                (cache_dir / f"{base}.glmesh").write_bytes(glb_to_glmesh_bytes(glb_path))
        elif model_format == "gltf":
            _write_gltf(output_dir / f"{base}.gltf", shape_gltf, pretty=pretty)


def _log_3d3_gltf_diagnostics(gltf: Dict[str, Any], source_path: Path | None) -> None:
    """Report suspicious or skipped 3D3 shape slots without aborting conversion."""
    label = str(source_path) if source_path is not None else "3D3"
    skipped = gltf.get("extras", {}).get("skipped_shapes", [])
    if isinstance(skipped, list):
        for item in skipped:
            if not isinstance(item, dict):
                continue
            meta = item.get("shape_payload")
            if not isinstance(meta, dict):
                meta = {}
            reason = (
                meta.get("render_error")
                or meta.get("edge_decode_error")
                or "no_renderable_primitives"
            )
            print(
                "warning: "
                f"{label}: skipped shape {item.get('shape_index')} "
                f"{item.get('shape_name')} "
                f"offset {item.get('shape_offset')}..{item.get('shape_end')} "
                f"render_mode={item.get('render_mode')} "
                f"reason={reason} "
                f"metadata={json.dumps(meta, sort_keys=True)}",
                file=sys.stderr,
            )

    raw_color_usage = gltf.get("extras", {}).get("raw_color_usage", {})
    point_counts = {}
    if isinstance(raw_color_usage, dict) and isinstance(raw_color_usage.get("points"), dict):
        point_counts = raw_color_usage["points"]
    if point_counts:
        print(
            "info: "
            f"{label}: exported degenerate source lines as POINTS "
            f"colors={json.dumps(point_counts, sort_keys=True)}",
            file=sys.stderr,
        )


def _dac6_to_rgb8(data: bytes) -> bytes:
    """Implement the dac6 to rgb8 asset-processing operation."""
    return bytes(((value & 0x3F) << 2) | ((value & 0x3F) >> 4) for value in data[:768])


def _find_external_palette(source_path: Path) -> tuple[Path, bytes, str] | None:
    """Locate external palette using the converter search rules."""
    same_stem = source_path.with_suffix(".PAL")
    if same_stem.exists() and same_stem.stat().st_size >= 768:
        return same_stem, same_stem.read_bytes()[:768], "same_stem_pal"

    lower_stem = source_path.with_suffix(".pal")
    if lower_stem.exists() and lower_stem.stat().st_size >= 768:
        return lower_stem, lower_stem.read_bytes()[:768], "same_stem_pal"

    alias_name = F117_PIC_PALETTE_ALIASES.get(source_path.stem.upper())
    if alias_name:
        alias_path = source_path.with_name(alias_name)
        if alias_path.exists() and alias_path.stat().st_size >= 768:
            return alias_path, alias_path.read_bytes()[:768], "f117_known_pic_palette_alias"
        alias_path = source_path.with_name(alias_name.lower())
        if alias_path.exists() and alias_path.stat().st_size >= 768:
            return alias_path, alias_path.read_bytes()[:768], "f117_known_pic_palette_alias"

    palette_table = source_path.with_name("PALETTES.PAL")
    if palette_table.exists() and palette_table.stat().st_size >= 768:
        return palette_table, palette_table.read_bytes()[:768], "f117_palettes_pal_chunk0"

    palette_table = source_path.with_name("palettes.pal")
    if palette_table.exists() and palette_table.stat().st_size >= 768:
        return palette_table, palette_table.read_bytes()[:768], "f117_palettes_pal_chunk0"

    return None


def _attach_external_palette(payload: Dict[str, Any], source_path: Path | None) -> None:
    """Attach the best matching external palette to an indexed image asset."""
    if source_path is None:
        return
    profile = payload.get("palette_profile")
    if isinstance(profile, dict) and profile.get("status") == "embedded":
        return

    index_bit_depth = 8
    if isinstance(profile, dict):
        try:
            index_bit_depth = int(profile.get("index_bit_depth", 8))
        except (TypeError, ValueError):
            index_bit_depth = 8
    known_palette = known_runtime_pic_palette(source_path.name, index_bit_depth)
    if known_palette is not None:
        payload["palette_dac6_base64"] = to_base64(bytes(known_palette["palette_dac6"]))
        payload["palette_rgb8_base64"] = to_base64(bytes(known_palette["palette_rgb8"]))
        payload["palette_profile"] = known_palette["profile"]
        return

    found = _find_external_palette(source_path)
    if found is None:
        return

    palette_path, palette_dac6, source = found
    payload["palette_dac6_base64"] = to_base64(palette_dac6)
    payload["palette_rgb8_base64"] = to_base64(_dac6_to_rgb8(palette_dac6))
    payload["palette_profile"] = {
        "source": source,
        # Store the palette filename, not the user's absolute install path, so
        # converted image sidecars remain shareable across machines.
        "source_file": palette_path.name,
        "status": "external",
        "index_mode": "indexed",
        "index_bit_depth": payload.get("palette_profile", {}).get("index_bit_depth", 8)
        if isinstance(payload.get("palette_profile"), dict)
        else 8,
        "color_mode": "palette_index",
        "palette_format": "vga_dac_6bit_rgb_triples",
        "notes": "External 768-byte VGA DAC palette applied during PNG export.",
    }


def _decode_asset(
    data: bytes,
    fmt: str,
    args: argparse.Namespace,
    source_path: Path | None = None,
) -> Dict[str, Any]:
    """Decode one legacy asset and return normalized metadata plus media outputs."""
    if args.png and fmt != "PIC":
        raise ValueError("--png is only supported for PIC/SPR inputs")

    if args.gltf and fmt != "3D3":
        raise ValueError("--gltf is only supported for 3D3 inputs")

    if fmt == "PIC":
        is_title640 = source_path is not None and source_path.name.upper() == "TITLE640.PIC"
        if is_title640:
            payload = decode_title640_pic_asset(
                data, source_name=source_path.name if source_path is not None else None
            )
        else:
            payload = decode_pic_asset(
                data, source_name=source_path.name if source_path is not None else None
            )
        _attach_external_palette(payload, source_path)
        if args.png:
            pixels = from_base64(payload["pixels_base64"])
            index_bit_depth = 8
            try:
                index_bit_depth = int(payload.get("palette_profile", {}).get("index_bit_depth", 8))
            except (TypeError, ValueError):
                index_bit_depth = 8
            palette = None
            palette_rgb8 = payload.get("palette_rgb8_base64")
            if isinstance(palette_rgb8, str):
                palette = list(from_base64(palette_rgb8))
            image = to_png_data(
                pixels,
                width=int(payload.get("decoded_width", 320)),
                height=int(payload.get("decoded_height", 200)),
                index_bit_depth=index_bit_depth,
                palette=palette,
            )
            image.save(args.png)
        return payload

    if fmt == "3D3":
        payload = parse_3d3(data)
        if source_path is not None:
            payload["shape_names"] = _load_shape_names_for_3d3(source_path)
        if getattr(args, "gltf", None):
            gltf_path = Path(args.gltf)
            if gltf_path.suffix.lower() not in {".gltf", ".glb"}:
                raise ValueError("--gltf expects a .gltf or .glb output path")
            gltf = export_3d3_to_gltf(payload)
            _log_3d3_gltf_diagnostics(gltf, source_path)
            if gltf_path.suffix.lower() == ".glb":
                _write_gltf_binary_doc(gltf_path, gltf)
            else:
                _write_gltf(gltf_path, gltf, pretty=args.pretty)
        return payload

    if fmt == "3DT":
        return parse_3dt(data)

    if fmt == "3DG":
        return parse_3dg(data)

    if fmt == "WLD":
        return parse_wld(data)

    raise ValueError(f"unsupported format: {fmt}")


def _load_shape_names_for_3d3(source_path: Path) -> Dict[str, str]:
    """Load stable shape names associated with a 3D3 model table."""
    stem = source_path.stem.upper()
    wld_stem = THEATER_WLD_STEM_ALIASES.get(stem, stem)
    wld_path = source_path.with_name(f"{wld_stem}.WLD")
    if not wld_path.exists():
        wld_path = source_path.with_name(f"{wld_stem}.wld")
    if not wld_path.exists():
        return {}

    try:
        wld = parse_wld(_read_binary(wld_path))
    except Exception:
        return {}

    names = wld.get("name_strings", [])
    if not isinstance(names, list):
        return {}

    shape_names: Dict[str, str] = {}
    for obj in wld.get("world_objects", []):
        try:
            object_name_index = int(obj.get("objectIdx", 0)) & 0x7F
        except (AttributeError, TypeError, ValueError):
            continue
        if object_name_index >= len(names):
            continue
        name = str(names[object_name_index] or "").strip()
        if name:
            shape_names.setdefault(str(object_name_index), name)
    return shape_names


def _decode_args(png: str | None = None, gltf: str | None = None, pretty: bool = False) -> argparse.Namespace:
    """Translate command-line decode options into decoder keyword arguments."""
    return argparse.Namespace(png=png, gltf=gltf, pretty=pretty)


def _minimize_3d3_json(payload: Dict[str, Any]) -> Dict[str, Any]:
    """Remove geometry duplicated by per-shape GLB files from a 3D3 sidecar."""
    shared_pool = payload.get("shared_vertex_pool")
    shared_pool_summary = None
    if isinstance(shared_pool, dict):
        shared_pool_summary = {
            "index_count": len(shared_pool.get("x_indices", [])),
            "x_value_count": len(shared_pool.get("x_values", [])),
            "y_value_count": len(shared_pool.get("y_values", [])),
            "z_value_count": len(shared_pool.get("z_values", [])),
        }
    minimized = {
        "format": payload.get("format", "3D3"),
        "version": payload.get("version", 1),
        "signature": payload.get("signature"),
        "shape_offsets": payload.get("shape_offsets", []),
        "shape_names": payload.get("shape_names", {}),
        "model_data_size": payload.get("model_data_size"),
        "shared_vertex_pool_summary": shared_pool_summary,
        "trailing_byte_count": len(from_base64(payload.get("trailing_bytes", "")))
        if isinstance(payload.get("trailing_bytes"), str)
        else 0,
        "notes": (
            "Minimized 3D3 index. GLB files are authoritative for geometry; "
            "use decode or convert-tree --include-3d3-model-data for a full "
            "reverse-engineering dump with model_data."
        ),
    }
    return minimized


def cmd_decode(args: argparse.Namespace) -> int:
    """Implement the single-asset decode command."""
    fmt = args.format.upper() if args.format else _detect_format(Path(args.input))
    if not fmt:
        raise ValueError("cannot detect format from extension; pass --format")

    data = _read_binary(Path(args.input))
    payload = _decode_asset(data, fmt, args, source_path=Path(args.input))
    _write_json(Path(args.json), payload, pretty=args.pretty, media_sidecar=False)
    return 0


def cmd_convert_tree(args: argparse.Namespace) -> int:
    """Implement recursive conversion of a game asset tree."""
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        if not getattr(args, "loadability_only", False):
            raise ValueError(f"input must be an existing directory: {args.input}")
        if getattr(args, "require_all", False):
            raise ValueError("--require-all needs an existing original input directory; omit it for source-free --loadability-only checks")
        print(
            f"warning: original input directory is missing: {args.input}; running source-free loadability checks only",
            file=sys.stderr,
        )

    output_root.mkdir(parents=True, exist_ok=True)

    sources = _iter_asset_paths(input_root, recursive=args.recursive)
    if not sources:
        raise ValueError("no supported asset files found")

    failed = 0
    for src in sources:
        fmt = _detect_format(src)
        if not fmt:
            continue

        relative = src.relative_to(input_root)
        dst_base = output_root / relative.with_suffix("")
        output_base = dst_base
        if fmt in {"3D3", "WLD", "3DT", "3DG"}:
            # Keep complex/gameplay-bearing formats isolated so their sidecars,
            # combined model, and per-shape model files are easy to browse and edit.
            output_base = _asset_output_dir(src, relative, input_root, output_root, fmt) / src.name

        output_base.parent.mkdir(parents=True, exist_ok=True)

        if fmt in {"3D3", "WLD", "3DT", "3DG"}:
            json_path = output_base.with_name(output_base.name + ".json")
        else:
            json_path = output_base.with_suffix(".json")
        json_path.parent.mkdir(parents=True, exist_ok=True)
        png_path = None
        if fmt == "PIC" and not args.no_png:
            png_path = str(output_base.with_suffix(".png"))

        gltf_path = None
        if fmt == "3D3":
            if args.models == "gltf":
                gltf_path = str(output_base.with_name(output_base.name + ".gltf"))
            elif args.models == "glb":
                gltf_path = str(output_base.with_name(output_base.name + ".glb"))

        decode_args = _decode_args(
            png=png_path,
            gltf=gltf_path,
            pretty=args.pretty,
        )

        try:
            payload = _decode_asset(_read_binary(src), fmt, decode_args, source_path=src)
        except Exception as exc:
            failed += 1
            if not args.continue_on_error:
                raise
            print(f"error: {exc} ({src})", file=sys.stderr)
            continue

        write_json_sidecar = fmt != "PIC" or png_path is None or getattr(args, "include_image_json", False)
        if write_json_sidecar:
            json_payload = payload
            if fmt == "3D3" and not getattr(args, "include_3d3_model_data", False):
                json_payload = _minimize_3d3_json(payload)
            _write_json(
                json_path,
                json_payload,
                pretty=args.pretty,
                media_sidecar=not (fmt == "3D3" and getattr(args, "include_3d3_model_data", False)),
            )
        else:
            _remove_if_exists(json_path)
        _remove_if_exists(json_path.with_suffix(".yaml"))

        if fmt == "3D3":
            _write_3d3_shape_models(
                output_base.parent,
                payload,
                args.models,
                args.pretty,
                write_cache=getattr(args, "include_glmesh_cache", False),
            )
            legacy_exts = [".json", ".yaml", ".gltf", ".glb", ".glmesh"]
            for legacy_ext in legacy_exts:
                _remove_if_exists(dst_base.with_suffix(legacy_ext))
        elif fmt in {"WLD", "3DT", "3DG"}:
            for legacy_ext in [".json", ".yaml"]:
                _remove_if_exists(dst_base.with_suffix(legacy_ext))

    return 0 if failed == 0 else 1


def cmd_export_fonts(args: argparse.Namespace) -> int:
    """Implement export of legacy bitmap fonts to editable modern files."""
    repo_root = Path(args.repo_root)
    output_root = Path(args.output)
    if not (repo_root / "src" / "fontdata.h").exists():
        raise ValueError(f"repo root does not contain src/fontdata.h: {repo_root}")
    index = export_fonts(
        repo_root,
        output_root,
        write_bdf=not args.no_bdf,
        include_metadata=getattr(args, "include_metadata", False),
    )
    index_path = output_root / "fonts.json"
    if getattr(args, "include_metadata", False):
        _write_json(index_path, index, pretty=args.pretty)
    else:
        _remove_if_exists(index_path)
    return 0


def cmd_export_sounds(args: argparse.Namespace) -> int:
    """Implement export of digitized cues and music streams."""
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        raise ValueError(f"input must be an existing directory: {args.input}")
    index = export_sounds(
        input_root,
        output_root,
        sample_rate=args.sample_rate,
        include_raw_blob=getattr(args, "include_raw_blob", False),
        include_metadata=getattr(args, "include_metadata", False),
    )
    if getattr(args, "include_metadata", False):
        _write_json(output_root / "sounds.json", index, pretty=args.pretty)
    return 0


def cmd_build_binary(args: argparse.Namespace) -> int:
    """Implement reconstruction of a legacy binary from editable metadata."""
    json_path = Path(args.json)
    payload = json.loads(json_path.read_text(encoding="utf-8"))
    fmt = args.format.upper() if args.format else str(payload.get("format", "")).upper()
    builders = {
        "3D3": build_3d3,
        "WLD": build_wld,
        "3DT": build_3dt,
        "3DG": build_3dg,
    }
    if fmt not in builders:
        raise ValueError("build-binary currently supports 3D3, WLD, 3DT, and 3DG JSON")
    sys.stdout.buffer.write(builders[fmt](payload))
    return 0




















def _structured_json_path(src: Path, relative: Path, input_root: Path, output_root: Path, fmt: str) -> Path:
    """Implement the structured json path asset-processing operation."""
    if fmt in {"WLD", "3D3", "3DT", "3DG"}:
        output_base = _asset_output_dir(src, relative, input_root, output_root, fmt) / src.name
        return output_base.with_name(output_base.name + ".json")
    return (output_root / relative).with_suffix(".json")






























def cmd_build_glmesh(args: argparse.Namespace) -> int:
    """Implement generation of the runtime GL mesh cache from a GLB model."""
    sys.stdout.buffer.write(glb_to_glmesh_bytes(Path(args.glb)))
    return 0




def cmd_validate_replacements(args: argparse.Namespace) -> int:
    """Implement exhaustive legacy-versus-replacement asset validation."""
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        if not args.loadability_only:
            raise ValueError(f"input must be an existing directory: {args.input}")
        if args.require_all:
            raise ValueError("--require-all needs an existing original input directory; omit it for source-free --loadability-only checks")
        print(
            f"warning: original input directory is missing: {args.input}; running source-free loadability checks only",
            file=sys.stderr,
        )
    if output_root.exists() and output_root.is_dir() and output_root.name != "converted_assets_all" and (output_root / "converted_assets_all").is_dir():
        output_root = output_root / "converted_assets_all"
        print(f"using converted output directory: {output_root}", file=sys.stderr)
    if not output_root.exists() or not output_root.is_dir():
        raise ValueError(f"output must be an existing directory: {args.output}")
    if args.loadability_only and args.strict_original_proof:
        raise ValueError("--loadability-only and --strict-original-proof are incompatible validation modes")
    if args.loadability_only and args.require_source_proof:
        raise ValueError("--loadability-only and --require-source-proof are incompatible validation modes")
    if args.loadability_only and args.allow_custom_glb_differences:
        raise ValueError("--loadability-only already allows custom GLB content differences; do not combine it with --allow-custom-glb-differences")

    checked = 0
    failed = 0
    pic_checked = 0
    checked_png_paths: set[Path] = set()
    if input_root.exists():
        for src in _iter_asset_paths(input_root, recursive=args.recursive):
            fmt = _detect_format(src)
            if fmt != "PIC":
                continue
            relative = src.relative_to(input_root)
            png_path = output_root / relative.with_suffix(".png")
            if not png_path.exists():
                if args.require_all:
                    print(f"missing replacement PNG: {png_path}", file=sys.stderr)
                    failed += 1
                continue
            checked += 1
            pic_checked += 1
            checked_png_paths.add(png_path.resolve())
            try:
                errors = validate_pic_png_replacement(src, png_path, _read_binary, args.loadability_only)
            except Exception as exc:
                errors = [f"{src}: {exc}"]
            if errors:
                failed += 1
                for error in errors:
                    print(error, file=sys.stderr)
    if args.loadability_only:
        for png_path in sorted(output_root.rglob("*.png")):
            if png_path.resolve() in checked_png_paths:
                continue
            if "fonts" in {part.lower() for part in png_path.relative_to(output_root).parts[:-1]}:
                continue
            checked += 1
            pic_checked += 1
            errors = validate_png_replacement_loadability(png_path)
            if errors:
                failed += 1
                for error in errors:
                    print(error, file=sys.stderr)

    sound_checked, sound_failed = validate_sound_replacements(input_root, output_root, args.require_all, args.loadability_only)
    checked += sound_checked
    failed += sound_failed
    font_checked, font_failed = validate_font_replacements(Path(args.repo_root), output_root, args.require_all, args.loadability_only)
    checked += font_checked
    failed += font_failed
    structured_checked, structured_failed = validate_structured_json_replacements(
        input_root,
        output_root,
        args.recursive,
        args.require_all,
        _iter_asset_paths,
        _detect_format,
        _structured_json_path,
        args.loadability_only,
    )
    checked += structured_checked
    failed += structured_failed
    glb_checked, glb_failed = validate_3d3_glb_replacements(
        input_root,
        output_root,
        args.recursive,
        args.require_all,
        _iter_asset_paths,
        _detect_format,
        _asset_output_dir,
        _load_shape_names_for_3d3,
        args.require_generated_cache or args.strict_original_proof,
        args.require_source_proof or args.strict_original_proof,
        args.allow_custom_glb_differences and not args.strict_original_proof,
        args.loadability_only,
    )
    checked += glb_checked
    failed += glb_failed

    if args.loadability_only and checked == 0:
        print(
            f"warning: no replacement files were checked under {output_root}; expected PNG, WAV, BDF, WLD/3DT/3DG JSON, 3D3 JSON, GLB, or GLMESH replacements",
            file=sys.stderr,
        )

    print(
        f"validated replacements: PIC/SPR PNG={pic_checked}, "
        f"sound WAV={sound_checked}, font BDF/PNG={font_checked}, "
        f"WLD/3DT/3DG JSON={structured_checked}, 3D3 JSON/GLB/GLMESH={glb_checked}; failures={failed}"
    )
    return 0 if failed == 0 else 1


def _repo_root_from_tool() -> Path:
    """Implement the repo root from tool asset-processing operation."""
    return Path(__file__).resolve().parents[2]


def cmd_convert_all(args: argparse.Namespace) -> int:
    """Convert every supported asset using only source and destination directories."""
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        raise ValueError(f"input must be an existing directory: {args.input}")

    output_root.mkdir(parents=True, exist_ok=True)

    convert_args = argparse.Namespace(
        input=str(input_root),
        output=str(output_root),
        recursive=True,
        models="glb",
        no_png=False,
        include_image_json=getattr(args, "include_image_json", False),
        include_3d3_model_data=getattr(args, "include_3d3_model_data", True),
        include_glmesh_cache=getattr(args, "include_glmesh_cache", False),
        continue_on_error=False,
        pretty=args.pretty,
    )
    result = cmd_convert_tree(convert_args)
    if result != 0:
        return result

    repo_root = _repo_root_from_tool()
    font_args = argparse.Namespace(
        repo_root=str(repo_root),
        output=str(output_root / "fonts"),
        no_bdf=False,
        include_metadata=getattr(args, "include_metadata", False),
        pretty=args.pretty,
    )
    result = cmd_export_fonts(font_args)
    if result != 0:
        return result

    sound_args = argparse.Namespace(
        input=str(input_root),
        output=str(output_root / "sounds"),
        sample_rate=DEFAULT_SAMPLE_RATE,
        include_raw_blob=getattr(args, "include_raw_blob", False),
        include_metadata=getattr(args, "include_metadata", False),
        pretty=args.pretty,
    )
    return cmd_export_sounds(sound_args)


def build_parser() -> argparse.ArgumentParser:
    """Build the command-line parser and register all asset-tool subcommands."""
    parser = argparse.ArgumentParser(
        description="F-15 asset converter (forward-only: game binary -> modern editable outputs)"
    )
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output")
    sub = parser.add_subparsers(dest="command", required=True)

    convert_all = sub.add_parser(
        "convert-all",
        help="convert game assets, in-repo fonts, and digitized sounds with default settings",
    )
    convert_all.add_argument("input", help="Installed game folder containing original assets")
    convert_all.add_argument("output", help="Output folder for all converted assets")
    convert_all.add_argument(
        "--include-image-json",
        action="store_true",
        help="Also write bulky PIC/SPR decode JSON; default output uses PNG as the image source",
    )
    convert_all.add_argument(
        "--include-3d3-model-data",
        action="store_true",
        default=True,
        help="Write .3D3 model_data base64 into JSON so modern-only packs can rebuild shape tables (default)",
    )
    convert_all.add_argument(
        "--minimize-3d3-json",
        dest="include_3d3_model_data",
        action="store_false",
        help="Omit bulky .3D3 model_data; requires original .3D3 files at runtime for shape tables",
    )
    convert_all.add_argument(
        "--include-glmesh-cache",
        action="store_true",
        help="Also pre-generate cache/*.glmesh runtime meshes; default output relies on runtime rebuild from GLB",
    )
    convert_all.add_argument(
        "--include-metadata",
        action="store_true",
        help="Also export optional font JSON, sound cue JSON, driver sidecars, and index metadata",
    )
    convert_all.add_argument(
        "--include-raw-blob",
        action="store_true",
        help="Also export f15dgtl_raw.wav/f15dgtl_raw.json for reverse-engineering reference",
    )
    convert_all.set_defaults(func=cmd_convert_all)

    decode = sub.add_parser(
        "decode",
        help="decode one game asset to full diagnostic JSON and optional media files",
    )
    decode.add_argument("input", help="Input binary asset")
    decode.add_argument("json", help="Output JSON sidecar")
    decode.add_argument(
        "--format",
        choices=["PIC", "PIC".lower(), "3D3", "3d3", "3DT", "3dt", "3DG", "3dg", "WLD", "wld"],
        help="Force input format",
    )
    decode.add_argument(
        "--png",
        help="Optional indexed PNG output for PIC/SPR (unsupported for other formats)",
    )
    decode.add_argument(
        "--gltf",
        help="Optional .gltf or .glb output for 3D3 models",
    )
    decode.set_defaults(func=cmd_decode)

    convert = sub.add_parser(
        "convert-tree",
        help="convert supported assets in one folder; use convert-all for recursive default conversion",
    )
    convert.add_argument("input", help="Input folder containing game assets")
    convert.add_argument("output", help="Output folder for converted assets")
    convert.add_argument("--recursive", action="store_true", help="Recurse into subdirectories")
    convert.add_argument(
        "--models",
        choices=["gltf", "glb", "none"],
        default="glb",
        help="Output format for 3D3 models (`glb` is self-contained by default)",
    )
    convert.add_argument("--no-png", action="store_true", help="Skip PNG output for PIC/SPR")
    convert.add_argument(
        "--include-image-json",
        action="store_true",
        help="Also write bulky PIC/SPR decode JSON; by default PNG is the image replacement source",
    )
    convert.add_argument(
        "--include-3d3-model-data",
        action="store_true",
        help="Also write bulky .3D3 model_data base64 into JSON; by default GLB is the geometry source",
    )
    convert.add_argument(
        "--include-glmesh-cache",
        action="store_true",
        help="Also pre-generate cache/*.glmesh runtime meshes; default output relies on runtime rebuild from GLB",
    )
    convert.add_argument(
        "--continue-on-error",
        action="store_true",
        help="Continue conversion when a file fails",
    )
    convert.set_defaults(func=cmd_convert_tree)

    fonts = sub.add_parser(
        "export-fonts",
        help="export extracted in-repo font tables to BDF; metadata is optional",
    )
    fonts.add_argument("repo_root", help="Repository root containing src/fontdata.h and src/gfx_impl.c")
    fonts.add_argument("output", help="Output folder for BDF font assets")
    fonts.add_argument("--no-bdf", action="store_true", help="Skip BDF font files")
    fonts.add_argument(
        "--include-metadata",
        action="store_true",
        help="Also write font_<id>.json sidecars and fonts.json; default output is BDF only",
    )
    fonts.set_defaults(func=cmd_export_fonts)

    sounds = sub.add_parser(
        "export-sounds",
        help="export digitized cue WAVs; optional metadata sidecars are opt-in",
    )
    sounds.add_argument("input", help="Installed game folder containing F15DGTL.BIN and sound drivers")
    sounds.add_argument("output", help="Output folder for sound assets")
    sounds.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help="Sample rate for exported unsigned 8-bit PCM cue WAVs",
    )
    sounds.add_argument(
        "--include-raw-blob",
        action="store_true",
        help="Also export f15dgtl_raw.wav/f15dgtl_raw.json for reverse-engineering reference",
    )
    sounds.add_argument(
        "--include-metadata",
        action="store_true",
        help="Also export cue JSON, driver sidecars, and sounds.json metadata for reverse engineering",
    )
    sounds.set_defaults(func=cmd_export_sounds)

    build_binary = sub.add_parser(
        "build-binary",
        help="rebuild runtime binary bytes from structured replacement JSON",
    )
    build_binary.add_argument("json", help="Input 3D3/WLD/3DT/3DG JSON replacement")
    build_binary.add_argument(
        "--format",
        choices=["3D3", "3d3", "WLD", "wld", "3DT", "3dt", "3DG", "3dg"],
        help="Force JSON format instead of reading payload.format",
    )
    build_binary.set_defaults(func=cmd_build_binary)

    build_glmesh = sub.add_parser(
        "build-glmesh",
        help="expand a replacement GLB into a simple runtime mesh stream for the OpenGL backend",
    )
    build_glmesh.add_argument("glb", help="Input per-shape GLB")
    build_glmesh.set_defaults(func=cmd_build_glmesh)

    validate = sub.add_parser(
        "validate-replacements",
        help="compare original asset loads against modern replacements; use flags for strict proof or custom GLB workflows",
    )
    validate.add_argument("input", help="Installed game folder containing original assets")
    validate.add_argument("output", help="Converted asset folder")
    validate.add_argument(
        "--repo-root",
        default=".",
        help="Repository root for validating in-repo original font tables",
    )
    validate.add_argument(
        "--no-recursive",
        dest="recursive",
        action="store_false",
        default=True,
        help="Only validate assets directly under the input folder",
    )
    validate.add_argument(
        "--require-all",
        action="store_true",
        help="Fail if a supported original asset has no modern authoring replacement",
    )
    validate.add_argument(
        "--require-generated-cache",
        action="store_true",
        help="Also fail if generated 3D cache/*.glmesh runtime caches are missing",
    )
    validate.add_argument(
        "--require-source-proof",
        action="store_true",
        help="Fail if GLB/GLMESH source primitive metadata is missing or no longer matches the original export",
    )
    validate.add_argument(
        "--strict-original-proof",
        action="store_true",
        help="Strict converter-regression mode: require generated 3D caches and GLB/GLMESH source proof metadata",
    )
    validate.add_argument(
        "--allow-custom-glb-differences",
        action="store_true",
        help="Warn instead of failing when per-shape GLB geometry/source proof differs from the original .3D3 export",
    )
    validate.add_argument(
        "--loadability-only",
        action="store_true",
        help="Validate modern files parse and have required structure, but skip equality checks against original assets",
    )
    validate.set_defaults(func=cmd_validate_replacements)

    return parser


def main(argv: list[str] | None = None) -> int:
    """Run the asset-tool command-line entry point."""
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:  # pragma: no cover
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
