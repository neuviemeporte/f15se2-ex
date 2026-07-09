#ifndef HDSPRITE_H
#define HDSPRITE_H

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

#endif /* HDSPRITE_H */
