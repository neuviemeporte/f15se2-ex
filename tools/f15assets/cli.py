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
        export_3d3_to_gltf,
        export_3d3_to_glb,
        parse_3dg,
        parse_3dt,
        parse_wld,
        export_fonts,
        export_sounds,
    )
    from tools.f15assets.f15assets.pic import to_png_data
    from tools.f15assets.f15assets.io import from_base64
except ModuleNotFoundError:
    from f15assets import (
        decode_pic_asset,
        decode_title640_pic_asset,
        parse_3d3,
        export_3d3_to_gltf,
        export_3d3_to_glb,
        parse_3dg,
        parse_3dt,
        parse_wld,
        export_fonts,
        export_sounds,
    )
    from f15assets.pic import to_png_data
    from f15assets.io import from_base64

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


def _iter_asset_paths(root: Path, recursive: bool):
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
    with path.open("rb") as f:
        return f.read()


def _read_yaml(path: Path) -> Dict[str, Any]:
    try:
        import yaml
    except Exception as exc:  # pragma: no cover
        raise RuntimeError("PyYAML is required for YAML support") from exc

    with path.open("r", encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        raise ValueError("YAML payload must be a mapping")
    return data


def _write_json(path: Path, payload: Dict[str, Any], pretty: bool = True) -> None:
    with path.open("w", encoding="utf-8") as f:
        if pretty:
            json.dump(payload, f, indent=2, sort_keys=True)
            f.write("\n")
        else:
            json.dump(payload, f)


def _write_gltf(path: Path, payload: Dict[str, Any], pretty: bool = True) -> None:
    with path.open("w", encoding="utf-8") as f:
        if pretty:
            json.dump(payload, f, indent=2, sort_keys=True)
            f.write("\n")
        else:
            json.dump(payload, f)


def _write_gltf_binary(path: Path, payload: Dict[str, Any]) -> None:
    glb_data = export_3d3_to_glb(payload)
    path.write_bytes(glb_data)


def _write_yaml(path: Path, payload: Dict[str, Any]) -> None:
    try:
        import yaml
    except Exception as exc:  # pragma: no cover
        raise RuntimeError("PyYAML is required for --yaml output") from exc

    with path.open("w", encoding="utf-8") as f:
        yaml.safe_dump(payload, f, sort_keys=True)


def _detect_format(path: Path) -> str:
    return ASSET_EXT_FORMATS.get(path.suffix.lower(), "")


def _decode_asset(
    data: bytes,
    fmt: str,
    args: argparse.Namespace,
    source_path: Path | None = None,
) -> Dict[str, Any]:
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
            if gltf_path.suffix.lower() == ".glb":
                _write_gltf_binary(gltf_path, payload)
            else:
                _write_gltf(gltf_path, export_3d3_to_gltf(payload), pretty=args.pretty)
        return payload

    if fmt == "3DT":
        return parse_3dt(data)

    if fmt == "3DG":
        return parse_3dg(data)

    if fmt == "WLD":
        return parse_wld(data)

    raise ValueError(f"unsupported format: {fmt}")


def _load_shape_names_for_3d3(source_path: Path) -> Dict[str, str]:
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
    return argparse.Namespace(png=png, gltf=gltf, pretty=pretty)


def cmd_decode(args: argparse.Namespace) -> int:
    fmt = args.format.upper() if args.format else _detect_format(Path(args.input))
    if not fmt:
        raise ValueError("cannot detect format from extension; pass --format")

    data = _read_binary(Path(args.input))
    payload = _decode_asset(data, fmt, args, source_path=Path(args.input))
    _write_json(Path(args.json), payload, pretty=args.pretty)
    if args.yaml:
        _write_yaml(Path(args.yaml), payload)
    return 0


def cmd_convert_tree(args: argparse.Namespace) -> int:
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        raise ValueError(f"input must be an existing directory: {args.input}")

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
        dst_base.parent.mkdir(parents=True, exist_ok=True)
        output_base = dst_base
        if fmt == "3D3":
            # Keep the original extension in the basename, e.g. 15FLT.3D3.glb,
            # so similarly named model records from different formats do not
            # collide in bulk exports.
            output_base = dst_base.with_name(src.name)

        json_path = output_base.with_suffix(".json")
        yaml_path = output_base.with_suffix(".yaml")
        png_path = None
        if fmt == "PIC" and not args.no_png:
            png_path = str(output_base.with_suffix(".png"))

        gltf_path = None
        if fmt == "3D3":
            if args.models == "gltf":
                gltf_path = str(output_base.with_suffix(".gltf"))
            elif args.models == "glb":
                gltf_path = str(output_base.with_suffix(".glb"))

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

        _write_json(json_path, payload, pretty=args.pretty)
        if args.yaml:
            _write_yaml(yaml_path, payload)

        if fmt == "3D3":
            legacy_exts = [".json", ".yaml", ".gltf", ".glb"]
            for legacy_ext in legacy_exts:
                legacy_path = dst_base.with_suffix(legacy_ext)
                if legacy_path.exists():
                    try:
                        legacy_path.unlink()
                    except OSError:
                        pass

    return 0 if failed == 0 else 1


def cmd_export_fonts(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root)
    output_root = Path(args.output)
    if not (repo_root / "src" / "fontdata.h").exists():
        raise ValueError(f"repo root does not contain src/fontdata.h: {repo_root}")
    index = export_fonts(repo_root, output_root, write_bdf=not args.no_bdf)
    _write_json(output_root / "fonts.json", index, pretty=args.pretty)
    return 0


def cmd_export_sounds(args: argparse.Namespace) -> int:
    input_root = Path(args.input)
    output_root = Path(args.output)
    if not input_root.exists() or not input_root.is_dir():
        raise ValueError(f"input must be an existing directory: {args.input}")
    index = export_sounds(input_root, output_root, sample_rate=args.sample_rate)
    _write_json(output_root / "sounds.json", index, pretty=args.pretty)
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="F-15 asset converter (forward-only: game binary -> modern editable outputs)"
    )
    parser.add_argument("--pretty", action="store_true", help="Pretty-print JSON output")
    sub = parser.add_subparsers(dest="command", required=True)

    decode = sub.add_parser(
        "decode",
        help="decode game asset to modern editable sidecar files",
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
    decode.add_argument("--yaml", help="Optional YAML output sidecar path")
    decode.add_argument(
        "--gltf",
        help="Optional .gltf or .glb output for 3D3 models",
    )
    decode.set_defaults(func=cmd_decode)

    convert = sub.add_parser(
        "convert-tree",
        help="convert all supported assets in a folder to modern editable targets",
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
    convert.add_argument("--yaml", action="store_true", help="Also write YAML sidecar files")
    convert.add_argument(
        "--continue-on-error",
        action="store_true",
        help="Continue conversion when a file fails",
    )
    convert.set_defaults(func=cmd_convert_tree)

    fonts = sub.add_parser(
        "export-fonts",
        help="export extracted in-repo font tables to atlas PNG and JSON metadata",
    )
    fonts.add_argument("repo_root", help="Repository root containing src/fontdata.h and src/gfx_impl.c")
    fonts.add_argument("output", help="Output folder for font atlas assets")
    fonts.add_argument("--no-bdf", action="store_true", help="Skip BDF font files")
    fonts.set_defaults(func=cmd_export_fonts)

    sounds = sub.add_parser(
        "export-sounds",
        help="export digitized sample blob and sound-driver sidecars",
    )
    sounds.add_argument("input", help="Installed game folder containing F15DGTL.BIN and sound drivers")
    sounds.add_argument("output", help="Output folder for sound assets")
    sounds.add_argument(
        "--sample-rate",
        type=int,
        default=7850,
        help="Sample rate for raw unsigned 8-bit PCM WAV export",
    )
    sounds.set_defaults(func=cmd_export_sounds)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args))
    except Exception as exc:  # pragma: no cover
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
