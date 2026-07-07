# F-15 SE II Asset Tools

Developer and customizer tools for converting original F-15 Strike Eagle II assets into modern, editable files.

These tools are forward-conversion first. They are meant to make original game assets inspectable, editable, and eventually loadable by a modernized game runtime when replacement assets are present. They do not currently promise full backward conversion into original binary formats.

## Source and output layout

Typical source assets:

```text
/home/xor/games/f15
```

Recommended output folder:

```text
converted_assets_all
```

Run commands from the repository root that contains this `tools` directory.

## Commands

Convert a whole asset tree:

```bash
python3 -m tools.f15assets.cli convert-tree /home/xor/games/f15 converted_assets_all --yaml
```

Decode one asset:

```bash
python3 -m tools.f15assets.cli decode /home/xor/games/f15/VN.WLD converted_assets_all/VN.WLD.json --format WLD --yaml converted_assets_all/VN.WLD.yaml
```

Export fonts:

```bash
python3 -m tools.f15assets.cli export-fonts . converted_assets_all/fonts
```

Export digitized sounds and driver sidecars:

```bash
python3 -m tools.f15assets.cli --pretty export-sounds /home/xor/games/f15 converted_assets_all/sounds --sample-rate 7850
```

## Converted formats

| Original | Modern output | Notes |
| --- | --- | --- |
| `*.PIC`, `*.SPR` | indexed PNG + JSON/YAML sidecar | PNG keeps palette-based pixels; sidecar keeps original decode metadata and source bytes. |
| `TITLE640.PIC` | `640x350` PNG + sidecar | Stored as alternating left/right rows by the original 640-mode loader. |
| `1.PIC`..`4.PIC` | VGA-style PNG + sidecar | Demo-only images use a different byte-indexed loader path. |
| `*.3D3` | `.glb` + JSON/YAML sidecar | Blender-friendly model export. JSON keeps shape ids, offsets, raw chunks, and metadata. |
| `*.3DT` | JSON/YAML sidecar | Terrain object placement tables. |
| `*.3DG` | JSON/YAML sidecar | Terrain grid lookup data. |
| `*.WLD` | JSON/YAML sidecar | Theater/world objects, terrain grid, object type table, flight-unit templates, and names. |
| fonts | BDF + PNG + JSON | Uses Unicode codepoints while preserving original CP437 source bytes. |
| `F15DGTL.BIN` | raw WAV + cue WAVs + JSON | Unsigned 8-bit mono PCM at 7850 Hz by default. |
| sound drivers | JSON sidecars | Driver executables are preserved for reverse-engineering metadata. |

## 3D model customization

`3D3` assets are exported to glTF binary (`.glb`) for Blender and other modern tools.

Important constraints:

- Shape ids must remain stable. Do not compact or renumber shapes if the game will use the edited asset as a replacement.
- Some original shapes are intentionally 2D or single-sided. The exporter keeps one-sided planes instead of fabricating backfaces, because duplicated reverse faces can create z-fighting.
- Some theater model slots are non-renderable placeholders. They are skipped in GLB nodes but preserved in sidecar metadata.
- Shape names are not embedded in `.3D3`; names are derived from matching `.WLD` object labels where possible.
- For runtime replacement, use both `.glb` and the `.3D3.json` sidecar. GLB alone is not enough to preserve game-specific ids and flags.

## World and mission editing

Open the standalone map editor:

```text
tools/f15assets/map_editor.html
```

Use it to load exported `*.WLD.json` files. It supports:

- viewing the 16x16 terrain grid;
- viewing, selecting, dragging, duplicating, and deleting world objects;
- editing raw object fields such as `x_coord`, `y_coord`, `unitRef`, `unitType`, `targetFlags`, `occupantType`, `patrolCount`, and `objectIdx`;
- derived labels for base candidates, target candidates, ground units, disabled objects, and waypoint/reference points;
- linked flight-unit summaries via `waypointIdx`;
- exporting edited JSON.

Primary and secondary mission targets are not fixed fields in `.WLD`. The start-module mission generator chooses them dynamically from world objects, `object_type_table`, terrain constraints, and `targetFlags`.

Known useful `targetFlags` bits:

```text
0x001  mission/placeable candidate
0x008  random removable unit
0x080  destroyed/inactive marker in runtime tables
0x100  airbase
0x200  large/runway/carrier-like target handling
0x400  waypoint/no intercept behavior
0x800  disabled
0x1000 enemy-counted runtime target flag
```

The `.WLD` format does not expose a simple universal friend/enemy enum. Some runtime flags imply combat behavior, but faction semantics are partly generated at mission load.

## Sound cues

`F15DGTL.BIN` is exported as one raw WAV plus driver-backed cue WAVs recovered from ASOUND.

ASOUND cue ranges are inclusive in the driver:

```text
audio_playSample(0): 0x0000..0x31F3
audio_playSample(4): 0x31F4..0x4796
audio_playSample(2), variant 0: 0x4797..0x5C92
audio_playSample(2), variant 1: 0x5C93..0x6A1A
audio_playSample(2), variant 2: 0x6A1B..0x7D9D
```

`voiceCueThresholds` in the game code are availability checks before playback, not the full cue split table.

The sample rate defaults to `7850` Hz. This comes from recovered driver timer behavior in the 8 kHz range, not from a WAV header in the original asset.

## Runtime replacement recommendation

For future game loading, prefer a manifest-like approach:

```json
{
  "VN.WLD": { "world": "VN.WLD.json" },
  "VN.3D3": { "geometry": "VN.3D3.glb", "metadata": "VN.3D3.json" },
  "TITLE640.PIC": { "image": "TITLE640.png", "metadata": "TITLE640.json" },
  "F15DGTL.BIN": { "sounds": "sounds/f15dgtl_raw.json" }
}
```

Do not rely on PNG, GLB, or WAV alone for game replacement. The sidecar JSON carries ids, flags, palette metadata, original offsets, and unresolved bytes that the runtime may need.

## Python package notes

The package is intentionally simple and local:

```text
tools/f15assets/cli.py
tools/f15assets/f15assets/*.py
tools/f15assets/tests/*.py
```

No install step is required when running from the repository root with `python3 -m tools.f15assets.cli`.

The tests are smoke/round-trip helpers for converter development. Run them only when you intentionally want validation work.
