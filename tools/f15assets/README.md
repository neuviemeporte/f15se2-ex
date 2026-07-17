# F-15 SE II Asset Tools

Developer and customizer tools for converting original F-15 Strike Eagle II assets into modern, editable files.
The overlapping MicroProse formats used by F-117A are partially supported too,
including external `.PAL` palettes for `PIC` images.

For a step-by-step editing workflow, see `CUSTOMIZATION.md` in this directory.

These tools are forward-conversion first. They are meant to make original game assets inspectable, editable, and eventually loadable by a modernized game runtime when replacement assets are present. They do not currently promise full backward conversion into original binary formats.

Longer-term packaging goal: the replacement pipeline should make it possible to
run the game with freely licensed custom media assets only, for example CC-licensed
models, pictures, fonts, and sounds. That is not the current runtime contract:
today the original assets are still the authoritative fallback and are still
needed for unsupported or incomplete replacement paths.

When both a modern media file and JSON mention the same property, the media file
wins. JSON is supplemental index/metadata only. For example, PNG dimensions and
palette override JSON image metadata, GLB geometry/extras override `.3D3.json`,
WAV headers/data override sound JSON, and BDF/PNG font data overrides font JSON.

Validation invariant: loading an unmodified converted asset should produce the
same in-memory game data as loading the original asset. For formats that pass
through float/modern encodings, the result should be numerically equivalent
within documented conversion tolerance. This comparison belongs in importer
validation/tests or explicit diagnostics, not in normal runtime loading.

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
At runtime, the game searches `converted_assets_all` below the game asset path
and current directory. Set `F15_REPLACEMENT_ROOT=/path/to/converted_assets_all`
to point the loader at an explicit converted/custom asset directory without
copying files beside the original game assets. If `F15_REPLACEMENT_ROOT` points
at a parent folder that contains `converted_assets_all`, that child folder is
also searched. If it points to a missing directory, the loader ignores it and
continues with the normal fallback locations after logging one warning. If the
game is launched from a
directory where `tools/f15assets/cli.py` is not discoverable, also set
`F15_ASSET_TOOL="python3 /path/to/f15se2-ex/tools/f15assets/cli.py"` so JSON and
GLB bridge loaders can rebuild legacy byte streams and GLMESH caches.

## Commands

## Convert all game assets

From the `f15se2-ex` repository root, run:

```bash
python3 -m tools.f15assets.cli convert-all /home/xor/games/f15 converted_assets_all
```

This produces:

- `*.png` for all supported `PIC`/`SPR` image assets.
- `*.json` sidecars for structured/table/model metadata. Bulky PIC/SPR decode
  JSON and font metadata JSON are skipped by default because PNG/BDF are the
  runtime media sources.
- extensionless asset directories for world/model groups, for example `VN/`,
  `LIBYA/`, `15FLT/`, or `WORLD/`.
- combined `.3D3.glb` and per-shape `shape_###*.glb` for supported 3D assets.
  Generated `cache/shape_###*.glmesh` runtime caches are optional and can be
  rebuilt by the game from GLB.
- `fonts/font_<id>.bdf`.
- driver-backed `sounds/voice_cue_*.wav` files. Sound metadata JSON and the
  full digitized sound blob are not exported by default.

Use a different source path if your original DOS game files are not in
`/home/xor/games/f15`.

For F-117A CD assets, use the F-117A game folder as the source:

```bash
python3 -m tools.f15assets.cli convert-tree /home/xor/games/F117A/F117.CD converted_f117_assets
```

Optional flags:

- Add global `--pretty` before `convert-all` for pretty-printed JSON.
- `convert-all` recurses through the game asset folder. Its default output is
  minimized; add `--include-image-json`, `--include-3d3-model-data`,
  `--include-glmesh-cache`, `--include-metadata`, or `--include-raw-blob` only
  for reverse-engineering diagnostics or prebuilt runtime caches.
  `--include-metadata` adds optional font JSON and sound metadata.
- Use `convert-tree` directly if you need non-default model output.
- Use `export-sounds` directly if you need a non-default sample rate.

Convert a whole asset tree:

```bash
python3 -m tools.f15assets.cli convert-tree /home/xor/games/f15 converted_assets_all
```

Use `convert-tree --include-image-json` only when you need lossless PIC/SPR
decoder diagnostics such as compressed payloads and decoded pixel base64.

Decode one asset:

```bash
python3 -m tools.f15assets.cli decode /home/xor/games/f15/VN.WLD converted_assets_all/VN/VN.WLD.json --format WLD
```

Export fonts:

```bash
python3 -m tools.f15assets.cli export-fonts . converted_assets_all/fonts
```

Add `--include-metadata` only when you need optional `font_<id>.json` sidecars
and `fonts.json`; runtime loading uses TTF/OTF first, then BDF, then PNG.

Use TrueType/OpenType fonts directly by placing `font_<id>.ttf` or
`font_<id>.otf` in `converted_assets_all/fonts`. To replace all non-small fonts
with a single face, copy it as `font_1.ttf`, `font_3.ttf`, `font_4.ttf`, and
`font_5.ttf`; leave `font_0.*` absent to keep the tiny HUD font.

Export digitized cue WAVs:

```bash
python3 -m tools.f15assets.cli --pretty export-sounds /home/xor/games/f15 converted_assets_all/sounds --sample-rate 7850
```

Add `--include-raw-blob` only when you also want the full `f15dgtl_raw.wav`
reference export for reverse engineering. Runtime replacement uses the separate
`voice_cue_*.wav` files.

Add `--include-metadata` only when you also want per-cue JSON, unresolved
driver sidecars, and `sounds.json` for reverse-engineering notes.
Rerunning without these flags removes stale optional sound metadata/reference
files from the output folder, so the default sound folder contains only the cue
WAV authoring files.

Validate modern replacements against original loader output outside normal runtime:

```bash
python3 -m tools.f15assets.cli validate-replacements /home/xor/games/f15 converted_assets_all
```

Rebuild full structured JSON to the original runtime binary byte stream:

```bash
python3 -m tools.f15assets.cli build-binary converted_assets_all/VN/VN.WLD.json --format WLD > /tmp/VN.WLD
```

Full `.3D3.json` files with `model_data` can also be rebuilt for the old loader
bridge. Default minimized `.3D3.json` omits `model_data`, so it is validation
and index metadata only.

Expand a per-shape GLB to the simple runtime mesh stream used by the OpenGL backend:

```bash
python3 -m tools.f15assets.cli build-glmesh converted_assets_all/VN/shape_049_SAM_Radar.glb > converted_assets_all/VN/cache/shape_049_SAM_Radar.glmesh
```

`*.glmesh` files are strict generated caches, not hand-editing sources. The
runtime and validator reject unsupported primitive modes, bad line/triangle
vertex counts, stale source checksums, empty meshes, and trailing unread bytes.
Delete a cache after editing the matching `.glb`; the game can rebuild it.

The validator currently compares original `PIC`/`SPR` decode output with indexed
PNG replacements byte-for-byte at the palette-index level, compares embedded or
externally attached image palettes when the source palette is known, compares
`F15DGTL.BIN` cue ranges with `sounds/voice_cue_*.wav` sample bytes, compares
cue WAV sample rates against the recovered/export default, compares in-repo font
tables with `fonts/font_<id>.bdf` and `fonts/font_<id>.png`, rebuilds `WLD`,
`3DT`, and `3DG` bytes from JSON, compares minimized `.3D3.json` shape-slot
metadata against the original `.3D3`, rebuilds and byte-compares full
`.3D3.json` when bulky `model_data` is present, compares unmodified per-shape
GLBs against the original `.3D3` export for primitive/source-metadata coverage,
and checks `.glmesh` caches against their source GLBs when caches are present. Use
`--require-all` when missing supported authoring replacements should fail the
check; for `.3D3`, this means the minimized JSON index and per-shape GLBs, not
the combined overview GLB. Use `--require-generated-cache` only when generated
runtime caches must also exist.
Use `--strict-original-proof` for converter regression checks; it implies
generated cache and GLB source-proof requirements.

Recommended validation modes:

- Edited/custom asset pack loadability:
  `python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --loadability-only`
  For source-free packs, `/path/to/F15_GAME` may be a missing placeholder path
  in `--loadability-only`; the command then checks modern files directly from
  `converted_assets_all` and skips original-equivalence proof. The output path
  may also be a parent folder containing `converted_assets_all`; the validator
  will use that child folder automatically and print the normalized directory.
  Do not use
  `--require-all` in that source-free mode because there is no original asset
  inventory to require against. If no replacement files are found, the validator
  prints a warning so a wrong output path is visible.
- Custom GLB check:
  `python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --allow-custom-glb-differences`
- Converter regression proof:
  `python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --require-all --strict-original-proof`

For PNG, WAV, font, and JSON table replacements, normal `validate-replacements`
still checks original equivalence. After intentionally editing those assets, use
`--loadability-only` to check modern file parseability and structure without
treating expected content differences as failures. For JSON table replacements,
loadability mode still rebuilds the replacement and requires a non-empty byte
stream for the legacy loader bridge. In loadability mode, source-free
`*.WLD.json`, `*.3DT.json`, and `*.3DG.json` files under the converted output
tree are also rebuilt even when the original binary asset is absent; byte
equivalence still requires the original files. Source-free PNG files under the
converted output tree are also checked for runtime-loadable dimensions and
indexed-palette requirements, except font atlases which are handled by font
validation. Source-free `.glb` and `.glmesh` files under the converted output
tree are also checked for parseability and non-empty renderable primitives;
primitive/order/color equivalence still requires the original `.3D3` files and
converter source metadata. Source-free minimized `.3D3.json` files are checked
for parseable shape-slot metadata, monotonic offsets, and valid
`model_data_size`; exact slot-table equivalence still requires the original
`.3D3`. For sound cues, WAV files must parse as
mono unsigned 8-bit PCM, contain at least one sample, and carry the playback
sample rate in the RIFF header. If `F15DGTL.BIN` is absent, sound validation
still checks cue WAV loadability but skips legacy sample-byte comparison.
Extra `sounds/voice_cue_*.wav` files are also parsed in loadability mode, but
only the known cue names are used by the current runtime playback path. For
BDF fonts, expected glyphs must exist with
non-empty bitmap rows and positive advance widths; in `--loadability-only`, a
bad BDF is only a warning when the matching PNG atlas fallback is loadable.
Extra `fonts/font_*.bdf` files are also parsed in loadability mode so
source-free/custom font packs can be checked without proving equivalence to the
built-in font tables; they must include all 96 glyphs from U+0020 through
U+007F with non-empty rows and positive advance widths. Source-free
`fonts/font_*.png` atlases are also checked
for the current runtime font ids whose cell dimensions are known.
`--require-all` requires at least one runtime font source for each known font:
BDF first, or PNG atlas fallback.
Indexed PNG replacements must include an embedded palette. Per-shape GLBs must
contain at least one triangle, line, or point primitive.
`--loadability-only` is intentionally incompatible with `--strict-original-proof`
and `--require-source-proof`; those modes are for proving original equivalence,
not customized asset loadability. It also supersedes
`--allow-custom-glb-differences`, so do not combine those two custom modes.

## Converted formats

| Original | Modern output | Notes |
| --- | --- | --- |
| `*.PIC`, `*.SPR` | indexed PNG, optional decode JSON | PNG is authoritative for pixels, dimensions, and palette. `convert-tree --include-image-json` writes bulky decode/index metadata for diagnostics. |
| F-117A `*.PIC` + `*.PAL` | indexed PNG, optional decode JSON | PNG is authoritative after palette application. Same-stem `.PAL` is used first; known aliases handle files such as `256LEFT.PIC` using `FLIGHT.PAL` and `CLIMBIN.PIC` using `ADV.PAL`; otherwise `PALETTES.PAL` chunk 0 is used as an F-117A fallback. |
| `TITLE640.PIC` | `640x350` PNG, optional decode JSON | Stored as alternating left/right rows by the original 640-mode loader; runtime loads the PNG into the hi-res title surface when present. |
| `1.PIC`..`4.PIC` | VGA-style PNG, optional decode JSON | Demo-only images use a different byte-indexed loader path. |
| `*.3D3` | asset/world directory with combined `.glb`, per-shape `.glb`, optional `cache/*.glmesh` runtime caches, and lightweight JSON index | GLB is authoritative for OpenGL geometry and model metadata; `.glmesh` is generated from GLB for OpenGL runtime loading and stores the source GLB checksum. Default JSON keeps only lightweight ids and lookup metadata. Full JSON with `model_data` can be rebuilt into legacy `.3D3` bytes for the old loader bridge, but default minimized JSON cannot. Use `convert-tree --include-3d3-model-data` only for full reverse-engineering or bridge dumps, and `--include-glmesh-cache` only if you want prebuilt caches. |
| `*.3DT` | ordered JSON sidecar in the related world directory | Terrain object placement tables. |
| `*.3DG` | ordered JSON sidecar in the related world directory | Terrain grid lookup data. |
| `*.WLD` | world directory with ordered JSON sidecar | Theater/world objects, terrain grid, mission object type table, flight-unit templates, and names. |
| fonts | BDF, optional JSON | BDF is authoritative for glyph shapes, Unicode codepoints, and metrics. JSON maps codepoints/index metadata only with `--include-metadata`; it does not override BDF glyph data. |
| `F15DGTL.BIN` | cue WAVs, optional JSON, optional raw WAV | `voice_cue_*.wav` files are authoritative for runtime cue replacement. Current runtime replacement accepts mono unsigned 8-bit PCM cue WAVs and uses their RIFF sample rate for playback speed. JSON keeps cue/index metadata only when `--include-metadata` is requested. The full raw blob is not needed for customization; use `--include-raw-blob` only for reverse-engineering reference. |
| sound drivers | optional JSON sidecars | Driver executable hashes, sizes, and unresolved status are preserved only when `--include-metadata` is requested; executable bytes are not duplicated into custom asset sidecars. |

## 3D model customization

`3D3` assets are exported to glTF binary (`.glb`) for Blender and other modern tools.
Bulk conversion writes related world and shape files into the same extensionless directory.
For example:

```text
converted_assets_all/WORLD/
  WORLD.WLD.json
  WORLD.3DT.json
  WORLD.3DG.json
  WORLD.3D3.glb
  shape_000.glb
  shape_001.glb

converted_assets_all/15FLT/
  15FLT.3D3.json
  15FLT.3D3.glb
  shape_000.glb
  shape_001.glb
```

The combined GLB is useful for overview and old viewer workflows. The `shape_###`
per-shape GLBs are the preferred files for editing one model slot without
opening every shape in the source container. Per-shape GLBs are minimized:
unused accessors, bufferViews, materials, and binary buffer ranges from the
combined GLB are not copied into the single-shape file. `.glmesh` files are
generated runtime caches in `cache/` derived from the matching per-shape GLB.
Runtime lookup keys off the stable `shape_###` slot prefix and is
case-tolerant; human-readable labels after the number are optional.
Default conversion does not pre-generate them; use `--include-glmesh-cache` if
you want cache files ahead of first runtime load. Each cache stores the MD5
checksum of the source GLB. Edit the GLB directly; the OpenGL runtime loader
rebuilds the cache automatically when it is missing or its checksum does not
match the GLB.
`PHOTO.3D3` is authored the same way in `converted_assets_all/PHOTO/`, even
though the legacy loader appends those target-view models to the active theater
shape table at runtime. The OpenGL replacement lookup maps those appended slots
back to `PHOTO/shape_###*.glb`.
Bulk conversion writes minimized `.3D3.json` indexes without `model_data`
base64. Use `decode` or `convert-tree --include-3d3-model-data` only when you
need a full reverse-engineering dump that can rebuild the original `.3D3` bytes
for the old loader bridge. If the runtime finds a minimized `.3D3.json`, the
bridge rejects it and falls back to the original `.3D3`; OpenGL geometry can
still come from per-shape GLB.
Current generated caches use the `F15GLM3` header, which also stores source
primitive kind/index/raw-color/order flags per runtime primitive. The renderer does not
need those fields to draw, but validators and diagnostic runs use them to prove
that cache generation did not merge, reorder, or drop order-sensitive `.3D3`
geometry or lose palette/color identity.
Older `F15GLM1`/`F15GLM2` caches can still be read by the runtime, but strict
test-time validation cannot use them to prove source primitive order.

Theater aliases are grouped with their world file: `LIBYA.WLD` with `LB.3D3`,
`LB.3DT`, and `LB.3DG` under `LIBYA/`, and `GULF.WLD` with `PG.3D3`,
`PG.3DT`, and `PG.3DG` under `GULF/`.

Important constraints:

- Shape ids must remain stable. Do not compact or renumber shapes if the game will use the edited asset as a replacement.
- Some original shapes are intentionally 2D or single-sided. The exporter keeps one-sided planes instead of fabricating backfaces, because duplicated reverse faces can create z-fighting.
- Source line and point primitives are exported in original order with duplicates
  preserved; do not deduplicate antennas, masts, deck lines, or other line-only
  details.
- Source face triangles are also exported in decoded order with order-sensitive
  metadata. This deliberately avoids color-batching triangles, because coplanar
  or overlapping faces can depend on draw order.
- Some theater model slots are non-renderable placeholders. They are skipped in GLB nodes but preserved in sidecar metadata.
- Shape names are not embedded in `.3D3`; names are derived from matching `.WLD` object labels where possible.
- For runtime replacement, prefer `.glb` for 3D geometry. The default
  `.3D3.json` sidecar is an index/manifest only and should not duplicate model
  bytecode; full bridge dumps may include `model_data` only when explicitly
  generated with `--include-3d3-model-data`. `cache/*.glmesh` is generated data
  and can be deleted safely.
- Non-coplanar triangle order is normally harmless, but coplanar or z-fighting
  geometry may depend on source draw order; preserve order unless a future
  validator proves the reordering is render-equivalent.
- GLB `extras` are diagnostic metadata. Geometry and material colors load from
  the GLB itself, but if Blender or another editor strips primitive extras then
  old-vs-new validation can no longer prove original source order/raw-color
  equivalence for that edited shape.

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

Primary and secondary mission targets are not fixed fields in `.WLD`. The start-module mission generator chooses them dynamically from world objects, `mission_object_type_table`, terrain constraints, and `targetFlags`.

Proven `.WLD` table names:

```text
terrain_target_ids.land/water       first two bytes; copied to g_landTargetId/g_waterTargetId
shape_target_category_table         copied to g_shapeTargetCategory and used by weapon/target compatibility
kill_tally_or_unit_flags            copied to g_tileKillTally, then exported to END as worldUnitFlags
mission_object_type_table           used by START mission generation against missionTable[].tensionMask
```

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

The `.WLD` format does not expose a simple universal friend/enemy enum. Some runtime flags imply combat behavior, but faction semantics are partly generated at mission load from `targetFlags`, `flight_units[].flags`, generated target slots, and runtime object placement.

## Sound cues

`F15DGTL.BIN` is exported as driver-backed cue WAVs recovered from ASOUND.
Customizers should edit only the individual `voice_cue_*.wav` files. Per-cue
JSON and driver sidecars are optional via `export-sounds --include-metadata`.
The full raw blob WAV is optional via `export-sounds --include-raw-blob` and is
meant only as reverse-engineering reference output.

ASOUND cue ranges are inclusive in the driver:

```text
audio_playSample(0): 0x0000..0x31F3
audio_playSample(4): 0x31F4..0x4796
audio_playSample(2), variant 0: 0x4797..0x5C92
audio_playSample(2), variant 1: 0x5C93..0x6A1A
audio_playSample(2), variant 2: 0x6A1B..0x7D9D
```

`voiceCueThresholds` in the game code are availability checks before playback, not the full cue split table.

The exported sample rate defaults to `7850` Hz, and the runtime uses the same
recovered rate for legacy `F15DGTL.BIN` cue fallback. This comes from recovered
driver timer behavior in the 8 kHz range, not from a WAV header in the original
asset. Replacement WAV headers are authoritative at runtime.

## Runtime replacement recommendation

Runtime loading currently uses path-based lookup, not a required manifest. If a
future pack manifest is added, it should point to the same grouped files:

```json
{
  "VN.WLD": { "world": "VN.WLD.json" },
  "VN.3D3": { "geometry": "VN.3D3.glb", "index": "VN.3D3.json" },
  "TITLE640.PIC": { "image": "TITLE640.png", "metadata": "TITLE640.json" },
  "F15DGTL.BIN": { "sounds": "sounds/voice_cue_*.wav" }
}
```

For replacement loading, always prefer the modern media artifact over JSON when
both contain overlapping facts. JSON exists to locate assets and preserve
non-media ids/flags/ranges that the media container does not represent.

Current runtime implementation status:

- A shared resolver searches converted asset directories such as `VN/`,
  `LIBYA/`, `GULF/`, `15FLT/`, and `PHOTO/`.
- Replacement lookup is case-tolerant for modern filenames and grouped model
  directories, so packs converted from lowercase or uppercase DOS asset trees
  can still be found on case-sensitive filesystems.
- Legacy file loads detect preferred modern replacements and log that the
  original loader remains active until that format has a native modern importer.
  Current probes cover `.3D3 -> .glb` for OpenGL geometry, full `.3D3.json` for
  legacy-byte-stream bridge dumps, `.PIC/.SPR -> .png`, and
  `.WLD/.3DT/.3DG -> .json`.
- PIC/SPR PNG replacement is used by the shared start/end image wrappers and by
  the flight cockpit/side-view picture path, so converted cockpit art can be
  edited as PNG without touching the original `.PIC`.
- The shared resolver can also locate per-shape 3D replacements by stable slot
  id, such as `VN/shape_049_SAM_Radar.glb`. The OpenGL backend can load these
  through generated `cache/*.glmesh` files and rebuild missing or stale caches
  from GLB through the `build-glmesh` bridge. The current loader still reads
  legacy `.3D3` bytes for shape-slot tables, PHOTO append ranges, comparison,
  and software fallback; GLB-only/free-asset runtime support requires moving
  those table reads to modern metadata.
  Full `.3D3.json` files that include `model_data` can be rebuilt through the
  same JSON bridge as WLD/3DT/3DG and fed to the old loader. Default minimized
  `.3D3.json` files are detected as metadata-only and do not trigger that
  bridge.
- Digitized sound runtime loading now prefers separate
  `sounds/voice_cue_*.wav` files when present. Each WAV must be uncompressed
  mono unsigned 8-bit PCM; its RIFF sample rate controls replacement playback
  speed. Missing cue files fall back to the matching range in `F15DGTL.BIN`;
  empty or invalid WAVs are rejected and also fall back to the legacy cue range.
  Old-vs-new WAV equivalence is checked by `validate-replacements` and CTest,
  not by normal gameplay.
- PIC/SPR screen and sprite loads now prefer matching PNG replacements when
  present. Indexed PNGs are copied as palette indices and their embedded palette
  is required and applied to the active DAC; truecolor PNGs are accepted by
  mapping pixels to the current game palette. Palette-less indexed PNGs are
  rejected and fall back to legacy PIC/SPR. Replacement PNGs are sampled into
  the existing game target rectangle, so higher-resolution source images do not
  enlarge menus, cockpits, or sprite sheets. Pixel and palette equivalence is
  checked by `validate-replacements` and CTest, not by normal gameplay.
  Menu/debrief disk-presence gates for sprite sheets also accept matching PNG
  replacements before prompting for original disks.
- WLD/3DT/3DG loads now prefer matching JSON replacements when present. Runtime
  import uses the converter's `build-binary` command to rebuild the same byte
  stream the legacy loaders expect, then feeds those bytes into the existing
  game loaders. This keeps loaded memory compatible while letting JSON be the
  editable source. Set `F15_ASSET_TOOL` to override the command prefix; by
  default runtime first looks for `tools/f15assets/cli.py` beside the
  replacement asset's `converted_assets_all` root, then tries nearby script
  paths, then falls back to `python3 -m tools.f15assets.cli`. JSON-rebuilt byte
  equivalence is checked by `validate-replacements` and CTest.
- Font drawing now prefers `fonts/font_<id>.ttf` or `.otf` when present and the
  build has FreeType. The scalable font is fitted into the same in-memory glyph
  rows and advance-width tables used by the original renderer, so layout and
  clipping stay compatible. If TTF/OTF is absent or unavailable, the loader
  falls back to BDF, then PNG atlas, then built-in fonts. Built-in font
  equivalence is checked by `validate-replacements` and CTest.
- Runtime GLB import is implemented for the OpenGL backend through a simple
  runtime mesh bridge generated from per-shape GLBs. The backend treats `.glb`
  as the source of truth, loads `cache/*.glmesh` only when the embedded source
  checksum matches, and rebuilds missing or stale caches through `build-glmesh`.
  Empty generated runtime meshes are rejected and fall back to legacy `.3D3`;
  stale or empty caches are logged before rebuild/fallback. Replacement runtime
  mesh equivalence is checked by `validate-replacements` and CTest.
  Software rendering still uses the original `.3D3` stream. Exported media files
  are authoritative over JSON metadata where applicable.

## Replacement comparison modes

Comparison is test/developer tooling only:

- `python3 -m tools.f15assets.cli validate-replacements <asset_dir> <converted_dir>`
  compares exported replacements against original assets outside the game. It
  recurses by default, matching `convert-all`; use `--no-recursive` for a
  flat-folder check.
- CTest `asset_runtime_loader_validation_tests` loads old and modern assets
  through the real runtime loader paths and compares the resulting data.

`--require-all` applies to authoring replacements such as PNG, BDF, WAV, JSON
tables, and per-shape GLB. Generated `cache/*.glmesh` files are validated when
present and required only with `--require-generated-cache`.
GLB source primitive extras are proof metadata for unmodified conversions; a
Blender-edited GLB may strip or alter them and still be a valid custom model.
Use `--require-source-proof` when metadata loss should fail validation.
`--loadability-only` ignores combined overview GLBs and converter-only proof
metadata; the runtime-relevant requirement is that each per-shape GLB parses and
contains renderable triangles, lines, or points.

Runtime comparison helpers are split out of individual loaders under
`src/shared/asset_compare*.c`. Put byte-stream checks in
`asset_compare_bytes.c`, indexed image/palette checks in
`asset_compare_image.c`, bitmap font checks in `asset_compare_font.c`, digitized
cue checks in `asset_compare_sound.c`, and renderer-independent 3D
topology/color/source-metadata reporting in `asset_compare_3d.c`. Tool-side
`validate-replacements` diff helpers live in
`tools/f15assets/f15assets/validation_compare.py` as a stable facade over
per-format `validation_compare_*` modules. The heavier per-format validation
orchestration lives in `validation_image.py`, `validation_font.py`,
`validation_sound.py`, `validation_structured.py`, and `validation_model3d.py`;
each module exposes a `validate_*` entry point. `cli.py` should only wire
commands and repository/output path policy.

Runtime diagnostics currently cover:

- `PIC`/`SPR`: replacement PNG indices against the legacy PIC decoder output.
- `WLD`/`3DT`/`3DG`: JSON-rebuilt byte stream against the original binary before
  the existing game loader consumes the replacement stream.
- Fonts: replacement BDF/PNG glyph rows, metrics, and widths against built-in
  font tables before installation.
- Digitized cues: replacement WAV samples against the matching `F15DGTL.BIN`
  byte range.
- OpenGL 3D: replacement GLB topology and raw source-color coverage against
  decoded legacy `.3D3` face/line/point coverage; generated `cache/*.glmesh`
  files are compared when present.

## Python package notes

The package is intentionally simple and local:

```text
tools/f15assets/cli.py
tools/f15assets/f15assets/*.py
tools/f15assets/tests/*.py
```

No install step is required when running from the repository root with `python3 -m tools.f15assets.cli`.
The game runtime also searches for `tools/f15assets/cli.py` beside the
replacement asset's `converted_assets_all` root and in nearby relative paths
before falling back to module execution; use `F15_ASSET_TOOL` if the tool lives
elsewhere.

The tests are smoke/round-trip helpers for converter development. Run them only when you intentionally want validation work.

`validate-replacements` is the explicit old-vs-modern test/developer comparison path.
It currently validates `PIC`/`SPR` PNG pixel indices and known embedded/external
palettes, digitized sound cue WAVs, WLD/3DT/3DG JSON replacements, BDF/PNG font
replacements, `.3D3` JSON slot metadata, and `.3D3` GLB structure. The `.3D3`
JSON check verifies the minimized shape offset table and model data size without
requiring bulky `model_data`; this is the metadata a future GLB-only/free-asset
runtime needs before original `.3D3` files can be omitted. If bulky `model_data`
is present, validation also rebuilds the `.3D3` byte stream and compares it with
the original file. The GLB check verifies combined GLB metadata, renderable
shape count, per-shape GLB presence, minimized per-shape marker, stable
`source_shape_index` metadata, primitive/source-metadata coverage against a
fresh export from the original `.3D3`, and `.glmesh` cache equivalence with the
source GLB. Full
runtime mesh primitive counts are also compared against the source GLB so
geometry drops are reported explicitly. `F15GLM3` source primitive metadata is
compared against both the original export and the source GLB, so validation
catches accidental primitive merging, reordering, or raw-color metadata loss
when `--require-source-proof` is used. Without that flag, missing or changed
source proof metadata is reported as a warning so custom GLBs remain acceptable.
Use `--allow-custom-glb-differences` when edited per-shape GLBs intentionally
change geometry counts or no longer match the original `.3D3` proof metadata.
Use `--loadability-only` for edited packs when you want structural validation
without old-vs-new equality checks.
For image replacements, full equality validation requires indexed PNGs because
that is the only mode that preserves original palette indices exactly.
`--loadability-only` accepts truecolor PNGs, matching the runtime convenience
path that maps truecolor pixels to the active game palette.
Expanded runtime vertices and RGBA values are compared with a small float
tolerance so cache validation catches coordinate/color drift without depending
only on exact byte equality. Stale root-level `shape_*.glmesh` files are
reported as invalid; generated runtime caches belong under `cache/`.
Missing generated caches are not failures unless `--require-generated-cache` is
specified, because the runtime can rebuild them from GLB.

`validate-replacements` is intended for proving an unmodified conversion. After
intentional customization, differences from the original `.3D3`, PNG, WAV, or
font source may be expected; runtime fallback and cache checks still apply, but
old-vs-new equality is no longer the success criterion for that edited asset.
