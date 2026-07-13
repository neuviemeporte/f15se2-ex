// seg000 optimized code (/Ot)
#include "eg3dcam.h"
#include "eg3dgrid.h"
#include "eg3dmap.h"
#include "eg3dproj.h"
#include "eg3dvp.h"
#include "egcode.h"
#include "egdata.h"
#include "egsphere.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "gfx_impl.h"
#include "const.h"

#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern vtxSignMask_t g_vtxSignMask;
/* Private helpers for this translation unit. */
void drawMapTiles(int16 originX, int16 originY, int16 zoomShift);
void computeTileBounds(int16 *, int16 *, int16 *, int16 *);
void worldToTileIndex(int16, int16, int16 *, int16 *);
void drawMapTileObject(char FAR *, int16, int16);
void drawModelPoint(int16 x, int16 y);
void buildVertexSignMask(int16 screenX, int16 screenY);
void projectModelVertices(int16 screenX, int16 screenY);
int16 aspectScaleY(int16 screenY);

// ==== seg000:0x2fda ====

struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY) {
    // These locals keep single-letter names on purpose: MSC 5.1 hashes each
    // name to a fixed stack-frame slot, and this frame (18 scalars, with two
    // hash collisions and an int16-aliased-as-long scratch) only byte-matches the
    // original with this exact layout. Renaming to descriptive names shifts the
    // slots and breaks the match. See drawNearestTileObject() below for the same
    // algorithm with readable names. Legend:
    //   c = lod (detail level 1..2)      e = neighbour sample index 0..8
    //   m = scaleCoordToLod() scratch (a 32-bit int now holds the whole value)
    //   i = tileX     k = tileY          r = fracX     d = fracY  (query point)
    //   a = neighbour col delta          b = neighbour row delta
    //   o = baseDx    p = baseDy         (query point -> neighbour cell offset)
    //   n = tile block index (process3dg)    f = entry index within the block
    //   g = shape id/bits
    //   h = objDx     j = objDy          (query point -> candidate object)
    //   q = distance metric (best-so-far compare)    l = unused frame padding
    int16 p, q, a, r, b, c, d, e, f, g, h, i, j, k, l, n, o;
    int32 m; /* the "int-aliased-as-long" scratch: must hold the full 20-bit fine coord */

    nearestTile.dist = 0x7fff;
    for (c = 1; c <= 2; c++) {
        for (e = 0; e < 9; e++) {
            m = (int)scaleCoordToLod(c, worldX);
            i = (unsigned)m >> 0xc;
            r = m & 0xfff;
            m = (int)scaleCoordToLod(c, worldY);
            k = (unsigned)m >> 0xc;
            d = m & 0xfff;
            a = g_neighborSampling.gridX[e];
            b = g_neighborSampling.gridY[e];
            /* lut[delta] for delta in {-1,0,+1} is exactly delta*0x1000 (the
             * original relied on lut[-1] aliasing gridY[10] in the struct — an
             * out-of-bounds index). Multiply, don't shift: a/b may be negative. */
            o = a * 0x1000 - r + 0x800;
            p = b * 0x1000 - d + 0x800;
            n = process3dg(c, i += a, k += b);
            if (n != -1) {
                g_curTileEntry = matrix3dt_2[c][n];
                for (f = 0; matrix3dt[c][n] > f; f++) {
                    if (g_shapeTargetCategory[g_curTileEntry->shape & 0x7f] != 0) {
                        h = o + g_curTileEntry->x;
                        j = g_curTileEntry->y + p;
                        q = abs(h) + abs(j);
                        if (c == 1) {
                            q >>= 2;
                        } else {
                            h <<= 2;
                            j <<= 2;
                        }
                        g = g_curTileEntry->shape;
                        if ((g_curTileEntry->shape & 0x80) != 0 &&
                            lookupTileEntry(c, f, i, k) != 0) {
                            g = g_dynTileEntries[g_tileEntryIdx].shape;
                        }
                        /* bit 7 = "needs tile lookup" flag, not part of the index
                         * (cf. eg3dproj.c, struct.h TileObject). When 0x80 is set
                         * but no dynamic entry resolves (e.g. it was stored at a
                         * different lod), the original left the flag in g and read
                         * buf3d3[]/nearestTile.id out of bounds. Mask to the base
                         * shape so both the model lookup and the id stay in range. */
                        g &= 0x7f;
                        if (q < nearestTile.dist) {
                            g_modelStreamPtr = (char far *)(g_world3dData + buf3d3[g]);
                            if (rdI16(g_modelStreamPtr) != 0 ||
                                *((char far *)g_modelStreamPtr + 2) != 0 ||
                                g_render3DTiles != 0) {
                                nearestTile.lod = (uint8)c;
                                nearestTile.subIndex = (uint8)f;
                                nearestTile.tileX = (uint8)i;
                                nearestTile.tileY = (uint8)k;
                                nearestTile.entry = g_curTileEntry;
                                nearestTile.id = g;
                                nearestTile.dist = q;
                                nearestTile.x = worldX + (int32)h;
                                nearestTile.y = worldY + (int32)j;
                            }
                        }
                    }
                    g_curTileEntry++;
                }
            }
        }
    }
    if (nearestTile.dist != 0x7fff) {
        return &nearestTile;
    }
    return 0;
}

void addTileEntry(struct TileObject *rec, int16 value, char tag) {
    rec->shapeOff = value;
    rec->flag = tag;
    memcpy(&g_dynTileEntries[g_tileEntryCount++], &rec->lod, 8);
    rec->entry->shape |= 0x80;
}

// ==== seg000:0x3266 ====
int16 lookupTileEntry(int16 lod, int16 subIndex, int16 tileX, int16 tileY) {
    for (g_tileEntryIdx = g_tileEntryCount - 1; g_tileEntryIdx >= 0; g_tileEntryIdx--) {
        if (g_dynTileEntries[g_tileEntryIdx].lod == lod &&
            g_dynTileEntries[g_tileEntryIdx].subIndex == subIndex &&
            g_dynTileEntries[g_tileEntryIdx].tileX == tileX &&
            g_dynTileEntries[g_tileEntryIdx].tileY == tileY) {
            return g_dynTileEntries[g_tileEntryIdx].value;
        }
    }
    return 0;
}

void drawNearestTileObject(uint32 coord1, uint32 coord2, uint32 coord3) {
    int16 yOff, fracX, lod, fracY, subIdx, relX, relY, tileX, tileY, cell, xOff;
    uint32 scaled;

    *(char *)&g_posVisibleFlag = 0;
    nearestTile.dist = 0x7fff;
    lod = 4;
    scaled = scaleCoordToLod(lod, coord1);
    tileX = (int16)(scaled >> 12);
    fracX = (int16)scaled & 0xfff;
    scaled = scaleCoordToLod(lod, coord2);
    tileY = (int16)(scaled >> 12);
    fracY = (int16)scaled & 0xfff;
    g_viewPosZ = (int16)scaleCoordToLod(lod, coord3);
    xOff = 0x800 - fracX;
    yOff = 0x800 - fracY;
    g_viewPosX = fracX - 0x800;
    g_viewPosY = fracY - 0x800;
    cell = process3dg(lod, tileX, tileY);
    if (cell != -1) {
        g_curTileEntry = matrix3dt_2[lod][cell];
        for (subIdx = 1; subIdx < matrix3dt[lod][cell]; subIdx++) {
            relX = g_curTileEntry->x + xOff;
            relY = g_curTileEntry->y + yOff;
            g_objDistance = abs(relX) + abs(relY);
            if (nearestTile.dist > g_objDistance) {
                nearestTile.entry = g_curTileEntry;
                nearestTile.dist = g_objDistance;
            }
            g_curTileEntry++;
        }
    }
    if (nearestTile.dist != 0x7fff) {
        g_curTileEntry = nearestTile.entry;
        /* mask the 0x80 "needs tile lookup" flag off the buf3d3 index (see findNearestTileObject). */
        g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[nearestTile.entry->shape & 0x7f]);
        g_objRelX = g_curTileEntry->x - g_viewPosX;
        g_objRelY = g_curTileEntry->y - g_viewPosY;
        g_objTransform[0] = g_curTileEntry->z - g_viewPosZ;
        g_modelStreamPtr++;
        *(uint8 *)&g_objRenderMode = 0;
        g_objDistance = 0;
        advanceModelPointerLod();
        if (*g_modelStreamPtr & 0x40) {
            g_objHasRotation = 0;
            rotatePoint3dFar();
        }
    }
}

// ==== seg000:0x345e ====
void renderMapTerrain(const int16 *transform, int16 mapX, int16 mapY, int16 zoomShift) {
    int16 tmp0, tmp1;
    g_objShade = 0;
    setup3DTransform(transform, 0, 0, 0, 0, 0, 0, 0);
    /* clip-left / clip-top are 16-bit descriptor words (see setupViewport). */
    gfx_setBlitOffset(gfx_calcRowAddr(transform[9], transform[7]));
    drawMapTiles(mapX, mapY, zoomShift);
    rasterize3DWorld();
}

// ==== seg000:0x51f9 ====

void drawMapTiles(int16 originX, int16 originY, int16 zoomShift) {
    int16 maxTileY, screenY, minTileX, minTileY, subIdx, col, row, cell, maxTileX, screenX;

    g_mapOriginX = originX >> (char)zoomShift;
    g_mapOriginY = originY >> (char)zoomShift;
    for (g_mapLodIndex = 4; g_mapLodIndex >= 0; g_mapLodIndex--) {
        g_curLod = g_mapTileLodTable[g_mapLodIndex];
        g_modelEvenOddBit = (g_mapLodIndex <= 1) ? 0x40 : 0;
        g_tileZoomShift = zoomShift - g_curLod * 2 + 8;
        g_tileWorldSize = 0x1000 >> (char)g_tileZoomShift;
        if (g_tileWorldSize > 16) {
            g_tileGridDim = 4 << (8 - (char)g_curLod * 2);
            computeTileBounds(&minTileX, &maxTileX, &minTileY, &maxTileY);
            for (row = minTileY; row <= maxTileY; row++) {
                for (col = minTileX; col <= maxTileX; col++) {
                    screenX = col * g_tileWorldSize - g_mapOriginX + (g_tileWorldSize >> 1);
                    screenY = row * g_tileWorldSize - g_mapOriginY + (g_tileWorldSize >> 1);
                    cell = process3dg(g_curLod, col, row);
                    if (cell != -1) {
                        g_curTileEntry = matrix3dt_2[g_curLod][cell];
                        for (subIdx = 0; matrix3dt[g_curLod][cell] > subIdx; subIdx++) {
                            if (g_curTileEntry->z == 0) {
                                g_modelStreamPtr = (char FAR *)(g_world3dData + buf3d3[g_curTileEntry->shape]);
                                drawMapTileObject(g_modelStreamPtr,
                                                  (g_curTileEntry->x >> (char)g_tileZoomShift) + screenX,
                                                  (g_curTileEntry->y >> (char)g_tileZoomShift) + screenY);
                            }
                            g_curTileEntry++;
                        }
                    }
                }
            }
        }
    }
}

// ==== seg000:0x3638 ====
void computeTileBounds(int16 *minTileX, int16 *maxTileX, int16 *minTileY, int16 *maxTileY) {
    worldToTileIndex(0, 0, minTileX, minTileY);
    if (*minTileX < 0) {
        *minTileX = 0;
    }
    if (*minTileY < 0) {
        *minTileY = 0;
    }
    worldToTileIndex(g_clipMaxX, g_clipMaxY, maxTileX, maxTileY);
    if (*maxTileX >= g_tileGridDim) {
        *maxTileX = g_tileGridDim - 1;
    }
    if (*maxTileY >= g_tileGridDim) {
        *maxTileY = g_tileGridDim - 1;
    }
}

// ==== seg000:0x3694 ====
void worldToTileIndex(int16 worldX, int16 worldY, int16 *outCol, int16 *outRow) {
    *outCol = (worldX - g_viewCenterX + g_mapOriginX) / g_tileWorldSize;
    *outRow = ((worldY - g_viewCenterY) * 4 / 3 + g_mapOriginY) / g_tileWorldSize;
}

// ==== seg000:0x36d2 ====
void drawMapTileObject(char FAR *modelData, int16 screenX, int16 screenY) {
    *(char FAR **)&g_modelStreamPtr = modelData;
    g_modelStreamPtr++;
    g_objDistance = 0;
    advanceModelPointerLod();
    if (g_curLod >= 3) {
        if ((**(char FAR **)&g_modelStreamPtr & 0x40) != g_modelEvenOddBit)
            return;
    }
    switch ((uint16)(uint8) * *(char FAR **)&g_modelStreamPtr & 0x3f) {
    case 0x3e:
        return;
    case 0x3f:
        drawModelPoint(screenX, screenY);
        return;
    }
    buildVertexSignMask(screenX, screenY);
    projectModelVertices(screenX, screenY);
    projectModelEdgesFar();
    drawModelDisplayList();
}

// ==== seg000:0x374a ====
void drawModelPoint(int16 x, int16 y) {
    g_lineX2 = g_lineX1 = x + g_viewCenterX;
    g_lineY2 = g_lineY1 = -y + g_viewCenterY;
    ++g_modelStreamPtr;
    gfx_setColor((uint8)*g_modelStreamPtr++);
    drawClipLineGlobal();
}

// ==== seg000:0x378e ====
void buildVertexSignMask(int16 screenX, int16 screenY) {
    int32 bit;
    int16 edgeIdx;

    bit = 1L;
    g_modelEdgeCount = (int16)(uint8)(*((*(char far **)&g_modelStreamPtr)++)) & 0x1f;
    g_vtxSignMask.Lo = -1;
    g_vtxSignMask.Hi = -1;
    *(char *)&g_modelWideVtxFlag = (g_modelEdgeCount > 16) ? 1 : 0;
    edgeIdx = 0;
    while (edgeIdx < g_modelEdgeCount) {
        g_modelStreamPtr += 4;
        if (rdI16(g_modelStreamPtr) < 0) {
            /* Lo:Hi are an adjacent int16 pair forming one 32-bit sign mask;
             * access as int32 — native `long` would over-read 4 bytes past Hi. */
            g_vtxSignMask.Value ^= bit;
        }
        g_modelStreamPtr += 4; /* int16 mask word read above (+2) + skip (+2) */
        bit <<= 1;
        edgeIdx++;
    }
}

// ==== seg000:0x3816 ====
void projectModelVertices(int16 screenX, int16 screenY) {
    int16 vtxIdx, vtxRef, packed, screenVtxX, screenVtxY;

    packed = (int16)(uint8) * *(char FAR **)&g_modelStreamPtr & 0x80;
    g_modelVtxCount = (int16)(uint8)(*(*(char FAR **)&g_modelStreamPtr)++) & 0x7F;
    for (vtxIdx = 0; vtxIdx < g_modelVtxCount; vtxIdx++) {
        g_modelStreamPtr += (uint8)g_modelWideVtxFlag * 2 + 2;
        if (packed != 0) {
            vtxRef = (int16)(uint8)(*(*(char FAR **)&g_modelStreamPtr)++);
            screenVtxX = (g_replayLog.vertexX[buf3d3_1[vtxRef]] >> g_tileZoomShift) + screenX;
            screenVtxY = (((int16 *)g_modelVertY)[buf3d3_2[vtxRef]] >> g_tileZoomShift) + screenY;
        } else {
            screenVtxX = (rdI16(g_modelStreamPtr) >> g_tileZoomShift) + screenX;
            g_modelStreamPtr += 2;
            screenVtxY = (rdI16(g_modelStreamPtr) >> g_tileZoomShift) + screenY;
            g_modelStreamPtr += 4; /* consumed Y word (+2) + original skip (+2) */
        }
        vtxScratch.vproj.in[vtxIdx].num = 1;
        vtxScratch.vproj.in[vtxIdx].div = 1;
        vtxScratch.vproj.x.v[vtxIdx] = screenVtxX + g_viewCenterX;
        vtxScratch.vproj.y.v[vtxIdx] = -aspectScaleY(screenVtxY) + g_viewCenterY;
    }
}

// ==== seg000:0x3922 ====
int16 aspectScaleY(int16 screenY) {
    return screenY - (screenY >> 2);
}

// ==== seg000:0x3932 ====
void setup3DTransform(const int16 *model, int16 angleX, int16 angleY, int16 angleZ, int16 posX, int16 posY, int16 posZ, int16 renderScene) {
    setupViewport(model);
    setViewRotation(angleX, angleY, angleZ);
    setViewPosition(posX, posY, posZ);
    if (renderScene != 0) {
        g_posVisibleFlag = 0;
        if (g_detailLevel == 0) {
            g_offscreenRender = 1;
        }
        if (g_offscreenRender == 0) {
            transformModelVerticesFar();
        }
        drawProjectionSphere(model[2]);
    }
    g_sortedObjCount = 0;
    /* Scaled by sim steps this frame so the spin advances at the sim rate, not
     * the (now higher) render rate; 0 on pure interpolation frames. */
    g_spinAngle -= g_simStepsThisFrame * (0x3000 / g_frameRateScaling);
}

// ==== seg000:0x39aa ====
void rasterize3DWorld(void) {
    renderSortedListFar();
    gfx_setBlitOffset2();
    gfx_nop23();
    g_offscreenRender = 0;
}

// ==== seg000:0x3a6c ====
