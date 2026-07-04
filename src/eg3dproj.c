// seg000 debug code (/Zi)
#include "eg3dcam.h"
#include "eg3dgrid.h"
#include "eg3dmap.h"
#include "eg3dproj.h"
#include "egcode.h"
#include "egdata.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "gfx_impl.h"
#include "const.h"
#include "comm.h"
#include "r3d.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
int far transformAndCullObjectFar(int, int, int);

/* Q8 sub-LOD-unit remainder scaleCoordToLod discards (signed, around its
 * round-to-nearest bias). Without it the viewer position feeding the terrain
 * transform steps in whole LOD units — 64 fine units at LOD 4 — which made
 * large far tiles (ocean shoreline, mountains) visibly judder while the
 * fine-resolution moving objects glided. */
static int lodCoordFracQ8(int level, uint32 coord) {
    switch (level) {
    case 4:
        return (int)(((coord + 0x20) & 0x3f) - 0x20) << 2;
    case 3:
        return (int)(((coord + 8) & 0xf) - 8) << 4;
    case 2:
        return (int)(((coord + 2) & 3) - 2) << 6;
    default: /* LOD 1/0 scaling is exact */
        return 0;
    }
}

/* Scale the eye's Q8 sub-fine-unit remainder (g_camEyeFrac*, the scene camera
 * projectObjects renders from) into the LOD level's unit. */
static int lodEyeFracQ8(int level, int fracQ8) {
    switch (level) {
    case 4:
        return fracQ8 >> 6;
    case 3:
        return fracQ8 >> 4;
    case 2:
        return fracQ8 >> 2;
    case 1:
        return fracQ8;
    default:
        return fracQ8 << 1;
    }
}

/* Viewer fraction for the LOD level currently being walked (renderGridTile's
 * setViewPosition clears the frac state, so it re-applies these). */
static int16 s_lodViewFracX, s_lodViewFracY, s_lodViewFracZ;

/* Bounds test mirroring process3dg's own clamp (LOD 4 uses a +2 origin offset).
   Used by the detail-4 extended-radius walk so it never samples off-grid. */
static int gridTileInBounds(int lod, int col, int row) {
    if (lod == 4) {
        col += 2;
        row += 2;
    }
    return col >= 0 && row >= 0 && col < g_lodGridDim[lod] && row < g_lodGridDim[lod];
}

/* Render every scene object in one grid tile at (tileX+gridX, tileY+gridY).
   Mirrors the full-detail tile branch of projectObjects' main loop; used only by
   the extended-radius sampling below. Tiles whose camera-space distance would
   overflow the Q16 (int32) transform accumulators (integer part capped ±0x7fff)
   are skipped — rotation preserves length, so a length test bounds every
   component. 0x7800 leaves margin; this caps the extra reach to a distance
   sphere that auto-shrinks with altitude, so far tiles never glitch. */
static void renderGridTile(int lod, int tileX, int tileY, int gridX, int gridY, int fracX, int fracY) {
    int col = tileX + gridX;
    int row = tileY + gridY;
    int cell, subIdx;
    long mag2;
    g_objLocalX = fracX - (gridX << 12) - 0x800;
    g_objLocalY = fracY - (gridY << 12) - 0x800;
    mag2 = (long)g_objLocalX * g_objLocalX + (long)g_objLocalY * g_objLocalY +
           (long)g_objLocalZ * g_objLocalZ;
    if (mag2 >= (long)0x7800 * 0x7800) {
        return;
    }
    setViewPosition(g_objLocalX, g_objLocalY, g_objLocalZ);
    setViewPositionFrac(s_lodViewFracX, s_lodViewFracY, s_lodViewFracZ);
    cell = process3dg(lod, col, row);
    if (cell == -1) {
        return;
    }
    g_objColorBase = (g_detailLevel == 2) ? 0 : ((unsigned char)lod << 8);
    g_curTileEntry = matrix3dt_2[lod][cell];
    for (subIdx = 0; subIdx < matrix3dt[lod][cell]; subIdx++) {
        if (g_curTileEntry->shape & 0x80) {
            g_modelStreamPtr = g_world3dData + lookupTileEntry(lod, subIdx, col, row);
            if (g_modelStreamPtr == (char far *)g_world3dData) {
                g_modelStreamPtr = g_world3dData + buf3d3[g_curTileEntry->shape & 0x7f];
            }
        } else {
            g_modelStreamPtr = g_world3dData + buf3d3[g_curTileEntry->shape];
        }
        {
            R3DSubmit obj = {g_modelStreamPtr, 0, 0, 0,
                             g_curTileEntry->x, g_curTileEntry->y, g_curTileEntry->z};
            r3d_submit(&obj);
        }
        g_curTileEntry++;
        g_objColorBase++;
    }
}

void projectObjects(int heading, int rangeGate, long worldX, long worldY, long worldZ) {
    int gridX;
    int gridY;
    int dirSector;
    int fracX;
    int subIdx;
    int fracY;
    int sampleIdx;
    int tmp0;
    int tileX;
    int tileY;
    int tmp1;
    long scaled;
    int cell;

    g_proj3d.x = worldX;
    g_proj3d.y = worldY;
    g_proj3d.z = worldZ;
    worldX = g_proj3d.x;
    worldY = g_proj3d.y;
    worldZ = g_proj3d.z;
    /* DOS computed this in 16-bit, relying on wraparound to keep dirSector in
     * [0,7]; mask to 16 bits or the grid index runs
     * wild past g_dirGridOffsets[192]. */
    dirSector = (uint16)(-heading + 0x1000) >> 13;
    g_curLod = (g_detailLevel != 0) ? 4 : 3;
    goto outer_test;
    do {
        g_curLod--;
    outer_test:
        if (g_curLod < 1) {
            return;
        }
        if (g_lodObjectCount[g_curLod] == 0) {
            continue;
        }
        scaled = scaleCoordToLod(g_curLod, worldX);
        tileX = (unsigned long)scaled >> 12;
        fracX = (int)scaled & 0xfff;
        s_lodViewFracX = (int16)(lodCoordFracQ8(g_curLod, (uint32)worldX) +
                                 lodEyeFracQ8(g_curLod, g_camEyeFracX));
        scaled = scaleCoordToLod(g_curLod, worldY);
        tileY = (unsigned long)scaled >> 12;
        fracY = (int)scaled & 0xfff;
        s_lodViewFracY = (int16)(lodCoordFracQ8(g_curLod, (uint32)worldY) +
                                 lodEyeFracQ8(g_curLod, g_camEyeFracY));
        scaled = scaleCoordToLod(g_curLod, worldZ);
        if ((unsigned long)scaled < 0x7FFFUL) {
            g_objLocalZ = (int)(((unsigned long)scaled < 2UL) ? 2UL : (unsigned long)scaled);
            s_lodViewFracZ = ((unsigned long)scaled < 2UL)
                                 ? 0
                                 : (int16)(lodCoordFracQ8(g_curLod, (uint32)worldZ) +
                                           lodEyeFracQ8(g_curLod, g_camEyeFracZ));
            for (sampleIdx = 0;; sampleIdx++) {
                if (g_detailLevel >= 4) break; /* detail 4: the dense walk below replaces this sparse pattern */
                if (g_curLod == 4 && g_detailLevel >= 2) {
                    if (sampleIdx == 15) {
                        break;
                    }
                    gridX = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (unsigned)18 * (unsigned)dirSector);
                    gridY = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (unsigned)18 * (unsigned)((dirSector + 2) & 7));
                    g_objLocalX = fracX - (gridX << 12) - 0x800;
                    g_objLocalY = fracY - (gridY << 12) - 0x800;
                    g_objRenderMode = 7;
                    if (transformAndCullObjectFar(-g_objLocalX, -g_objLocalY, -g_objLocalZ) != 0) {
                        goto next_iter;
                    }
                } else {
                    if (sampleIdx == 9) {
                        break;
                    }
                    if (g_curLod != 4 && g_detailLevel < 2 && sampleIdx < 4) {
                        goto next_iter;
                    }
                    if (rangeGate < (int)0xd555) {
                        gridX = g_neighborSampling.gridX[sampleIdx];
                        gridY = g_neighborSampling.gridY[sampleIdx];
                    } else {
                        gridX = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (unsigned)18 * (unsigned)dirSector);
                        gridY = *(const int16 *)((const char *)g_dirGridOffsets + sampleIdx * 2 + (unsigned)18 * (unsigned)((dirSector + 2) & 7));
                    }
                    g_objLocalX = fracX - (gridX << 12) - 0x800;
                    g_objLocalY = fracY - (gridY << 12) - 0x800;
                }
                setViewPosition(g_objLocalX, g_objLocalY, g_objLocalZ);
                setViewPositionFrac(s_lodViewFracX, s_lodViewFracY, s_lodViewFracZ);
                cell = process3dg(g_curLod, tileX + gridX, tileY + gridY);
                if (cell == -1) {
                    goto next_iter;
                }
                if (sampleIdx >= 4 || g_detailLevel >= 2) {
                    g_objColorBase = (g_detailLevel == 2) ? 0 : ((unsigned char)g_curLod << 8);
                    g_curTileEntry = matrix3dt_2[g_curLod][cell];
                    for (subIdx = 0; subIdx < matrix3dt[g_curLod][cell]; subIdx++) {
                        if (g_curTileEntry->shape & 0x80) {
                            g_modelStreamPtr = g_world3dData + lookupTileEntry(g_curLod, subIdx, tileX + gridX, tileY + gridY);
                            if (g_modelStreamPtr == (char far *)g_world3dData) {
                                g_modelStreamPtr = g_world3dData + buf3d3[g_curTileEntry->shape & 0x7f];
                            }
                        } else {
                            g_modelStreamPtr = g_world3dData + buf3d3[g_curTileEntry->shape];
                        }
                        {
                            R3DSubmit obj = {g_modelStreamPtr, 0, 0, 0,
                                             g_curTileEntry->x, g_curTileEntry->y, g_curTileEntry->z};
                            r3d_submit(&obj);
                        }
                        g_curTileEntry++;
                        g_objColorBase++;
                    }
                } else {
                    if (g_curLod == 4) {
                        g_curTileEntry = matrix3dt_2[g_curLod][cell];
                        g_modelStreamPtr = g_world3dData + buf3d3[g_curTileEntry->shape];
                        g_objColorBase = 0x400;
                        {
                            R3DSubmit obj = {g_modelStreamPtr, 0, 0, 0,
                                             g_curTileEntry->x, g_curTileEntry->y, g_curTileEntry->z};
                            r3d_submit(&obj);
                        }
                    }
                }
            next_iter:;
            }
            /* Detail level 4: replace the sparse directional ±2 walk above with a
               dense omnidirectional square out to a fixed radius. This both
               extends draw distance and fills the directional gaps the sparse
               pattern leaves around the original range (which otherwise show as
               objects vanishing then reappearing through their LODs as you
               approach). Off-grid and Q16-unsafe tiles are skipped in
               renderGridTile; far tiles still cull inside projectSceneObject. */
            if (g_detailLevel >= 4) {
                const int radius = 7;
                int gx, gy;
                for (gy = -radius; gy <= radius; gy++) {
                    for (gx = -radius; gx <= radius; gx++) {
                        if (gridTileInBounds(g_curLod, tileX + gx, tileY + gy)) {
                            renderGridTile(g_curLod, tileX, tileY, gx, gy, fracX, fracY);
                        }
                    }
                }
            }
        }
    } while (1);
}
// Once implemented, try merging egame2.c + egame1e.c (if register spill doesn't affect codegen)

// ==== seg000:0x26b4 ====
uint32 scaleCoordToLod(int level, uint32 coord) {
    switch (level) {
    case 4:
        return (coord + 0x20) >> 6;
    case 3:
        return (coord + 8) >> 4;
    case 2:
        return (coord + 2) >> 2;
    case 1:
        return coord;
    default: // case 0
        return coord << 1;
    }
}
