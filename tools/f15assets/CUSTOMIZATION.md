# F-15 Asset Customization Guide

This guide describes the practical edit workflow for converted assets. The
rule is simple: edit the modern media file whenever it can represent the data,
and touch JSON only for tables, ids, placement, or metadata that the media file
cannot store.

Converted metadata should be portable. Sidecars should use original asset names
or paths relative to the converted folder, not absolute developer-machine paths.

## 1. Convert the original game folder

Run from the repository root:

```bash
python3 -m tools.f15assets.cli convert-all /path/to/F15_GAME converted_assets_all
```

Use your installed DOS game folder for `/path/to/F15_GAME`. The output folder is
the folder the game replacement loader and editor tools should use.
`convert-all` recurses through the game folder so campaign/theater subdirectory
assets are converted too.
Default output is minimized for customization. Use `--include-image-json`,
`--include-3d3-model-data`, `--include-metadata`, or `--include-raw-blob` only
when you need reverse-engineering diagnostics.

To run the game with replacements stored outside the original game folder, set:

```bash
F15_REPLACEMENT_ROOT=/path/to/converted_assets_all
```

The runtime then searches that folder for PNG, JSON, BDF, WAV, GLB, and GLMESH
replacements before falling back to the usual converted/output locations. If
you point `F15_REPLACEMENT_ROOT` at the parent folder instead, a
`converted_assets_all` child is also searched. A missing replacement root is
reported once, ignored, and the usual fallback locations are still searched.
If the game is launched from a directory where the repository `tools` folder is
not discoverable, also set:

```bash
F15_ASSET_TOOL="python3 /path/to/f15se2-ex/tools/f15assets/cli.py"
```

That bridge command is used only when the runtime must rebuild JSON tables or
generated GLMESH caches from editable modern files.

## 2. Check replacement equivalence before editing

Run this once after conversion if you want proof that the unmodified converted
files still load like the original assets:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --allow-custom-glb-differences
```

For strict converter regression proof before editing, use:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --require-all --strict-original-proof
```

After editing PNG, WAV, font, JSON, or GLB assets, use loadability validation:

```bash
python3 -m tools.f15assets.cli validate-replacements /path/to/F15_GAME converted_assets_all --loadability-only
```
For a source-free custom pack without the original game folder, the first path
may be a missing placeholder when `--loadability-only` is used. In that mode the
validator checks modern files directly from `converted_assets_all` and skips
original-equivalence proof. The output path may be either the `converted_assets_all`
folder itself or a parent folder containing it; the validator prints the
normalized directory when it descends to the child folder. Do not combine
source-free validation with `--require-all`; completeness checks need the
original asset inventory. If no replacement files are found, the validator
prints a warning so a wrong output path is visible.
Do not combine `--loadability-only` with `--strict-original-proof` or
`--require-source-proof`; those flags are for unmodified converter proof.
`--loadability-only` already permits custom GLB content differences, so do not
combine it with `--allow-custom-glb-differences`.

The validator compares original loader output with converted PNG, WAV, BDF/PNG,
JSON, GLB, and GLMESH replacement data where implemented.
For sound-only custom packs without `F15DGTL.BIN`, validation still parses and
checks the separate cue WAVs; it cannot prove byte-equivalence to the original
blob because there is no original blob to compare.
For source-free world/table packs, `--loadability-only` also rebuilds
`*.WLD.json`, `*.3DT.json`, and `*.3DG.json` files found under the converted
output folder even when the original binary tables are absent.
For source-free image packs, `--loadability-only` also checks extra PNG files
under the converted output folder for runtime-loadable dimensions and embedded
palette requirements for indexed PNGs. Original-equivalence still requires the
matching original `PIC`/`SPR` file.
For source-free 3D packs, `--loadability-only` also checks extra `.glb` and
`.glmesh` files under the converted output folder for parseability and non-empty
renderable primitives. Exact primitive/order/color proof still requires the
matching original `.3D3` file.
It also sanity-checks source-free minimized `.3D3.json` index files for
parseable slot metadata, monotonic offsets, and valid `model_data_size`.
For source-free font packs, `--loadability-only` also parses extra
`fonts/font_*.bdf` files and checks that glyph rows and advance widths are
usable for all 96 runtime glyphs from U+0020 through U+007F. It also checks
`fonts/font_*.png` atlases for the current runtime font ids whose cell
dimensions are known. Built-in font equivalence still requires the in-repo
captured font tables.

Use equivalence checks before editing. After intentional customization,
differences from the original asset are expected for the edited file, so
loadability validation is the useful check for customized packs.
The normal command treats generated `cache/*.glmesh` and GLB source proof extras
as optional diagnostics. `--allow-custom-glb-differences` also treats intentional
per-shape GLB geometry changes as warnings. `--strict-original-proof` requires
generated caches and source proof metadata.

Comparison is intentionally not done while playing. Use
`validate-replacements` or CTest before editing when you need old-vs-modern
proof; use `--loadability-only` after intentional customization.

## 3. 3D models: edit GLB in Blender

Authoritative file:

```text
converted_assets_all/<group>/shape_###_<name>.glb
```

Examples:

```text
converted_assets_all/15FLT/shape_010.glb
converted_assets_all/VN/shape_049_SAM_Radar.glb
converted_assets_all/PHOTO/shape_000.glb
```

Normal Blender workflow:

1. Import the per-shape `.glb` into Blender.
2. Edit geometry, line objects, point objects, and materials.
3. Export back to the same `.glb` path.
4. Keep the `shape_###` number in the filename unchanged.
5. Start the game with the OpenGL backend; it rebuilds `cache/*.glmesh`
   automatically if the cache is missing or stale.

The filename label after `shape_###` is only for humans. Runtime lookup is
case-tolerant and uses the stable slot number prefix.

Current limitation: GLB model replacement is implemented in the OpenGL backend.
The software 3D backend still renders the original `.3D3` stream, and the
current loader still reads original `.3D3` bytes for shape-slot tables, PHOTO
append ranges, comparison, and fallback. The minimized `.3D3.json` index now
validates those slot fields, but the game does not yet use it as the runtime
source for GLB-only/free-asset packs.

Do not edit these for normal model customization:

```text
converted_assets_all/<group>/<group>.3D3.json
converted_assets_all/<group>/cache/*.glmesh
```

`.3D3.json` is a lightweight index/manifest. It preserves shape slots and source
container metadata, but GLB geometry and GLB primitive extras override it. Bulk
conversion omits bulky `.3D3` `model_data` base64 by default; validation still
compares the minimized shape offset table and `model_data_size` against the
original `.3D3`. Request bulky `model_data` only with
`convert-tree --include-3d3-model-data` for reverse-engineering dumps or for
the legacy-byte-stream bridge. Those full dumps duplicate original model data
and are not the normal customization surface.
`cache/*.glmesh` is generated runtime data derived from GLB and can be deleted.
Default conversion does not pre-generate it; use `--include-glmesh-cache` only
when you want caches ahead of first runtime load. The runtime and validator
parse these caches strictly, so malformed or stale caches are rejected instead
of being treated as an alternate editable model source.

Keep these constraints:

- Preserve the shape slot number. World objects reference shape ids.
- `PHOTO/shape_###.glb` files replace target-view photo models. The legacy
  loader appends those models to the theater shape table, but the OpenGL
  replacement lookup maps the appended slots back to `PHOTO/shape_###`.
- Preserve line-only details such as antennas, masts, deck lines, runway lines, and ship rails unless you intentionally remove them.
- Preserve coplanar faces and draw-sensitive overlaps when visual fidelity matters. The converter stores source primitive metadata so validation can detect dropped or reordered primitives.
- Use per-shape GLBs for editing. The combined `<group>.3D3.glb` is mainly for overview and inspection.
- GLB `extras` are diagnostic metadata, not the drawing source. If Blender or
	  another editor strips primitive extras, the model can still render from GLB
	  geometry/materials, but strict `validate-replacements` has less proof that
	  source order/raw colors still match the original.

When JSON is needed:

- Rename or document shape slots in metadata.
- Inspect source offsets or original shape order.
- Debug conversion issues.

When JSON is not needed:

- Changing mesh vertices.
- Changing face colors/materials.
- Adding/removing Blender geometry inside the same shape file.
- Re-exporting the same shape from Blender.

## 4. World and mission data: edit WLD JSON

Authoritative file:

```text
converted_assets_all/<world>/<world>.WLD.json
```

Use the map editor for placement edits:

```text
tools/f15assets/map_editor.html
```

World JSON is the source of truth for:

- object coordinates;
- object shape ids;
- target flags;
- takeoff/landing/base-like records;
- flight-unit templates;
- terrain and mission classification tables;
- name strings.

GLB files do not store world placement. If you move a SAM site, runway, carrier,
or target on the theater map, edit the WLD JSON through the editor.

Runtime note: the current loader rebuilds a legacy `.WLD` byte stream from this
JSON and feeds it to the existing game loader. JSON is still the editable source
because world data is structured table data, not a media file.

## 5. Terrain tables: edit 3DT and 3DG JSON carefully

Authoritative files:

```text
converted_assets_all/<group>/<file>.3DT.json
converted_assets_all/<group>/<file>.3DG.json
```

Use these for terrain/tile placement and lookup data. These formats are numeric
tables, not artwork. Keep record order stable unless you understand the runtime
lookup path.

Runtime note: as with WLD, the current loader rebuilds legacy bytes from JSON
before calling the existing table loaders.

## 6. Pictures and sprites: edit indexed PNG

Authoritative files:

```text
converted_assets_all/<asset>.png
```

PNG is authoritative for:

- pixels;
- dimensions;
- embedded palette.

PIC/SPR JSON is only decode/index metadata and is not emitted by default by
`convert-tree`/`convert-all`. For normal art changes, edit the PNG and keep it
indexed/paletted when possible. Truecolor PNG can be accepted by the runtime as
an editing convenience, but indexed PNG with an embedded palette is the safest
self-contained replacement.

Use `ctest -R asset_replacement_full_validation` or
`python3 -m tools.f15assets.cli validate-replacements` to compare indexed PNG
replacements against the original PIC/SPR pixel indices and active legacy DAC
palette. This proof is intentionally a test-time check, not a runtime game mode.

`TITLE640.PIC` is exported as a 640x350 PNG. It is also loaded as PNG at runtime
when present, using the hi-res title surface rather than the normal 320x200 page
surface.

## 7. Fonts: edit TTF/OTF or BDF

Authoritative files:

```text
converted_assets_all/fonts/font_<id>.ttf
converted_assets_all/fonts/font_<id>.otf
converted_assets_all/fonts/font_<id>.bdf
```

Preferred workflow:

1. Use `font_<id>.ttf` or `font_<id>.otf` for normal scalable-font replacement.
2. Use `font_<id>.bdf` only when you need hand-edited pixel glyphs and metrics.
3. Ignore JSON for normal customization; `font_<id>.json` is optional metadata
   emitted only with `--include-metadata`.

TTF/OTF is preferred for Unicode/localization because customizers can use normal
font editors and do not need sidecar files. Runtime loads TTF/OTF first when the
build has FreeType, then BDF, then legacy `font_<id>.png` atlases. Leave
`font_0.*` absent to keep the original tiny HUD font. JSON cannot override font
glyph data.

## 8. Sounds: edit separate WAV cues

Authoritative files:

```text
converted_assets_all/sounds/voice_cue_*.wav
```

`sounds/f15dgtl_raw.wav` is not exported by default. It is only a full-blob
reference export when `export-sounds --include-raw-blob` is used. Customizers
do not need it. The current runtime replacement path uses the separate
`voice_cue_*.wav` files for playback.

WAV files are authoritative for:

- sample bytes;
- sample rate;
- channel count;
- bit depth.

Use mono unsigned 8-bit PCM for maximum compatibility with the current loader.
The current runtime cue loader accepts mono unsigned 8-bit PCM cue WAVs and uses
their sample bytes and RIFF sample rate as replacements for the matching ASOUND
cue ranges. The default exported sample rate is `7850` Hz; changing a cue WAV's
sample rate intentionally changes playback speed. JSON keeps cue ranges and
driver metadata; do not edit JSON for normal sample replacement. Cue JSON,
driver sidecars, and `sounds.json` are not exported by default; use
`export-sounds --include-metadata` only when you need reverse-engineering notes.
Rerunning the exporter without metadata/raw flags removes stale optional sound
sidecars, leaving the cue WAVs as the normal customization files.
`--loadability-only` also parses extra `sounds/voice_cue_*.wav` files in a
source-free pack, but the current runtime playback path uses only the recovered
known cue filenames exported by `export-sounds`.

## 9. What the game loads first

When a modern replacement exists, the runtime should prefer it:

| Asset class | Preferred replacement |
| --- | --- |
| 3D shape | per-shape `.glb`; `cache/*.glmesh` is generated from it |
| PIC/SPR image | `.png` |
| WLD/3DT/3DG table | `.json` rebuilt through `build-binary` |
| Font | `.bdf` for glyphs and metrics; `.png` atlas fallback for glyph pixels only |
| Digitized sound cue | separate `.wav` |

If a replacement is missing or invalid, the loader should fall back to the
original game asset.

## 10. Files you can usually ignore

```text
cache/*.glmesh
default *.3D3.json for geometry-only edits; full model_data dumps only for bridge/reverse-engineering work
*.PIC.json / *.SPR.json for pixel-only edits; request them only with --include-image-json for decoder diagnostics
fonts/font_<id>.json for normal glyph edits; request it only with --include-metadata for glyph/index diagnostics
optional sounds/sounds.json for sample-only edits
```

These files are useful for validation, indexing, or loader bridges, but they are
not the normal authoring surface when the modern media file already represents
the data.

## 11. Future free custom asset packs

The long-term target is a replacement pack that can contain only freely licensed
custom assets, for example Creative Commons models, images, fonts, and sounds.
That is a packaging goal, not the current guarantee.

Current state:

- Original game assets are still required as fallback data.
- Default minimized `.3D3.json` plus per-shape GLB still need an original
  `.3D3` byte stream for shape-slot tables and software renderer fallback. A
  full `.3D3.json` bridge dump can replace those bytes, but it duplicates
  original model data and is not the desired free/minimized asset path.
- Some replacement paths still bridge through original-compatible binary layouts.
- Runtime comparison uses original assets as the reference for equivalence.

Future pack requirements:

- Every loaded asset class must have a modern replacement loader.
- Missing original assets must not be needed for supported replacement packs.
- The pack should include license metadata for every media file.
- Sidecars should stay portable and should not contain local absolute paths.
- Generated caches such as `cache/*.glmesh` should be reproducible from the
  editable source files and should not be the legal/source artifact.
