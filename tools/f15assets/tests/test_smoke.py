from __future__ import annotations

import base64
import pathlib
import json
import struct
import tempfile
import unittest

from f15assets import decode_pic_asset, encode_pic_asset, export_3d3_to_gltf, export_3d3_to_glb
from f15assets import parse_3d3, build_3d3, parse_3dg, build_3dg, parse_3dt, build_3dt, parse_wld, build_wld
from tools.f15assets import cli as cli_module


class SmokeConvertersTest(unittest.TestCase):
    def test_pic_roundtrip_byte_mode(self):
        pixels = bytes(((i * 7) % 256) for i in range(320 * 200))
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 12,
            "bitstream_mode": "byte",
            "pixels_base64": base64.b64encode(pixels).decode("ascii"),
        }

        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        out = base64.b64decode(decoded["pixels_base64"].encode("ascii"))
        self.assertEqual(out[: len(pixels)], pixels)

    def test_pic_roundtrip_nibble_mode(self):
        nibble_pixels = bytes(((i * 5) & 0x0F) for i in range(320 * 200))
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 11,
            "bitstream_mode": "nibble",
            "pixels_base64": base64.b64encode(nibble_pixels).decode("ascii"),
        }

        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        out = base64.b64decode(decoded["pixels_base64"].encode("ascii"))
        self.assertEqual(out[: len(nibble_pixels)], nibble_pixels)

    def test_pic_roundtrip_preserves_compressed_payload(self):
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 12,
            "bitstream_mode": "byte",
            "pixels_base64": base64.b64encode(bytes(((i * 3) % 256) for i in range(320 * 200))).decode("ascii"),
        }

        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        rebuilt = encode_pic_asset(decoded)
        self.assertEqual(encoded, rebuilt)

    def test_pic_sidecar_includes_palette_profile(self):
        payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 12,
            "bitstream_mode": "byte",
            "pixels_base64": base64.b64encode(bytes((i & 0xFF) for i in range(320 * 200))).decode("ascii"),
        }
        encoded = encode_pic_asset(payload)
        decoded = decode_pic_asset(encoded)
        self.assertEqual(decoded["format"], "PIC")
        palette_profile = decoded.get("palette_profile")
        self.assertIsInstance(palette_profile, dict)
        self.assertEqual(palette_profile.get("status"), "not_embedded")
        self.assertEqual(palette_profile.get("index_mode"), "indexed")
        self.assertEqual(palette_profile.get("index_bit_depth"), 8)

        nibble_payload = {
            "format": "PIC",
            "decoded_width": 320,
            "decoded_height": 200,
            "max_lzw_width": 11,
            "bitstream_mode": "nibble",
            "pixels_base64": base64.b64encode(bytes(((i * 3) & 0x0F) for i in range(320 * 200))).decode("ascii"),
        }
        nibble_encoded = encode_pic_asset(nibble_payload)
        nibble_decoded = decode_pic_asset(nibble_encoded)
        palette_profile = nibble_decoded.get("palette_profile")
        self.assertIsInstance(palette_profile, dict)
        self.assertEqual(palette_profile.get("status"), "not_embedded")
        self.assertEqual(palette_profile.get("index_bit_depth"), 4)

    def test_cli_rejects_invalid_flag_combinations(self):
        sample_pixels = bytes((i & 0xFF) for i in range(320 * 200))
        pic_bytes = encode_pic_asset(
            {
                "format": "PIC",
                "decoded_width": 320,
                "decoded_height": 200,
                "max_lzw_width": 12,
                "bitstream_mode": "byte",
                "pixels_base64": base64.b64encode(sample_pixels).decode("ascii"),
            }
        )
        shape_payload = build_3d3(
            {
                "format": "3D3",
                "shape_offsets": [0],
                "model_data": "",
                "model_data_size": 0,
                "trailing_bytes": "",
            }
        )

        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            pic_path = base / "sample.pic"
            grid_path = base / "sample.3D3"
            table_path = base / "sample.3DT"

            pic_path.write_bytes(pic_bytes)
            grid_path.write_bytes(shape_payload)
            table_path.write_bytes(
                build_3dt(
                    {
                        "format": "3DT",
                        "version": 1,
                        "levels": [
                            {"level": 0, "objects": []},
                            {"level": 1, "objects": []},
                            {"level": 2, "objects": []},
                            {"level": 3, "objects": []},
                            {"level": 4, "objects": []},
                        ],
                    }
                )
            )

            rc = cli_module.main(
                ["decode", str(grid_path), str(base / "out.json"), "--png", str(base / "out.png")]
            )
            self.assertEqual(rc, 1)

            rc = cli_module.main(
                [
                    "decode",
                    str(pic_path),
                    str(base / "out2.json"),
                    "--gltf",
                    str(base / "out2.gltf"),
                ]
            )
            self.assertEqual(rc, 1)

            rc = cli_module.main(
                [
                    "decode",
                    str(table_path),
                    str(base / "out3dt.json"),
                    "--png",
                    str(base / "out3dt.png"),
                ]
            )
            self.assertEqual(rc, 1)

    def test_3dt_roundtrip(self):
        payload = {
            "format": "3DT",
            "version": 1,
            "levels": [
                {
                    "level": 0,
                    "objects": [
                        {
                            "tile_index": 0,
                            "objects": [{"x": 10, "y": 20, "z": -5, "shape_word": 0x12FF}],
                        }
                    ],
                },
                {"level": 1, "objects": []},
                {"level": 2, "objects": []},
                {"level": 3, "objects": []},
                {"level": 4, "objects": []},
            ],
            "trailing_bytes": base64.b64encode(b"tail").decode("ascii"),
        }
        encoded = build_3dt(payload)
        parsed = parse_3dt(encoded)
        self.assertEqual(parsed["tile_counts"][0], 1)
        self.assertEqual(parsed["levels"][0]["objects"][0]["objects"][0]["shape_word"], 0x12FF)
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_3dt(parsed), encoded)

    def test_3dg_roundtrip(self):
        payload = {
            "format": "3DG",
            "version": 1,
            "grid1": [0] * 16,
            "grid2": [0x11] * 256,
            "grid3": [0x22] * 512,
            "grid4": [0x33] * 512,
            "grid5": [0x44] * 512,
            "trailing_bytes": base64.b64encode(b"tail").decode("ascii"),
        }
        encoded = build_3dg(payload)
        parsed = parse_3dg(encoded)
        self.assertEqual(parsed["grid1"][0], 0)
        self.assertEqual(parsed["grid2"][0], 0x11)
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_3dg(parsed), encoded)

    def test_3d3_roundtrip_without_tail(self):
        payload = {
            "format": "3D3",
            "shape_offsets": [0, 6, 12],
            "model_data": base64.b64encode(b"\x00" * 12 + b"abc").decode("ascii"),
            "model_data_size": 15,
            "shared_vertex_pool": None,
            "trailing_bytes": "",
        }
        encoded = build_3d3(payload)
        parsed = parse_3d3(encoded)
        self.assertEqual(parsed["shape_offsets"], [0, 6, 12])
        self.assertEqual(build_3d3(parsed), encoded)

    def test_wld_roundtrip(self):
        payload = {
            "format": "WLD",
            "unknown_header": base64.b64encode(b"\x00\x00").decode("ascii"),
            "read_item_size": 2,
            "ground_unit_count": 0,
            "world_object_count": 1,
            "world_objects": [
                {
                    "unitRef": 1,
                    "x_coord": 100,
                    "y_coord": 200,
                    "unitType": 3,
                    "targetFlags": 0,
                    "occupantType": 4,
                    "patrolCount": 0,
                    "objectIdx": 7,
                },
                {
                    "unitRef": 2,
                    "x_coord": 300,
                    "y_coord": 400,
                    "unitType": 4,
                    "targetFlags": 1,
                    "occupantType": 5,
                    "patrolCount": 2,
                    "objectIdx": 8,
                },
            ],
            "flight_unit_count": 1,
            "flight_units": [
                {
                    "waypointIdx": 1,
                    "x": 10,
                    "y": 11,
                    "altitude": 12,
                    "xPrecise": 123,
                    "yPrecise": -456,
                    "heading": 0x1234,
                    "pitch": -2,
                    "roll": 3,
                    "planeType": 4,
                    "flags": 0,
                    "maxSpeed": 200,
                    "fuel": 999,
                    "reserved": base64.b64encode(b"\x00\x01\x02\x03\x04\x05").decode("ascii"),
                }
            ],
            "wld_buf7": base64.b64encode(b"A" * 100).decode("ascii"),
            "wld_buf8": base64.b64encode(b"B" * 100).decode("ascii"),
            "object_type_table": base64.b64encode(b"C" * 100).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
            "name_table": base64.b64encode(b"name_one\x00name_two\x00" + b"\x00" * 739).decode("ascii"),
            "trailing_bytes": base64.b64encode(b"wld-tail").decode("ascii"),
        }
        encoded = build_wld(payload)
        parsed = parse_wld(encoded)
        self.assertEqual(base64.b64decode(parsed["wld_buf7"].encode("ascii"))[:3], b"AAA")
        self.assertEqual(parsed["flight_units"][0]["xPrecise"], 123)
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_wld(parsed), encoded)

    def test_wld_roundtrip_with_short_name_table(self):
        payload = {
            "format": "WLD",
            "unknown_header": base64.b64encode(b"\x11\x22").decode("ascii"),
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "wld_buf7": base64.b64encode(b"\x01" * 100).decode("ascii"),
            "wld_buf8": base64.b64encode(b"\x02" * 100).decode("ascii"),
            "object_type_table": base64.b64encode(b"\x03" * 100).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
            "name_table": base64.b64encode(b"JP\x00WLD\x00").decode("ascii"),
            "trailing_bytes": base64.b64encode(b"alt-trailing").decode("ascii"),
        }
        encoded = build_wld(payload)
        parsed = parse_wld(encoded)
        self.assertEqual(parsed["name_table"], payload["name_table"])
        self.assertEqual(parsed["name_strings"], ["JP", "WLD"])
        self.assertEqual(parsed["trailing_bytes"], payload["trailing_bytes"])
        self.assertEqual(build_wld(parsed), encoded)

    def test_3d3_to_gltf_export(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x04] + [0x00] * 32)
        vertices = bytes(
            [0x03,  # vertex header: count=3, non-shared
             0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # v0 mask + (0,0,0)
             0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  # v1 mask + (1,0,0)
             0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00]  # v2 mask + (0,1,0)
        )
        edges = bytes([0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x01, 0x02, 0xFF, 0xFF, 0x02, 0x00])
        primitive = bytes([0x01, 0x05, 0x03, 0x00, 0x01, 0x02, 0x01])  # one fill polygon
        shape_data = render_mode + face_info + vertices + edges + primitive

        blob = bytearray()
        blob.extend((0x33, 0x33))  # signature
        blob.extend((1, 0))  # shape count
        blob.extend((0, 0))  # shape offset[0]
        blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
        blob.extend(shape_data)

        payload = parse_3d3(bytes(blob))
        gltf = export_3d3_to_gltf(payload)
        self.assertEqual(gltf["asset"]["version"], "2.0")
        self.assertEqual(len(gltf["meshes"]), 1)
        self.assertEqual(len(gltf["nodes"]), 1)
        first_mesh = gltf["meshes"][0]
        self.assertEqual(gltf["extras"]["format"], "3D3")
        self.assertFalse(gltf["extras"]["has_shared_vertex_pool"])
        self.assertTrue(first_mesh["primitives"])
        self.assertTrue(any(primitive["mode"] == 4 for primitive in first_mesh["primitives"]))

    def test_3d3_gltf_includes_shared_pool_metadata(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x00] + [0x00] * 32)
        vertices = bytes(
            [
                0x81,       # vertex header: shared-vertex entry + 1 vertex
                0xFF, 0xFF, # visibility mask
                0x00,       # vertex index 0 -> shared arrays
            ]
        )
        edges = bytes([0x00])  # no edges
        primitive_count = bytes([0x00])
        shape_data = render_mode + face_info + vertices + edges + primitive_count

        payload = {
            "format": "3D3",
            "shape_offsets": [0],
            "model_data": base64.b64encode(shape_data).decode("ascii"),
            "model_data_size": len(shape_data),
            "shared_vertex_pool": {
                "x_indices": [0],
                "y_indices": [0],
                "z_indices": [0],
                "x_values": [7],
                "y_values": [8],
                "z_values": [9],
            },
            "trailing_bytes": "",
        }

        gltf = export_3d3_to_gltf(payload)
        self.assertTrue(gltf["extras"]["has_shared_vertex_pool"])
        self.assertEqual(gltf["extras"]["shared_vertex_pool"]["index_count"], 1)
        self.assertEqual(gltf["meshes"][0]["extras"]["render_mode"], 0)

    def test_3d3_to_glb_export(self):
        render_mode = bytes([0x00])
        face_info = bytes([0x04] + [0x00] * 32)
        vertices = bytes(
            [0x03,  # vertex header: count=3, non-shared
             0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  # v0 mask + (0,0,0)
             0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,  # v1 mask + (1,0,0)
             0xFF, 0xFF, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00]  # v2 mask + (0,1,0)
        )
        edges = bytes([0x03, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x01, 0x02, 0xFF, 0xFF, 0x02, 0x00])
        primitive = bytes([0x01, 0x05, 0x03, 0x00, 0x01, 0x02, 0x01])  # one fill polygon
        shape_data = render_mode + face_info + vertices + edges + primitive

        blob = bytearray()
        blob.extend((0x33, 0x33))  # signature
        blob.extend((1, 0))  # shape count
        blob.extend((0, 0))  # shape offset[0]
        blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
        blob.extend(shape_data)

        payload = parse_3d3(bytes(blob))
        glb = export_3d3_to_glb(payload)

        self.assertGreaterEqual(len(glb), 20)
        self.assertEqual(glb[:4], b"glTF")
        self.assertEqual(struct.unpack("<I", glb[4:8])[0], 2)
        total_length = struct.unpack("<I", glb[8:12])[0]
        self.assertEqual(total_length, len(glb))

        json_chunk_length = struct.unpack("<I", glb[12:16])[0]
        self.assertGreater(json_chunk_length, 0)
        self.assertEqual(glb[16:20], b"JSON")
        self.assertGreaterEqual(len(glb), 20 + json_chunk_length)

    def test_3d3_to_gltf_tolerates_truncated_shape_payload(self):
        payload = {
            "format": "3D3",
            "shape_offsets": [0],
            "model_data": base64.b64encode(bytes([7, 62, 7, 7, 16, 61, 62, 63, 64, 34, 65])).decode("ascii"),
            "model_data_size": 11,
            "shared_vertex_pool": None,
            "trailing_bytes": "",
        }

        gltf = export_3d3_to_gltf(payload)
        self.assertEqual(len(gltf["meshes"]), 1)
        self.assertEqual(
            gltf["meshes"][0]["extras"]["shape_payload"]["render_error"],
            "truncated 3D3 face-normal block",
        )

    def test_yaml_sidecar_export_and_roundtrip(self):
        try:
            import yaml
        except Exception:
            self.skipTest("PyYAML unavailable")

        payload = {
            "format": "3DT",
            "version": 1,
            "levels": [
                {
                    "level": 0,
                    "objects": [
                        {
                            "tile_index": 0,
                            "objects": [
                                {"x": 1, "y": 2, "z": 3, "shape_word": 0x00FE},
                            ],
                        }
                    ],
                },
                {"level": 1, "objects": []},
                {"level": 2, "objects": []},
                {"level": 3, "objects": []},
                {"level": 4, "objects": []},
            ],
        }

        source = build_3dt(payload)
        decoded = parse_3dt(source)
        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            yaml_path = base / "shape.yaml"
            json_path = base / "shape.json"

            json_path.write_text(json.dumps(decoded, indent=2, sort_keys=True), encoding="utf-8")
            yaml_path.write_text(yaml.safe_dump(decoded, sort_keys=True), encoding="utf-8")

            from_yaml = yaml.safe_load(yaml_path.read_text(encoding="utf-8"))
            self.assertEqual(from_yaml["format"], "3DT")
            self.assertEqual(from_yaml["tile_counts"], decoded["tile_counts"])

            restored = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(build_3dt(from_yaml), build_3dt(restored))

    def test_yaml_encode_input_supported(self):
        try:
            import yaml
        except Exception:
            self.skipTest("PyYAML unavailable")

        payload = {
            "format": "3DT",
            "version": 1,
            "levels": [
                {
                    "level": 0,
                    "objects": [
                        {
                            "tile_index": 0,
                            "objects": [{"x": 9, "y": 8, "z": 7, "shape_word": 0x00AB}],
                        }
                    ],
                },
                {"level": 1, "objects": []},
                {"level": 2, "objects": []},
                {"level": 3, "objects": []},
                {"level": 4, "objects": []},
            ],
        }

        source = build_3dt(payload)
        parsed = parse_3dt(source)

        with tempfile.TemporaryDirectory() as workdir:
            yaml_path = pathlib.Path(workdir) / "shape.yaml"
            with yaml_path.open("w", encoding="utf-8") as f:
                yaml.safe_dump(parsed, f, sort_keys=True)
            decoded_yaml = cli_module._read_yaml(yaml_path)
            self.assertEqual(decoded_yaml["format"], "3DT")
            self.assertEqual(build_3dt(decoded_yaml), source)

    def test_yaml_sidecar_for_3dg_and_wld(self):
        try:
            import yaml
        except Exception:
            self.skipTest("PyYAML unavailable")

        grid_payload = {
            "format": "3DG",
            "version": 1,
            "grid1": [1] * 16,
            "grid2": [2] * 256,
            "grid3": [3] * 512,
            "grid4": [4] * 512,
            "grid5": [5] * 512,
        }
        wld_payload = {
            "format": "WLD",
            "unknown_header": base64.b64encode(b"\x11\x11").decode("ascii"),
            "read_item_size": 0,
            "ground_unit_count": 0,
            "world_object_count": 0,
            "world_objects": [],
            "flight_unit_count": 0,
            "flight_units": [],
            "wld_buf7": base64.b64encode(bytes([7] * 100)).decode("ascii"),
            "wld_buf8": base64.b64encode(bytes([8] * 100)).decode("ascii"),
            "object_type_table": base64.b64encode(bytes([9] * 100)).decode("ascii"),
            "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
            "name_table": base64.b64encode(b"ALPHA\x00BRAVO\x00").decode("ascii"),
            "trailing_bytes": "",
        }

        grid_bytes = build_3dg(grid_payload)
        wld_bytes = build_wld(wld_payload)
        grid_parsed = parse_3dg(grid_bytes)
        wld_parsed = parse_wld(wld_bytes)

        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)
            grid_yaml = base / "grid.yaml"
            wld_yaml = base / "wld.yaml"
            grid_yaml.write_text(yaml.safe_dump(grid_parsed, sort_keys=True), encoding="utf-8")
            wld_yaml.write_text(yaml.safe_dump(wld_parsed, sort_keys=True), encoding="utf-8")

            self.assertEqual(
                build_3dg(cli_module._read_yaml(grid_yaml)),
                grid_bytes,
            )
            self.assertEqual(
                build_wld(cli_module._read_yaml(wld_yaml)),
                wld_bytes,
            )

    def test_cli_decode_writes_stable_sidecars(self):
        asset_root = pathlib.Path("/home/xor/games/f15")
        if not asset_root.exists():
            self.skipTest("missing asset tree")

        sample_pic = next(iter(sorted(asset_root.glob("*.PIC"))), None)
        if sample_pic is None:
            sample_pic = next(iter(sorted(asset_root.glob("*.pic"))), None)
        if sample_pic is None:
            sample_pic = next(iter(sorted(asset_root.glob("*.SPR"))), None)
        if sample_pic is None:
            sample_pic = next(iter(sorted(asset_root.glob("*.spr"))), None)

        sample_3d3 = next(iter(sorted(asset_root.glob("*.3D3"))), None)
        if sample_3d3 is None:
            self.skipTest("missing .3D3 sample")

        sample_3dt = next(iter(sorted(asset_root.glob("*.3DT"))), None)
        if sample_3dt is None:
            self.skipTest("missing .3DT sample")

        with tempfile.TemporaryDirectory() as workdir:
            base = pathlib.Path(workdir)

            json_3d3 = base / "asset_3d3.json"
            yaml_3d3 = base / "asset_3d3.yaml"
            gltf = base / "asset_3d3.gltf"

            if sample_pic is not None:
                json_pic = base / "asset_pic.json"
                yaml_pic = base / "asset_pic.yaml"
                png_pic = base / "asset_pic.png"
                rc = cli_module.main(
                    [
                        "decode",
                        str(sample_pic),
                        str(json_pic),
                        "--png",
                        str(png_pic),
                        "--yaml",
                        str(yaml_pic),
                    ]
                )
                self.assertEqual(rc, 0)
                self.assertTrue(json_pic.exists())
                self.assertTrue(yaml_pic.exists())
                self.assertTrue(png_pic.exists())
                payload_pic = json.loads(json_pic.read_text(encoding="utf-8"))
                self.assertEqual(payload_pic["format"], "PIC")

            rc = cli_module.main(
                [
                    "decode",
                    str(sample_3d3),
                    str(json_3d3),
                    "--gltf",
                    str(gltf),
                    "--yaml",
                    str(yaml_3d3),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_3d3.exists())
            self.assertTrue(yaml_3d3.exists())
            self.assertTrue(gltf.exists())
            payload = json.loads(json_3d3.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "3D3")

            json_3dt = base / "asset_3dt.json"
            yaml_3dt = base / "asset_3dt.yaml"
            rc = cli_module.main(
                [
                    "decode",
                    str(sample_3dt),
                    str(json_3dt),
                    "--yaml",
                    str(yaml_3dt),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_3dt.exists())
            self.assertTrue(yaml_3dt.exists())
            payload = json.loads(json_3dt.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "3DT")

            sample_3dg = next(iter(sorted(asset_root.glob("*.3DG"))), None)
            if sample_3dg is None:
                sample_3dg = next(iter(sorted(asset_root.glob("*.3dg"))), None)
            if sample_3dg is None:
                self.skipTest("missing .3DG sample")

            json_3dg = base / "asset_3dg.json"
            yaml_3dg = base / "asset_3dg.yaml"
            rc = cli_module.main(
                [
                    "decode",
                    str(sample_3dg),
                    str(json_3dg),
                    "--yaml",
                    str(yaml_3dg),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_3dg.exists())
            self.assertTrue(yaml_3dg.exists())
            payload = json.loads(json_3dg.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "3DG")

            sample_wld = next(iter(sorted(asset_root.glob("*.WLD"))), None)
            if sample_wld is None:
                sample_wld = next(iter(sorted(asset_root.glob("*.wld"))), None)
            if sample_wld is None:
                self.skipTest("missing .WLD sample")

            json_wld = base / "asset_wld.json"
            yaml_wld = base / "asset_wld.yaml"
            rc = cli_module.main(
                [
                    "decode",
                    str(sample_wld),
                    str(json_wld),
                    "--yaml",
                    str(yaml_wld),
                ]
            )
            self.assertEqual(rc, 0)
            self.assertTrue(json_wld.exists())
            self.assertTrue(yaml_wld.exists())
            payload = json.loads(json_wld.read_text(encoding="utf-8"))
            self.assertEqual(payload["format"], "WLD")

    def test_cli_convert_tree_produces_modern_artifacts(self):
        with tempfile.TemporaryDirectory() as workdir:
            source_root = pathlib.Path(workdir) / "assets"
            output_root = pathlib.Path(workdir) / "out"
            nested = source_root / "nested"
            source_root.mkdir()
            nested.mkdir()

            pic_payload = {
                "format": "PIC",
                "decoded_width": 320,
                "decoded_height": 200,
                "max_lzw_width": 12,
                "bitstream_mode": "byte",
                "pixels_base64": base64.b64encode(bytes((i & 0xFF) for i in range(320 * 200))).decode("ascii"),
            }
            (source_root / "TITLE.PIC").write_bytes(encode_pic_asset(pic_payload))

            shape_data = bytes([0x00])  # render mode
            face_info = bytes([0x00] + [0x00] * 32)
            vertices = bytes(
                [
                    0x00,  # vertex header: vertex_count=0, non-shared
                ]
            )
            edges = bytes([0x00])  # no edges
            primitive = bytes([0x00])  # no primitives
            shape_data += face_info + vertices + edges + primitive

            shape_blob = bytearray()
            shape_blob.extend((0x33, 0x33))
            shape_blob.extend((1, 0))
            shape_blob.extend((0, 0))
            shape_blob.extend((len(shape_data) & 0xFF, (len(shape_data) >> 8) & 0xFF))
            shape_blob.extend(shape_data)
            (source_root / "SCENERY.3D3").write_bytes(bytes(shape_blob))

            tdt_payload = {
                "format": "3DT",
                "version": 1,
                "levels": [
                    {"level": 0, "objects": []},
                    {"level": 1, "objects": []},
                    {"level": 2, "objects": []},
                    {"level": 3, "objects": []},
                    {"level": 4, "objects": []},
                ],
            }
            (nested / "TACTICS.3DT").write_bytes(build_3dt(tdt_payload))

            grid_payload = {
                "format": "3DG",
                "version": 1,
                "grid1": [0] * 16,
                "grid2": [1] * 256,
                "grid3": [2] * 512,
                "grid4": [3] * 512,
                "grid5": [4] * 512,
            }
            (nested / "LANDS.3DG").write_bytes(build_3dg(grid_payload))

            wld_payload = {
                "format": "WLD",
                "unknown_header": base64.b64encode(b"\x00\x00").decode("ascii"),
                "read_item_size": 0,
                "ground_unit_count": 0,
                "world_object_count": 0,
                "world_objects": [],
                "flight_unit_count": 0,
                "flight_units": [],
                "wld_buf7": base64.b64encode(bytes([1] * 100)).decode("ascii"),
                "wld_buf8": base64.b64encode(bytes([2] * 100)).decode("ascii"),
                "object_type_table": base64.b64encode(bytes([3] * 100)).decode("ascii"),
                "terrain_grid": base64.b64encode(bytes(range(256))).decode("ascii"),
                "name_table": base64.b64encode(b"NODE\x00VALUE\x00").decode("ascii"),
            }
            (source_root / "WORLD.WLD").write_bytes(build_wld(wld_payload))

            rc = cli_module.main(
                [
                    "convert-tree",
                    str(source_root),
                    str(output_root),
                    "--recursive",
                    "--yaml",
                    "--models",
                    "glb",
                ]
            )
            self.assertEqual(rc, 0)

            self.assertTrue((output_root / "TITLE.json").exists())
            self.assertTrue((output_root / "TITLE.yaml").exists())
            self.assertTrue((output_root / "TITLE.png").exists())

            self.assertTrue((output_root / "SCENERY.json").exists())
            self.assertTrue((output_root / "SCENERY.yaml").exists())
            self.assertTrue((output_root / "SCENERY.glb").exists())

            self.assertTrue((output_root / "nested" / "TACTICS.json").exists())
            self.assertTrue((output_root / "nested" / "TACTICS.yaml").exists())

            self.assertTrue((output_root / "nested" / "LANDS.json").exists())
            self.assertTrue((output_root / "nested" / "LANDS.yaml").exists())

            self.assertTrue((output_root / "WORLD.json").exists())
            self.assertTrue((output_root / "WORLD.yaml").exists())


if __name__ == "__main__":
    unittest.main()
