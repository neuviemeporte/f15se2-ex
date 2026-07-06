# HD assets

High-resolution replacement art for the sprites/graphics the game draws. These are
**optional** and **GPU-only**: on a GPU backend (OpenGL) a present asset overrides the
original sprite; where an asset is missing — or on the software renderer — the game
uses the original low-res art. So the set can be filled in **incrementally**: draw one,
drop it in, it appears next run. Nothing here is required to run the game.

The engine mirrors this folder next to the binary at build time, so files land in
`build/assets/…` and are found relative to the working dir.

## Folder layout & naming

One PNG per logical graphic. The **asset id is its path under `assets/` without the
`.png`** (e.g. `assets/flight/radar/ownship.png` → id `flight/radar/ownship`). Names
are kebab-case and describe *what the thing is*, never a sheet index.

```
assets/
  flight/            # in-flight (EGAME)
    cockpit/         # WIDESCREEN cockpit overlays — special format, see below
                     #   forward.png  left.png  right.png  rear.png   (4 view angles)
    panel/           # stock-center cockpit pieces: MFD frames, buttons, gauges
    radar/           # middle-MFD radar icons: ownship.png, contact-*.png, …
    tac/             # left-MFD TAC-map icons
    hud/             # HUD raster symbology (non-vector)
    weapon/          # weapon/stores icons, MFD weapon labels
    fx/              # explosions, hit flashes, muzzle
  start/             # menus/briefing (START)
    title/           # title screens, logos
    menu/            # menu backgrounds, buttons, cursors
    briefing/        # briefing map, icons
  end/               # debrief (END)
    map/             # debrief map background
    marker/          # event / POI markers
    award/           # medals, rank insignia
```

(The exhaustive per-sprite id list — the actual filenames to fill — is in
`docs/sprite-inventory.md`.)

## Authoring rules

- **Format:** RGBA PNG, straight (un-premultiplied) alpha. Transparent where the old
  sprite was transparent.
- **Square pixels.** Author as if pixels are square. **Do NOT pre-stretch** for the
  game's 4:3 / non-square-pixel look — the engine applies the aspect to *placement*,
  not to your art's own grid (see `docs/2d-rework.md`).
- **Resolution:** author larger than the on-screen size; the engine downscales with
  smooth filtering. A good baseline is ~**8×** the original sprite's pixel size
  (e.g. a 7×7 icon → 64×64; a 16×16 → 128×128). Bigger is fine.
- **Rotatable sprites** (radar/TAC contact icons whose orientation shows heading):
  draw **once** at the canonical orientation (**nose up**), centred on a square canvas
  with enough transparent margin that a full 360° rotation never clips — keep the art
  inside the inscribed circle. The engine rotates it (smooth, any angle) instead of the
  original's handful of pre-drawn frames. **Anchor = centre.**
- **Static sprites:** keep a 1px transparent border so edge filtering doesn't bleed.

## Widescreen cockpit overlays (special)

`flight/cockpit/{forward,left,right,rear}.png` — one per cockpit view direction (4).
Unlike normal sprites these are drawn **oversized and centred to fill the whole
window**, framing the widescreen 3D.

- **Canvas:** square pixels, **height 240** (= the original 200 rows × the 1.2 aspect),
  aspect **21:9** → **560×240** (or an integer multiple: 1120×480, 2240×960 — bigger is
  better, it's downscaled).
- **Central cutout:** leave the central **320×240** region **fully transparent**. That
  is exactly where the game draws the stock cockpit/HUD/MFDs (the "original graphics").
  Your art fills the **120px columns on each side** — canopy rails, side consoles, etc.
- The engine scales the art so that transparent central 320×240 maps onto the stock
  UI's on-screen box, then extends your side art outward to cover the window.
- In the side art, paint the cockpit frame **opaque** and leave side-window areas
  **transparent** so the peripheral 3D world shows through them (widescreen 3D renders
  behind the whole window). Alpha is what decides frame-vs-window.
- Applies to OpenGL now; the software renderer will use these once it does widescreen.
