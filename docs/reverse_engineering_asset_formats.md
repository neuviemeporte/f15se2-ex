# F-15 Strike Eagle II Reverse Engineering Asset Notes

This document collects facts useful for asset reverse engineering and future
converters for F-15 Strike Eagle II. It combines the manuals in
`/home/xor/games/f15/docs/`, observed DOS data files in `/home/xor/games/f15`,
and the current decompiled loaders in this repository.

## Source Documents

- `F-15_Strike_Eagle_II_RefCard.pdf`
- `f-15 strike eagle ii (manual).pdf`
- `f-15 strike eagle ii (operation desert storm scenario disk).pdf`
- `f-15_strike_eagle_ii_maps.pdf`
- `f-15_strike_eagle_ii_technical_supplement.pdf`

The technical supplement confirms the game supports CGA, EGA, MCGA, VGA, Tandy,
and Hercules modes. EGA needs 256K. It also confirms that all game files must be
in one directory or subdirectory, and that extra F-19/F-15 scenario disk theater
files are selected in game through the "Other" theater option.

## File Families

The installed DOS data under `/home/xor/games/f15` contains these asset families:

- `*.PIC`: compressed full-screen or title/cockpit pictures.
- `*.SPR`: compressed sprite sheets used by start/end screens and map/debrief UI.
- `*.3D3`: 3D model containers and shape offset tables.
- `*.3DT`: terrain/tile object placement tables.
- `*.3DG`: hierarchical terrain/grid lookup tables.
- `*.WLD`: theater world/campaign data.

The theater file names pair like this:

| Theater | World file | 3D base |
| --- | --- | --- |
| Libya | `LIBYA.WLD` | `LB.3D3`, `LB.3DT`, `LB.3DG` |
| Persian Gulf | `GULF.WLD` | `PG.3D3`, `PG.3DT`, `PG.3DG` |
| Vietnam | `VN.WLD` | `VN.3D3`, `VN.3DT`, `VN.3DG` |
| Middle East | `ME.WLD` | `ME.3D3`, `ME.3DT`, `ME.3DG` |
| North Cape | `NC.WLD` | `NC.3D3`, `NC.3DT`, `NC.3DG` |
| Central Europe | `CE.WLD` | `CE.3D3`, `CE.3DT`, `CE.3DG` |
| Desert Storm / JP slot | `JP.WLD` | `JP.3D3`, `JP.3DT`, `JP.3DG` |

The technical supplement lists additional scenario-disk files such as `NC.WLD`,
`CE.WLD`, `NC.3D3`, `NC.3DT`, `NC.3DG`, `CE.3D3`, `CE.3DT`, `CE.3DG`,
`NCAPE.SPR`, and `CEUROPE.SPR`.

## Recommended Editable Formats

Use different intermediate formats for different jobs. The goal is not one
universal conversion target; the goal is to make each asset easy to edit and
stable to share. The project is intentionally forward-only: convert from game binaries
into stable, modern, self-contained formats for customization.

The converter prioritizes forward conversion only:

- Export should create an editable file plus a metadata sidecar.
- Unknown bytes, padding, original order, raw numeric values, compression mode,
  palette identity, and offsets must be preserved in the sidecar.
- Rebuild/backward conversion to original game files is explicitly unsupported and is not part of the project contract.
  The canonical source is the decoded payload plus a metadata sidecar.
- The modern baseline used for authoring is:
  - `glTF 2.0` (`.glb` by default, `.gltf` optional) for models.
  - Indexed `PNG` (with embedded palette chunk) for image assets.
  - `JSON` (with optional `YAML`) for structured world/terrain/table data.
- The default developer workflow is stable-then-edit: export to modern formats
  first, then modify only the exported modern format plus metadata sidecars.
- Modern reliable authoring targets:
  - `glTF 2.0` (`.glb` by default, `.gltf` optional) for geometry and model editing (Blender-first).
  - Indexed `PNG` for image art.
  - `JSON`/`YAML` for structured world/terrain/table data.
- For stable pipelines, the modern editable target for 3D is **glTF 2.0**
  (`.glb`) because it is reproducible, self-contained, widely supported, and easy to
  move into Blender.
- Lossy/editor formats like PNG and glTF should be accompanied by canonical
  structured metadata for any real edit pipeline.

| Game format | Best editable format | Canonical export source | Export from game | Import back to game | Notes |
| --- | --- | --- | --- | --- | --- |
| `*.PIC` | Indexed PNG | PNG + JSON sidecar + original header/compression metadata | Yes | No | Best for full-screen images and cockpit/title art. JSON keeps palette, image mode, max code width, output layout, and original compression metadata. |
| `*.SPR` | Indexed PNG sprite sheet + JSON atlas | PNG + JSON sidecar + original header/compression metadata | Yes | No | Same compressed image stream as PIC. The atlas is not embedded in SPR; generate/maintain JSON from code sprite descriptors and hand-assigned names. |
| `*.3D3` theater models | **glTF (`.glb` default)** + `3D3` JSON sidecar | Parsed `.3D3` JSON plus raw display-list chunks and vertex-pool data | Yes | No | glTF is the stable modern mesh target for Blender, tools, and web pipelines. Keep a canonical `3D3` sidecar for traceable edits. |
| `15FLT.3D3` aircraft models | **glTF (`.glb` default)** + `3D3` JSON sidecar | Parsed `.3D3` JSON plus raw aircraft display-list chunks | Yes | No | Same model-stream issue as theater `.3D3`; preserve shape ids and aircraft table mapping in JSON metadata. |
| `PHOTO.3D3` target-photo models | **glTF (`.glb` default)** + `3D3` JSON sidecar | Parsed `.3D3` JSON plus raw chunks and append metadata | Yes | No | Treat like `.3D3`; preserve append/slot metadata because runtime appends selected chunks. |
| `*.3DT` tile placements | YAML or JSON, optional CSV view | Canonical YAML/JSON with every 16-bit field preserved | Yes | No | Text is better than Blender for placements. It is object placement data: tile level, tile index, x/y/z, and a 16-bit shape word whose low byte is used at runtime. CSV can be a view, not the canonical source. |
| `*.3DG` terrain grid | JSON/YAML data + PNG previews | Canonical JSON/YAML with all five grid buffers | Yes | No | PNG previews are only for visualization. Exact hierarchical grid data must be kept in JSON/YAML. |
| `*.WLD` world/campaign | YAML or JSON, optional CSV tables | Canonical YAML/JSON preserving unknown buffers, name table bytes, and record order | Yes | No | Best as structured text: world objects, flight units, terrain grid, name table, unknown buffers. CSV helps spreadsheet editing, but JSON/YAML should be canonical. |
| Palette data | GIMP `.gpl` or JASC `.pal` for editing | JSON with exact DAC tables, mode, and 6-bit/8-bit level metadata | Yes | No | Editor palette files are useful but not enough; JSON must preserve exact game palette/register metadata. |
| Text and strings | `.po` + `.json` text bundles | In-binary text regions extracted from executable/overlay segments | Planned | No | No standalone text resource files were identified. Use string-id and source-offset maps to support translation and custom copy editing. |
| Fonts | Unicode `BDF` + atlas `PNG` + metadata JSON | Extracted font tables in `src/fontdata.h` + metrics in `src/gfx_impl.c` | Yes | No | `export-fonts` emits one BDF, atlas, and sidecar per known font. BDF is the editable Unicode-capable bitmap font target; JSON preserves legacy CP437 source bytes, Unicode codepoints, atlas rects, advance widths, and raw bitmap rows. |
| Sounds | `.wav` + metadata JSON, unresolved driver sidecars | `F15DGTL.BIN` and sound-driver executables | Partial | No | `export-sounds` emits `F15DGTL.BIN` as a raw unsigned 8-bit PCM WAV plus ASOUND-recovered cue WAVs and metadata. Driver EXEs are preserved as unresolved JSON until synthesized effect tables are decoded. |
| Manuals/maps | Markdown notes + georeferenced PNG overlays | Not applicable | Yes | No | These are reference material, not game assets. Use them to validate WLD coordinates, target names, and theater symbols. |

### Modern Forward-Compatible Export Target

The modern stable formats for developer workflows are:

- Indexed `PNG` (with embedded palette chunk) for image assets (`.PIC`, `.SPR`) because it is universally supported and
  easy to edit.
- `glTF 2.0` (`.glb` by default, `.gltf` optional) for `3D3` model previews/editing.  
  Blender users should import/export via glTF (`.blend` is a downstream workspace,
  not the canonical interchange artifact).
- JSON (and optional YAML) for table/world data (`.3DT`, `.3DG`, `.WLD`), with stable
  schemas and explicit byte-preservation sidecars.
- `.po`/`.json` for translatable text bundles and locale work.
- Unicode `BDF` + font atlas `PNG` + glyph-metrics `JSON` for font extensions and localization.
- PCM `WAV` (`AIFF/FLAC` as needed) + sidecar JSON for game sounds.

Blender interoperability is achieved via glTF import/export. There is no direct
`.3D3` editor round-trip in this project. The stable authoritative interchange
format for model customization is `.glb` (fallback: `.gltf`).

A practical modernization goal is: export each game format into one of the above
stable formats plus a machine-readable metadata sidecar (`.json`) before any custom
editor tooling is applied. For development workflows, the canonical artifact must be
the modern self-contained export plus its sidecar; raw binary edits are not part of the supported
customization path.

For developer customization, the project treats `.glb` as the stable
modern format for mesh/content workflows and `JSON` as the stable format for all
non-binary structured assets.
All mesh outputs should remain self-contained to avoid external dependency files.

### Text, Fonts, and Sounds Export (Customization Plan)

The game appears to keep text strings, font glyph sets, and sounds inside
executables/overlays rather than as standalone `.txt`, `.fnt`, or `.wav` asset files.

For reliable modding, extend the forward export contract as follows:

1. Text export to `strings.po` + `strings.json`.
2. Font export to `font_<id>.bdf` + `font_<id>.png` + `font_<id>.json`.
3. Sound export to `f15dgtl_raw.wav` + `f15dgtl_raw.json`, ASOUND-recovered
   cue WAVs such as `voice_cue_000_sample0.wav`, plus driver sidecars such as
   `asound_driver.json` for unresolved synthesis/effect data.

Proposed metadata fields for all three classes:

- `id`: stable identifier (`<module>:<offset>`).
- `source_file`: source executable/overlay file.
- `source_offset`: byte offset in source.
- `source_length`: byte length.
- `encoding`: text encoding (`cp437`, `latin1`, `utf8`) or sound codec.
- `codec`: codec name/marker if non-PCM.
- `sample_rate`, `channels`, `bits_per_sample` (audio only).
- `source_bytes_sha256`: hash of the original bytes.
- `status`: `extracted`, `decoded`, `unresolved`, or `needs-review`.
- `source_bytes_base64`: full unmodified source slice.

Translator guidance:

- Keep `msgctxt` in `.po` entries as `<file>:<offset>:<context>` to preserve
  in-place replacements.
- Keep stable identifiers so edits stay aligned with sidecars and future
  extraction runs.

### Practical Conversion Plan

Implement forward export support in this order:

1. `PIC`/`SPR` to indexed PNG + JSON sidecar.
2. `WLD`, `3DT`, and `3DG` to structured JSON/YAML sidecars.
3. `3D3`/`15FLT.3D3` to glTF export for viewing/editing in modern toolchains.
   Backward conversion/import back to `.3D3` is intentionally unsupported.
4. Text extraction from executable/overlay data, then export to the formats
   above for localization workflows. Font and sound forward export now have
   initial CLI support.

For editable text, prefer YAML when humans will hand-edit files and JSON when
tools will generate or validate files. A robust converter can support both, but
one canonical form should be chosen per format to avoid noisy diffs.

### Python Converter CLI

The local converter lives at `tools/f15assets/` and handles supported image,
model, terrain, world, font, and sound export paths:

- `PIC`/`SPR` (`.pic`, `.spr`)
- `.3D3` (`.3d3`)
- `.3DT` (`.3dt`)
- `.3DG` (`.3dg`)
- `.WLD` (`.wld`)

Usage:

```text
python -m tools.f15assets.cli [--pretty] decode <asset> <sidecar.json>
python -m tools.f15assets.cli [--pretty] decode <input> <output.json> --png <out.png>
python -m tools.f15assets.cli [--pretty] decode <input> <output.json> --yaml <output.yaml>
python -m tools.f15assets.cli [--pretty] decode <input.3d3> <output.json> --gltf <out.gltf|out.glb>
python -m tools.f15assets.cli [--pretty] convert-tree <asset_dir> <out_dir> [--recursive] [--models glb|gltf|none] [--yaml] [--no-png]
python -m tools.f15assets.cli [--pretty] export-fonts <repo_root> <out_dir> [--no-bdf]
python -m tools.f15assets.cli [--pretty] export-sounds <asset_dir> <out_dir> [--sample-rate 7850]
```

Supported flags:

- `--format`: force a format if the extension is missing or ambiguous.
- `--png`: extract indexed PNG for PIC/SPR only.
- `--gltf`: export `*.3D3` to `.gltf` or `.glb` while writing the `3D3` JSON sidecar.
- `--yaml`: optional YAML sidecar output for table formats (`3DT`, `3DG`, `WLD`) and other parser outputs.
- `--pretty`: pretty-print JSON output. It is a global option and must appear
  before the subcommand.
- `convert-tree` command:
  - `--recursive`: recurse input directories.
  - `--models {glb,gltf,none}`: choose 3D model output; defaults to `glb` for self-contained outputs.
  - `--no-png`: skip image exports for PIC/SPR.
  - `--yaml`: emit YAML sidecars for all supported parsable formats.
- `export-fonts` command:
  - Reads `src/fontdata.h` and `src/gfx_impl.c`.
  - Emits `font_<id>.bdf`, `font_<id>.png`, and `font_<id>.json`.
  - Current known font ids are `0`, `1`, `3`, `4`, and `5`.
  - JSON uses Unicode codepoints while preserving original CP437 source bytes
    (`source_byte`, `source_encoding`) so additional glyphs for other languages
    can be appended without losing the original game mapping.
- `export-sounds` command:
  - Reads `F15DGTL.BIN` from the installed game directory and emits
    `f15dgtl_raw.wav` plus `f15dgtl_raw.json`.
  - Also emits driver-backed cue files (`voice_cue_000_sample0.wav`,
    `voice_cue_001_sample4.wav`, `voice_cue_002_sample2_variant0.wav`, ...)
    with per-cue JSON. ASOUND uses inclusive playback ranges:
    `0x0000..0x31F3`, `0x31F4..0x4796`,
    `0x4797..0x5C92`, `0x5C93..0x6A1A`, and `0x6A1B..0x7D9D`.
    The final three ranges are rotating variants for `audio_playSample(2)`.
    `voiceCueThresholds` in `egdata.c` are availability checks before playback,
    not the complete split table.
  - Preserves sound-driver executables (`ASOUND.EXE`, `ISOUND.EXE`,
    `NSOUND.EXE`, `RSOUND.EXE`, `TSOUND.EXE`) as unresolved JSON sidecars.
  - Uses unsigned 8-bit mono PCM at a PIT-derived `7850` Hz by default; override
    with `--sample-rate` if a different recovered driver rate is confirmed.

Current objective is forward conversion from game assets to modern editable
formats only. Rebuild/import pathways are intentionally unsupported to keep the
output pipeline stable and script-friendly.

A required modern compatibility baseline is therefore:

- `.glb` (fallback `.gltf`) for 3D geometry (Blender-first workflows), and it should remain self-contained.
- Indexed `.png` for image assets.
- JSON or YAML text for structural tables and world data, with strict,
  versioned sidecar metadata.

PIC/SPR sidecars are lossless for decoded pixel payloads at export time.

- `format`: format tag (`PIC`)
- `decoded_width`/`decoded_height`: currently expected to be `320`/`200`
- `bitstream_mode`: `"byte"` or `"nibble"`
- `max_lzw_width`: LZW max code width
- `palette_profile`: metadata about palette identity source/availability
  - `source`: where palette identity comes from
  - `status`: `"not_embedded"` when source palette is external to payload
  - `index_mode`: `"indexed"`
  - `index_bit_depth`: integer palette index width
  - `color_mode`: palette index color mode identifier

For exports where palette data is unavailable (historically the common case for
`.PIC`/`.SPR`), the converter now applies a fallback `16`-color VGA palette for
`index_bit_depth: 4` assets to avoid near-black renders. `index_bit_depth: 8`
assets still use a grayscale fallback.
- `compressed_payload_base64`: full original compressed payload, preserved for replayable export
- `pixels_base64`: full decoded pixel stream (row-major)
- `lzw_payload_size`: original compressed payload size in bytes

Forward-conversion tests are implemented in `tools/f15assets/tests/test_smoke.py` and
`tools/f15assets/tests/test_full_assets.py` (the latter is skipped when
`/home/xor/games/f15` is missing).  

Current text/font/sound extraction state:

- `PIC`/`SPR`/`3D3`/`3DT`/`3DG`/`WLD` decoding is the current converter focus.
- Font extraction is implemented from the current recovered font tables and
  metrics.
- Sound extraction is partial: `F15DGTL.BIN` is exported as a raw WAV plus
  ASOUND-recovered cue WAVs. Digitized cue ranges are known; synthesized effect
  driver tables remain unresolved sidecars.
- Text extraction remains a roadmap item and should emit `status: unresolved`
  entries until parsers are implemented.

### Implementation Language

Use Python as the primary converter language.

Reasons:

- Blender scripting is Python, so `.3D3` to Blender import/export can share code
  with the command-line converter.
- PNG, indexed palettes, YAML, JSON, CSV, and binary struct handling are all
  well-supported.
- Reverse-engineering work benefits from quick scripts, readable parsers, and
  fast iteration more than from maximum runtime speed.
- Kaitai Struct can generate Python parsers for the container formats.
- The asset files are small enough that Python performance is not a problem.

Recommended layout:

```text
tools/f15assets/
    README.md
    map_editor.html
    cli.py              # command-line entry point
    f15assets/
        pic.py          # PIC/SPR LZW/RLE decode
        fonts.py        # extracted font tables to BDF/PNG/JSON
        sounds.py       # F15DGTL.BIN WAV/cue export and driver sidecars
        model3d.py      # .3D3 container and display-list parser
        terrain.py      # .3DT and .3DG
        world.py        # .WLD
        io.py           # binary/base64 helpers
    tests/
        test_smoke.py
        test_full_assets.py
```

Do not start with a mixed-language converter. Add Rust or C++ later only if a
specific hot path or library boundary needs it. If that happens, keep the file
format tests and reference parser in Python so reverse-engineering remains easy.

## PIC and SPR Images

The shared image loader is in `src/shared/picimpl.c`; wrappers are in
`src/shared/filepic.c`, `src/egpic.c`, `src/enaward.c`, `src/endbrf.c`, and
`src/stmain.c`.

`*.PIC` and `*.SPR` use the same picture decoder path. Examples:

- `1.PIC`, `2.PIC`, `3.PIC`, `4.PIC` are demo-only `M0` wrapped pictures. They
  start with an embedded 256-color VGA DAC palette, then a small image header,
  then an inner LZW/RLE stream. The demo loader treats that inner stream as
  byte-indexed VGA data with the first inner byte used as the max code width;
  do not apply the normal-game nibble/byte mode rule to the inner `0b` byte.
  Use the embedded palette for PNG export and do not force the outer `4d30`
  bytes through the shared LZW decoder.

- `openShowPic("f15.spr", 2)` loads the start sprite sheet.
- `loadPic(theaterSprFiles[gameData->theater], spriteBufSeg)` loads debrief map
  sprites.
- `openShowPic("dbicons.spr", 1)` loads debrief icon sprites.

The format is a stateful LZW stream followed by an RLE stage:

1. The first two bytes are read as a little-endian word.
2. The low byte selects output mode and maximum code width.
   - Bit 7 set means byte/pixel mode, with max width equal to `-int8(low_byte)`.
   - Bit 7 clear means nibble mode, with max width equal to `low_byte`.
3. The high byte of the first word is already the first compressed data byte.
4. LZW starts at 9-bit codes.
5. The dictionary starts with 256 single-byte entries.
6. The dictionary grows up to 2048 entries.
7. If the code width grows beyond the max width, the dictionary resets.
8. Output byte `0x90` is an RLE escape.
   - `0x90 0x00` emits a literal `0x90`.
   - `0x90 count` repeats the previous output byte `count - 1` more times.

Decoded output modes:

- Normal pictures decode to 320 bytes per row for 200 rows.
- Nibble mode expands 160 decoded bytes per row into 320 4-bit pixels, low
  nibble first and high nibble second.
- `picBlit()` is used by the start/title path for an EGA title layout: 700 rows,
  40 bytes per row, planar-style output. This is not the same as a mode-13h
  320x200 page.

For a PNG converter, implement the stream decoder once, then expose separate
output layouts:

- 320x200 indexed image for normal `PIC` and `SPR` sheets.
- EGA title mode for `TITLE640.PIC`. The converter decodes 700 storage rows and
  reconstructs them as a 640x350 indexed image: even decoded rows are the left
  half and odd decoded rows are the right half.
- Palette handling still needs to be mapped from VGA/EGA palette code.
- `1.PIC`-`4.PIC` are confirmed normal 320x200 VGA byte-indexed output with
  embedded palette data; they should not use title planar remap.

For exporter stability, keep codec metadata (modes, widths, stream flags, payload
bytes) in the sidecar so export diffs are meaningful and reproducible across
tool versions.

## 3D Model Containers (`.3D3`)

Signatures are in `src/const.h`:

- `.3D3`: `0x3333`
- `.3DT`: `0x3131`
- `.3DG`: `0x3232`

The main theater model loader is `load3D3()` in `src/eg3dload.c`.

Theater `.3D3` layout:

```text
u16 signature             # 0x3333
u16 shape_offset_count
u16 shape_offsets[shape_offset_count]
u16 model_data_size
u8  model_data[model_data_size]

# optional shared vertex pool follows in theater files
u8  vertex_ref_count
u8  x_index[vertex_ref_count]
u8  y_index[vertex_ref_count]
u8  z_index[vertex_ref_count]
u8  x_count
s16 x_values[x_count]
u8  y_count
s16 y_values[y_count]
u8  z_count
s16 z_values[z_count]
```

`load3D3()` adds a sentinel offset after the loaded offset table:
`shape_offsets[shape_offset_count] = model_data_size`.

Observed theater headers:

| File | Shape offsets | Model bytes |
| --- | ---: | ---: |
| `LB.3D3` | 89 | 23511 |
| `PG.3D3` | 91 | 24806 |
| `VN.3D3` | 96 | 25792 |
| `ME.3D3` | 90 | 25581 |
| `NC.3D3` | 83 | 25978 |
| `CE.3D3` | 92 | 22711 |
| `JP.3D3` | 97 | 39110 |
| `PHOTO.3D3` | 13 | 20477 |

`15FLT.3D3` is the aircraft model file and is loaded by `load15Flt3d3()` in
`src/egmath.c`. It has the same leading signature and offset-table idea, but it
loads into `g_aircraftModels` rather than `g_world3dData`.

Shape selection:

- Shape ids with bit `0x100` set use theater model data:
  `shape_offsets[shape_id & 0x7f]`.
- Other shape ids use `15FLT.3D3` aircraft model offsets.

`PHOTO.3D3` is appended selectively for target-photo models when target slot
flags request it.

## 3D Model Display Lists

The model byte stream is not a simple mesh file. It is a display list interpreted
by `src/eg3drast.c`, with rendering paths in `projectSceneObject()`,
`processSceneObject()`, `renderPrimitiveList()`, and `renderPrimitiveCommand()`.

Important facts for a Blender converter:

- Each model starts with a render-mode byte.
- LOD records can appear before the primitive body. Records whose first byte has
  bit 7 set are skipped or selected based on object distance.
- Some opcodes store `g_spinAngle` into transform slots for animated/spinning
  parts.
- Opcode `0x3f` draws an origin point.
- Opcode `0x3e` draws a run of distance-shaded edges.
- Filled primitives use commands where `(opcode & 3) == 1`.
- Line primitives use visibility masks, edge indices, and color bytes.
- `g_modelEdgeCount > 16` switches to a wider visibility mask.
- Color bytes are remapped through `colorLut[color] + g_objShade`.
- Some theater models use the shared vertex pool from the tail of `.3D3`:
  `x_index`, `y_index`, and `z_index` select entries from the X/Y/Z coordinate
  arrays.

A Blender exporter should emulate the display-list parser and collect vertices,
edges, and filled faces before worrying about exact screen-space rasterization.
A backward conversion/importer to `.3D3` is intentionally unsupported.
It would need to rebuild game-compatible display lists, LODs, color commands, and
optional shared-vertex pools.

## Terrain Tile Placements (`.3DT`)

The egame loader is `load3DT()` in `src/eg3dload.c`. The start/mission parser is
`parseTerrain()` in `src/stparse.c`.

Layout:

```text
u16 signature           # 0x3131
u16 tile_counts[5]      # each observed as 32

for level in 0..4:
    u16 object_count[level][tile_counts[level]]

for level in 0..4:
    for tile in 0..tile_counts[level]-1:
        repeated object_count[level][tile] times:
            s16 x
            s16 y
            s16 z
            u16 shape_word
```

The runtime struct is `TileSceneObject`:

```c
struct TileSceneObject {
    int16 x;
    int16 y;
    int16 z;
    uint8 shape; /* bit7 = dynamic lookup/override, low 7 bits = buf3d3 index */
};
```

The file stores the final field as a 16-bit value. `load3DT()` and
`parseTerrain()` read that word, then store only the low byte in the runtime
tile object. A future round-trip converter should preserve the original 16-bit
`shape_word` in text output even if the current game renderer only uses the low
8 bits.

For converters, `.3DT` is the placement layer: it tells where terrain/tile
objects are placed and which `.3D3` shape they reference.

Parser output keeps the complete raw suffix as `trailing_bytes` so unusual/extended
`.3DT` files preserve non-standard tail data in the sidecar.

## Terrain/Grid Lookup (`.3DG`)

The start parser in `src/stparse.c` matches the observed file size:

```text
u16 signature       # 0x3232
u8  grid1[16]
u8  grid2[256]
u8  grid3[512]
u8  grid4[512]
u8  grid5[512]
```

All observed `.3DG` files are 1810 bytes:

```text
2 + 16 + 256 + 512 + 512 + 512 = 1810
```
If files include additional unknown suffix bytes, keep those bytes as
`trailing_bytes` in the JSON sidecar so custom tooling can preserve non-canonical tails.

`lookupGridCell()` in `src/stterr.c` treats the data as a five-level nested grid:

- Level 4 uses `grid1` as a 4x4 top grid.
- Level 3 uses `grid2` as a 16x16 grid.
- Levels 2, 1, and 0 use `grid3`, `grid4`, and `grid5` with recursive 4x4
  subtiles selected from the parent cell.

This is useful for terrain browsing and for reconstructing a world/tile preview
before full 3D rendering.

## World and Campaign Data (`.WLD`)

The loader is `parseWorld()` in `src/stgen.c`.

Read order:

```text
u8  unknown_header[2]
u16 read_item_size
u16 ground_unit_count
u16 world_object_count
WorldObject world_objects[read_item_size]     # 16 bytes each
u16 flight_unit_count
FlightUnit flight_units[flight_unit_count]    # 36 bytes each
u8  wld_buf7[100]
u8  wld_buf8[100]
u8  object_type_table[100]
u8  terrain_grid[256]
char name_string_table[750]
```

Some legacy files in this repository may stop before the full 750 bytes; the
converter accepts that truncated tail and stores the bytes as the parsed name-table
payload rather than raising a parse error.

`WorldObject` is a packed 16-byte record:

```c
struct WorldObject {
    uint16 unitRef;     /* index into name_string_table offsets */
    uint16 x_coord;
    uint16 y_coord;
    int16 unitType;
    int16 targetFlags;  /* 0x100 airbase, 0x200 large, 0x400 waypoint,
                           0x500 base, 0x800 disabled */
    int16 occupantType;
    int16 patrolCount;
    int16 objectIdx;    /* low 7 bits index wld_obj_table */
};
```

`FlightUnit` is a packed 36-byte record:

```c
struct FlightUnit {
    int16 waypointIdx;
    uint16 x;
    uint16 y;
    uint16 altitude;
    int32 xPrecise;
    int32 yPrecise;
    int16 heading;
    int16 pitch;
    int16 roll;
    int16 planeType;
    int16 flags;
    int16 maxSpeed;
    uint16 fuel;
    uint8 reserved[6];
};
```

Coordinates:

- `WORLD_COORD_SHIFT` is 5.
- Precise coordinates are commonly `world_coord << 5`.
- `terrainGrid[(x >> 0xb) + ((y >> 0xb) * 16)]` maps world coordinates to a
  16x16 terrain grid with cells of 0x800 world units.
- `formatGridRef()` converts world coordinates to map grid references such as
  `TD00`, `JZ00`, `XV00`, `ES00`, `WX00`, `CC00`, and `HZ00`, with per-theater
  offsets.

`exportWorldToComm()` is not a plain `.WLD` writer. It writes most loaded world
state into `commData->worldBuf`, but omits `object_type_table` and appends
mission/runtime fields. A future `.WLD` round-trip converter should preserve the
original load order above, including unknown buffers.

For editor-facing exports, keep `name_table` bytes as parsed and include any
suffix bytes beyond the game payload as `trailing_bytes` in the JSON/YAML
representation so variant or unknown tails are not discarded.

## Manual and Map Facts Useful for RE

The manuals and maps help name and validate data:

- Theater map symbols include airbases, aircraft carriers, SAM sites, radar,
  warships, and target icons.
- HUD and target display colors distinguish target state, weapon range, locks,
  and incoming threats.
- The Desert Storm scenario disk adds a new theater, night missions, and
  precision weapons such as laser-guided bombs and HARM.
- The ref card maps many input names used by code and UI: views `F1`..`F10`,
  director `D`, target search `T`, radar range `R`, zoom `Z`, expand `X`,
  weapon select keys, chaff, flare, missile fire, cannon fire, training mode,
  resupply, detail, joystick center, and volume.

## Kaitai Struct Fit

Kaitai Struct makes sense for the container formats, especially as living
documentation and testable parsers:

- Good fit: `.3DG`, `.3DT`, `.WLD`, `.3D3` headers, offset tables, and vertex
  pool tails.
- Partial fit: `.PIC` and `.SPR` headers plus raw compressed stream framing.
- Poor fit by itself: PIC/LZW decompression, RLE state, and 3D display-list to
  mesh reconstruction.

Recommended approach:

1. Write `.ksy` specs for the structural layers first.
2. Use Kaitai `instances` for model chunks selected by offset tables.
3. Keep PIC decompression and 3D display-list interpretation in normal code,
   called after Kaitai parses the container.
4. For future two-way converters, do not depend only on generated Kaitai writers.
   Preserve unknown bytes and use purpose-built encoders for PIC and display
   lists.

## Converter Roadmap

For image export work:

1. Implement the PIC/LZW/RLE decoder.
2. Decode `PIC` and `SPR` to indexed pixel buffers.
3. Map game palettes for VGA/EGA output.
4. Keep codec metadata (compression mode, widths, code tables, payload bytes) in sidecar for deterministic re-export diffs.

For Blender:

1. Parse `.3D3` containers and `15FLT.3D3`.
2. Implement enough of `eg3drast.c` to extract vertices, edges, faces, colors,
   LOD records, and animated transform markers.
3. Parse `.3DT` placements to instantiate theater shapes.
4. Use `.3DG` and `.WLD` to place terrain/world objects at theater scale.
5. Keep export forward-only: game assets are exported to stable modern formats.

For world/campaign editing:

1. Parse `.WLD` using the exact `parseWorld()` order.
2. Preserve unknown buffers and the name-table payload.
3. Expose `WorldObject`, `FlightUnit`, `terrainGrid`, and object type table as
   editable structured data.
4. Keep `.WLD` workflows forward-only; no rebuild/writer path is planned.
5. Keep parsed `name_table` bytes as they were read (including legacy short
   tables), and append `trailing_bytes` only from bytes after the parsed name
   table.

## Open Questions

- Exact palette sources for each image mode still need to be mapped.
- The first two bytes of `.WLD` files are preserved but not yet named.
- The purpose of `wld_buf7`, `wld_buf8`, and `object_type_table` needs more
  semantic naming.
- The full model display-list opcode grammar needs to be documented from
  `eg3drast.c` before lossless `.3D3` writing is realistic.
- `.SPR` appears to use the PIC decoder path and sprite descriptors define
  sub-rectangles, but a standalone sprite-sheet metadata format was not found.
