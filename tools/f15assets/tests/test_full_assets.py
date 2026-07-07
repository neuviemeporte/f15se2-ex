from __future__ import annotations

import base64
import unittest
from pathlib import Path

from f15assets import decode_pic_asset, encode_pic_asset, parse_3d3, parse_3dg, parse_3dt, parse_wld

ASSET_ROOT = Path("/home/xor/games/f15")
HAS_ASSETS = ASSET_ROOT.exists()


@unittest.skipIf(not HAS_ASSETS, f"missing asset tree: {ASSET_ROOT}")
class FullAssetRoundTripTest(unittest.TestCase):
    @staticmethod
    def _roundtrip_pic(path: Path) -> None:
        source = path.read_bytes()
        payload = decode_pic_asset(source)
        rebuilt = encode_pic_asset(payload)
        reparsed = decode_pic_asset(rebuilt)
        source_pixels = base64.b64decode(payload["pixels_base64"])
        rebuilt_pixels = base64.b64decode(reparsed["pixels_base64"])
        if source_pixels != rebuilt_pixels:
            raise AssertionError("PIC pixel payload changed after decode+encode")

    @staticmethod
    def _parse_only(path: Path, parse_func, expected_format: str) -> None:
        payload = parse_func(path.read_bytes())
        if payload.get("format") != expected_format:
            raise AssertionError(f"bad parsed format for {path.name}: {payload.get('format')}")

    def test_full_pic_and_spr_roundtrip(self) -> None:
        paths = sorted(ASSET_ROOT.glob("*.PIC")) + sorted(ASSET_ROOT.glob("*.pic")) + sorted(ASSET_ROOT.glob("*.SPR")) + sorted(ASSET_ROOT.glob("*.spr"))
        if not paths:
            self.skipTest("no .PIC/.SPR files found")
        for path in paths:
            with self.subTest(path=path):
                self._roundtrip_pic(path)

    def test_full_pic_and_spr_decode(self) -> None:
        paths = sorted(ASSET_ROOT.glob("*.PIC")) + sorted(ASSET_ROOT.glob("*.pic")) + sorted(ASSET_ROOT.glob("*.SPR")) + sorted(ASSET_ROOT.glob("*.spr"))
        if not paths:
            self.skipTest("no .PIC/.SPR files found")
        for path in paths:
            with self.subTest(path=path):
                self._parse_only(path, decode_pic_asset, "PIC")

    def test_full_3d3_decode(self) -> None:
        paths = sorted(ASSET_ROOT.glob("*.3D3")) + sorted(ASSET_ROOT.glob("*.3d3"))
        if not paths:
            self.skipTest("no .3D3 files found")
        for path in paths:
            with self.subTest(path=path):
                self._parse_only(path, parse_3d3, "3D3")

    def test_full_3dt_decode(self) -> None:
        paths = sorted(ASSET_ROOT.glob("*.3DT")) + sorted(ASSET_ROOT.glob("*.3dt"))
        if not paths:
            self.skipTest("no .3DT files found")
        for path in paths:
            with self.subTest(path=path):
                self._parse_only(path, parse_3dt, "3DT")

    def test_full_3dg_decode(self) -> None:
        paths = sorted(ASSET_ROOT.glob("*.3DG")) + sorted(ASSET_ROOT.glob("*.3dg"))
        if not paths:
            self.skipTest("no .3DG files found")
        for path in paths:
            with self.subTest(path=path):
                self._parse_only(path, parse_3dg, "3DG")

    def test_full_wld_decode(self) -> None:
        paths = sorted(ASSET_ROOT.glob("*.WLD")) + sorted(ASSET_ROOT.glob("*.wld"))
        if not paths:
            self.skipTest("no .WLD files found")
        for path in paths:
            with self.subTest(path=path):
                self._parse_only(path, parse_wld, "WLD")
