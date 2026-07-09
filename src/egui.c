// seg000 optimized code (/Ot)
#include "egcode.h"
#include "egdata.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "gfx.h"
#include "r2d.h"
#include "hdsprite.h"
#include "const.h"

#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

/* Private helpers for this translation unit. */
void drawMapMarkerBox(int centerX, int centerY, int color);
void projectMapPoint(int mapX, int mapY);
void blitGaugeSprite(int srcCol, int srcRow, int destX, int destY);
static int drawRotatedGaugeSprite(int srcCol, int srcRow, float cx, float cy, int angle16);

/* Sub-pixel projected position of the most recent projectMapPoint(), in absolute
 * 320-space (fractional). The integer vtxScratch.vproj.x.lo/y.lo (whole 320-space)
 * still drives blip-icon placement and the on-scope visibility test; the radar's
 * *lines* draw from these floats so they glide instead of snapping to whole pixels
 * as the plane moves/turns. */
static float g_scopeFx, g_scopeFy;

/* The 1.2 vertical stretch the GL backend presents the 320x200 overlay with (the
 * authentic non-square-pixel CRT look). A top-down radar must read isotropic, so
 * the projection pre-divides its vertical offset by this factor and the present's
 * x1.2 restores it to square. The software backend presents square pixels (no
 * vector replay), so the scope is already isotropic there — return 1. */
static float scopeAspectY(void) {
    return 5.0f / 6.0f;
}

static int scopeRound(float v) {
    return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

/* One radar/MFD line, drawn from the current fill colour and cut at the middle-MFD
 * box (the fillSpanRect region above, as a half-open scissor rect). */
static void scopeLine(float x1, float y1, float x2, float y2) {
    r2d_submitScopeLine(x1, y1, x2, y2, g_pageFront[2], 120, 104, 200, 176, 0.5f);
}

void drawTacticalMap(char page) {
    float startX;
    int code;
    float startY;
    int altBand;
    int altDiff;
    int gridX;
    int i;
    int gridY;
    int radius;
    int gridLo;
    int gridStep;

    /* The scope's grid/marker/projectile lines and its blip icon sprites draw in
     * submission order (icons, submitted last, land over the lines) — the crisp
     * vector scope. The black backdrop draws immediately behind them on GL (and
     * bakes into the page on software), so it never dirties the retained page. The
     * interior is a plain rectangle (all content is rect-clipped at submit), not a
     * round mask, so no scissor is needed. */
    radius = g_radarScopeRange + 1;
    setDrawColor(COLOR_BLACK);
    fillSpanRectImmediate(g_pageFront, 120, 104, 199, 175);
    setDrawColor(COLOR_DARKGRAY);
    gridStep = 1;
    if (g_radarScopeRange < 2 && g_detailLevel != 0) {
        gridStep = (1 << (2 - (unsigned char)g_radarScopeRange)) + 1;
    }
    gridLo = 1 - gridStep;
    gridX = g_viewX_ & 0xf800;
    gridY = g_viewY_ & 0xf800;
    /* ±0x2c00 spans the whole scope at long range: past the corner radius
     * (~0x2312 at shift 7) + grid alignment (0x7ff); submit clip trims to box. */
    i = gridLo * 2;
    while (gridStep * 2 >= i) {
        projectMapPoint(i * 0x400 + gridX, gridY + 0x2c00);
        startX = g_scopeFx;
        startY = g_scopeFy;
        projectMapPoint(i * 0x400 + gridX, gridY - 0x2c00);
        scopeLine(startX, startY, g_scopeFx, g_scopeFy);
        i += 2;
    }
    i = gridLo * 2;
    while (gridStep * 2 >= i) {
        projectMapPoint(gridX + 0x2c00, i * 0x400 + gridY);
        startX = g_scopeFx;
        startY = g_scopeFy;
        projectMapPoint(gridX - 0x2c00, i * 0x400 + gridY);
        scopeLine(startX, startY, g_scopeFx, g_scopeFy);
        i += 2;
    }
    for (i = 0; i < g_groundUnitCount; i++) {
        if ((g_simObjects[i].flags.b[0] & 2) && g_simObjects[i].speed != 0) {
            projectMapPoint(g_simObjects[i].posX, g_simObjects[i].posY);
            if (g_projDepth != -1) {
                if (g_currentWeaponType == 1 && i == g_airTargetLock) {
                    drawMapMarkerBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, 7);
                }
                /* The original stores the threat label as a 16-bit encoded
                 * complement; keep the subtraction unsigned so labels like
                 * 0xfffe still identify object index 1 instead of promoting
                 * to signed -2 and making the branch unreachable. */
                if (g_scopeSweepTimer > 0 && i == 0xffff - (uint16)g_threatLabelTarget) {
                    drawMapMarkerBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, g_scopeArcColor);
                }
                altDiff = g_simObjects[i].alt - g_viewZ;
                altBand = 0;
                if (altDiff < -1000) {
                    altBand = 1;
                }
                if (altDiff > 1000) {
                    altBand = 2;
                }
                /* GL: spin the base plane icon to the contact's relative heading;
                 * software falls back to the atlas's 16 hand-drawn rotation frames. */
                if (!drawRotatedGaugeSprite(0, altBand, g_scopeFx, g_scopeFy,
                                            g_simObjects[i].heading.w - g_ourHead)) {
                    code = g_simObjects[i].heading.w - g_ourHead + 0x800;
                    blitGaugeSprite((code >> 12) & 0xf, altBand, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
                }
            }
        }
    }
    for (i = 0; i < g_planeCount; i++) {
        if (!(g_planeTable.planes[i].flags & 0x80)) {
            projectMapPoint(g_planeTable.planes[i].mapX, g_planeTable.planes[i].mapY);
            if (g_projDepth != -1) {
                if (g_currentWeaponType == 2 && i == g_groundTargetLock) {
                    drawMapMarkerBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, 7);
                }
                code = 5;
                if (g_planeTable.planes[i].flags & 0x201) {
                    code = (((-g_ourHead + 0x1000) >> 13) & 3) + 8;
                }
                if (g_planeTable.planes[i].active != 0) {
                    code = 1;
                }
                if (g_planeTable.planes[i].flags & 8) {
                    code = 7;
                }
                if (i == g_targetSlots[0].planeIndex || i == g_targetSlots[1].planeIndex) {
                    code = 6;
                }
                /* Landing-field runway (cols 8-11, no status override): GL spins the
                 * base runway icon to the ownship heading; software uses the 4
                 * hand-drawn frames. Status blips (1/6/7) never rotate. */
                if (code < 8 || code > 11 ||
                    !drawRotatedGaugeSprite(8, 3, g_scopeFx, g_scopeFy, -g_ourHead)) {
                    blitGaugeSprite(code, 3, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
                }
            }
        }
    }
    projectMapPoint(g_viewX_, g_viewY_);
    if (g_projDepth != -1) {
        if (!hdsprite_drawRadarOwnship(g_scopeFx, g_scopeFy)) {
            blitGaugeSprite(0, 3, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
        }
    }
    for (i = 0; i < 4; i++) {
        if (mapEvents[i].ttl != 0) {
            projectMapPoint(mapEvents[i].mapX, mapEvents[i].mapY);
            if (g_projDepth != -1) {
                switch (mapEvents[i].type) {
                case 1:
                    blitGaugeSprite(2, 3, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
                    break;
                case 2:
                    blitGaugeSprite(3, 3, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
                    break;
                }
            }
        }
    }
    /* Missiles draw last so their heading ticks sit on top of every other blip —
     * an inbound weapon is the most important threat and must stay visible. */
    for (i = 0; i < 12; i++) {
        if (g_projectiles[i].ttl != 0) {
            projectMapPoint(g_projectiles[i].mapX, g_projectiles[i].mapY);
            if (g_projDepth != -1) {
                if (sams[g_projectiles[i].specIdx].weaponClass <= 0) {
                    setDrawColor(COLOR_LIGHTRED);
                } else {
                    setDrawColor(COLOR_YELLOW);
                }
                if (sams[g_projectiles[i].specIdx].weaponClass == 3) {
                    setDrawColor(COLOR_FLAMING);
                }
                if (!(g_projectiles[i].alt & 1)) {
                    setDrawColor(COLOR_LIGHTGRAY);
                }
                if (i >= 8) {
                    setDrawColor(COLOR_WHITE);;
                }
                /* Missile heading tick. radius (the zoom index, 1-3) is a tiny
                 * magnitude, so the original's integer sinMul/cosMul rounded the
                 * offset to a handful of whole-pixel pairs — the blip snapped to ~8
                 * compass directions with uneven lengths. Build the offset in float
                 * (sine/cosine over 32768, matching sinMul/cosMul without the Q15
                 * truncation) so the tick points in the true heading at a consistent
                 * length, like the scope's grid lines. */
                code = g_projectiles[i].worldX - g_ourHead;
                scopeLine(g_scopeFx, g_scopeFy,
                          g_scopeFx - (float)sine(code) * (radius / 32768.0f),
                          g_scopeFy + (float)cosine(code) * (radius / 32768.0f) * scopeAspectY());
            }
        }
    }
}

// ==== seg000:0xa740 ====
void drawMapMarkerBox(int centerX, int centerY, int color) {
    /* Centre on the sub-pixel projected blip (g_scopeFx/y, set by the immediately
     * preceding projectMapPoint); the ±4×±3 box reads square after the 1.2 stretch. */
    float x = g_scopeFx, y = g_scopeFy;
    (void)centerX;
    (void)centerY;
    setDrawColor(color);
    scopeLine(x - 4, y - 3, x + 4, y - 3);
    scopeLine(x + 4, y - 3, x + 4, y + 3);
    scopeLine(x + 4, y + 3, x - 4, y + 3);
    scopeLine(x - 4, y + 3, x - 4, y - 3);
}

// ==== seg000:0xa7c4 ====

void projectMapPoint(int mapX, int mapY) {
    char shift = 7 - (char)g_radarScopeRange;
    /* Carry the fraction the original's `>> shift` dropped: scale the 16-bit world
     * delta (kept as int16 so it wraps exactly as the original word math did) by 1/2^shift
     * in float, then rotate by heading with the shared sine table. cosMul/sinMul are
     * fixedMulQ14(sine, v) = sine*v/32768, so cosine()/sine() over 32768 reproduce
     * them without the whole-unit truncation that made the scope wobble. */
    float inv = 1.0f / (float)(1 << shift);
    float fsx = (float)(int16)(mapX - g_viewX_) * inv;
    float fsy = (float)(int16)(g_viewY_ - mapY) * inv;
    float c = (float)cosine(g_ourHead) * (1.0f / 32768.0f);
    float s = (float)sine(g_ourHead) * (1.0f / 32768.0f);
    float rx = c * fsx - s * fsy;
    float ry = c * fsy + s * fsx;
    g_projDepth = 0;
    g_scopeFx = 160.0f + rx;
    g_scopeFy = 152.0f - ry * scopeAspectY();
    vtxScratch.vproj.x.lo = (int16)scopeRound(g_scopeFx);
    vtxScratch.vproj.y.lo = (int16)scopeRound(g_scopeFy);
    if (vtxScratch.vproj.x.lo < 124 || vtxScratch.vproj.x.lo > 195) {
        g_projDepth = -1;
    }
    if (vtxScratch.vproj.y.lo < 107 || vtxScratch.vproj.y.lo > 172) {
        g_projDepth = -1;
    }
}

// ==== seg000:0xa872 ====
void blitGaugeSprite(int srcCol, int srcRow, int destX, int destY) {
    int srcX = srcCol * 8 + 1;
    int srcY = srcRow * 8 + 31;
    /* On the GL scope, centre the 7x7 icon on the sub-pixel projected blip
     * (g_scopeFx/y, the floats the scope's lines already use) so it glides with the
     * grid instead of stepping a whole 320-space pixel at a time. destX/destY equal
     * round(g_scopeFx/y); the software backend (no float blit) uses them below. */
    if (r2d_submitImageF(gfx_spriteBufImage((int)gfxBufPtr), srcX, srcY, 7, 7,
                         g_scopeFx - 3.0f, g_scopeFy - 3.0f, 7, 7, 0)) {
        return;
    }
    gaugeSpriteParams.bufPtr = gfxBufPtr;
    gaugeSpriteParams.srcX = srcX;
    gaugeSpriteParams.srcY = srcY;
    gaugeSpriteParams.page = 0;
    gaugeSpriteParams.dstX = destX - 3;
    gaugeSpriteParams.dstY = destY - 3;
    gaugeSpriteParams.width = 7;
    gaugeSpriteParams.height = 7;
    gfx_blitSpriteClipped((int16 *)&gaugeSpriteParams);
}

/* GL-only radar rotation: draw the base atlas frame (srcCol,srcRow) spun to angle16
 * (the game's 16-bit heading units, clockwise) about the sub-pixel blip centre —
 * the HD path spins one icon smoothly rather than snapping to a hand-drawn frame.
 * Returns 0 on the software backend so the caller keeps the pre-rotated atlas frame. */
static int drawRotatedGaugeSprite(int srcCol, int srcRow, float cx, float cy, int angle16) {
    int srcX = srcCol * 8 + 1;
    int srcY = srcRow * 8 + 31;
    float rad = (float)(int16)angle16 * (float)(6.283185307179586 / 65536.0);
    return r2d_submitImageRot(gfx_spriteBufImage((int)gfxBufPtr), srcX, srcY, 7, 7,
                              cx, cy, 7.0f, 7.0f, rad, 0);
}

// ==== seg000:0xa8c8 ====
void blitSprite(int destX, int destY, int srcX, int srcY, int spriteWidth, int spriteHeight, int transparent) {
    blitSpriteParams.bufPtr = gfxBufPtr;
    blitSpriteParams.srcX = srcX;
    blitSpriteParams.srcY = srcY;
    blitSpriteParams.page = 0;
    blitSpriteParams.dstX = destX;
    blitSpriteParams.dstY = destY;
    blitSpriteParams.width = spriteWidth;
    blitSpriteParams.height = spriteHeight;
    blitSpriteParams.pad19[0] = (char)transparent;
    if (transparent != 0) {
        blitSpriteParams.flags = 1;
        gfx_blitSpriteClipped((int16 *)&blitSpriteParams);
        return;
    }
    blitSpriteParams.flags = 0x10;
    gfx_blitSpriteOpaque((int16 *)&blitSpriteParams);
}

// ==== seg000:0xa934 ====
void cacheScopePanel(void) {
    gfx_captureToImage(g_eg2dBacking, *g_pageFront, 24, 112, 24, 112, 73, 57);
}

// ==== seg000:0xa962 ====
void restoreScopePanel(void) {
    gfx_restoreFromImage(g_eg2dBacking, *g_pageFront, 24, 112, 24, 112, 73, 57);
}

// ==== seg000:0xa9bc ====
void captureScopePanel(void) {
    gfx_restoreFromImage(g_eg2dBacking, *g_pageFront, 24, 112, 24, 112, 73, 57);
}
