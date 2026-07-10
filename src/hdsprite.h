#ifndef HDSPRITE_H
#define HDSPRITE_H

typedef struct R2DImage R2DImage;

/*
 * Optional high-resolution sprite art (PNGs under assets/), drawn at native window
 * resolution over the low-res overlay. GPU backends only (r2d_hasNativeOverlay);
 * the software path always uses the original paletted sprites. Assets are authored
 * in square (1:1) pixels and drawn into the 320-space footprint of the legacy sprite
 * they replace, so the non-square aspect correction applies to placement but not to
 * the art's own grid — see docs/2d-rework.md.
 *
 * Each draw helper returns 1 if it submitted the HD sprite (the caller then skips
 * the legacy draw) or 0 to fall back to the original sprite.
 */

/* Radar (middle-MFD) ownship symbol: draw the HD F-15 into the legacy 7x7 footprint
 * centred at (destX,destY), fixed orientation (the radar is plane-relative, so the
 * ownship always points up). Returns 0 when no HD asset is available or this is not
 * a native-overlay vector frame. */
int hdsprite_drawRadarOwnship(float destX, float destY);

/* Debrief theatre map: draw the HD PNG (if available) in the legacy map footprint
 * (position 8,10; size 224×168 in 320-space). Returns 1 if drawn, 0 to fall back
 * to the legacy SPR. Lazily loaded once per theatre. */
int hdsprite_drawDebriefTheatreMap(int theatre);

/*
 * Pre-mission briefing (START mission-select) widescreen HD art. Unlike the sprites
 * above — which replace a sprite inside the 320-space overlay box — the briefing
 * room fills the whole window (the officer stands in the wide margins, outside the
 * 4:3 game box), so these draw with a window-fill transform, not a 320-space
 * footprint. Wall and arm cels share the same HEIGHT (drawn at the same window-height
 * scale so the forearm meets the officer's body); the wall is centred, while each arm
 * cel is left-aligned within the 4:3 menu box it points into (its extra width tucks
 * behind the officer) — see BRIEFING_ARM_BOX_LEFT_X in hdsprite.c.
 *
 * `assets/start/menu/wall.png`       — room + officer body + projector; the menu text
 *                                      is drawn as a native-res overlay ON TOP of the
 *                                      projector-screen area (no transparent cut-out
 *                                      needed — the room is an opaque backdrop).
 * `assets/start/menu/arm/<0..6>.png` — the pointer-arm poses, keyed by arm position
 *                                      (0-4 point at menu rows 1-5, 5-6 parked for the
 *                                      raise-in/lower-out sweep); a missing pose draws
 *                                      no arm for that frame.
 *
 * All are GPU-only and optional; a missing file falls back to the legacy
 * Wall.Pic / ArmPiece.Pic. Must be called inside an active native-overlay frame.
 */
int hdsprite_hasBriefingWall(void); /* true if the HD wall loaded (gates the HD path) */
void hdsprite_drawBriefingWall(void);
void hdsprite_drawBriefingArm(int frame); /* frame = arm position 0..6 */

#endif /* HDSPRITE_H */
