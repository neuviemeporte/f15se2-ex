/* egtgt2.c — world->HUD projection + range/bearing helpers (reads g_viewZ
   as int16). Split from egtarget.c at the projectWorldToHud boundary. */
#include "eg3dmap.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egcombat.h"
#include "egdata.h"
#include "egflight.h"
#include "egframe.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egtarget.h"
#include "egthreat.h"
#include "egtypes.h"
#include "egui.h"
#include "offsets.h"
#include "log.h"
#include "const.h"

#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

/* Private helpers for this translation unit. */
void drawTargetBox(int, int, int, int);
void drawMissileLock(void);
void __cdecl drawTargetLabel(const char *, int, int);
void buildRangeString(int rangeRaw);
void projectWorldToHud(int worldX, int worldY, int worldZ);
long rotateVectorComponent(int axis, int vecX, int vecY, int vecZ);
int computeMapTargetRange(int targetIdx);
int computeSimObjectRange(int objIdx);
int computeTargetBearing(int targetX, int targetY, int wantBearing);

void projectWorldToHud(int worldX, int worldY, int worldZ);
long rotateVectorComponent(int axis, int vecX, int vecY, int vecZ);
int computeMapTargetRange(int targetIdx);
int computeSimObjectRange(int objIdx);
int computeTargetBearing(int targetX, int targetY, int wantBearing);

// ==== seg000:0xc488 ====
void projectWorldToHud(int worldX, int worldY, int worldZ) {
    int relX;
    long camX;
    int relY;
    long camY;
    int relZ;
    long camDepth;

    relX = g_viewX_ - worldX;
    relY = worldY - g_viewY_;
    relZ = (worldZ - g_viewZ) >> 5;

    if (keyValue & 0x80) {
        relX -= (int)((g_ViewX - g_camEyeX) >> 5);
        relY -= (int)((g_ViewY - g_camEyeY) >> 5);
        relZ -= (int)((-((long)(unsigned)g_viewZ - (long)g_camEyeZ)) >> 5);
    }

    camX = rotateVectorComponent(0, relX, relY, relZ);
    camY = rotateVectorComponent(1, relX, relY, relZ);
    camDepth = rotateVectorComponent(2, relX, relY, relZ);

    if (camDepth >= 0) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    if (g_halfScaleRender) {
        camX >>= 1;
        camY >>= 1;
    }

    if (-camDepth < camX || camX < camDepth) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    vtxScratch.vproj.x.lo = (int)((camX << 8) / camDepth) + 160;
    vtxScratch.vproj.y.lo = (int)((camY << 8) / camDepth);
    vtxScratch.vproj.y.lo -= vtxScratch.vproj.y.lo >> 1 >> 1;
    vtxScratch.vproj.y.lo += (g_pageFront[8] == 199) ? 100 : 56;

    g_projDepth = (int)(camDepth >> 3);

    if (vtxScratch.vproj.x.lo < 0 || vtxScratch.vproj.x.lo > 319) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
    if (vtxScratch.vproj.y.lo < 0 || g_pageFront[8] < vtxScratch.vproj.y.lo) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
}

/* Fine-precision projectWorldToHud. Takes the object's *fine* world position
 * (mapX<<5 scale on X/Y, altitude on Z — the same integrated coords the 3D model
 * is drawn from) rather than the coarse posX/posY (÷32) the reticle used to read.
 * The coarse path rounds the viewer and the object to the ÷32 grid independently,
 * so under the render/sim decouple the two roundings beat ±1 against each other
 * between sim ticks and the target box jumped around — worst up close, where one
 * grid cell spans many screen pixels. Differencing the interpolated fine coords
 * once and rotating at the finest power-of-two scale whose components still fit the
 * int16 rotate inputs keeps sub-grid precision through the perspective divide (the
 * projected ratio is scale-invariant), so the box glides with the model. */
void projectWorldToHudFine(int32 fineX, int32 fineY, int fineZ) {
    long relX, relY, relZ, am, camX, camY, camDepth;
    int rx, ry, rz, sh;

    relX = g_ViewX - fineX;
    relY = fineY + g_ViewY - 0x100000L; /* (relY >> 5) == object posY - g_viewY_ */
    relZ = (long)fineZ - (long)g_viewZ;

    if (keyValue & 0x80) {
        relX -= g_ViewX - g_camEyeX;
        relY -= g_ViewY - g_camEyeY;
        relZ -= -((long)(unsigned)g_viewZ - (long)g_camEyeZ);
    }

    am = labs(relX) | labs(relY) | labs(relZ);
    sh = 0;
    while ((am >> sh) > 0x3fff)
        sh++;
    rx = (int)(relX >> sh);
    ry = (int)(relY >> sh);
    rz = (int)(relZ >> sh);

    camX = rotateVectorComponent(0, rx, ry, rz);
    camY = rotateVectorComponent(1, rx, ry, rz);
    camDepth = rotateVectorComponent(2, rx, ry, rz);

    if (camDepth >= 0) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    if (g_halfScaleRender) {
        camX >>= 1;
        camY >>= 1;
    }

    if (-camDepth < camX || camX < camDepth) {
        vtxScratch.vproj.x.lo = -1;
        return;
    }

    vtxScratch.vproj.x.lo = (int)((camX << 8) / camDepth) + 160;
    vtxScratch.vproj.y.lo = (int)((camY << 8) / camDepth);
    vtxScratch.vproj.y.lo -= vtxScratch.vproj.y.lo >> 1 >> 1;
    vtxScratch.vproj.y.lo += (g_pageFront[8] == 199) ? 100 : 56;

    /* Same perspective divide in float: the box draws from these on the GL overlay
     * so its centre isn't quantized to the 320x200 grid (worst up close). The GL 3D
     * pass projects with a 5/6 vertical grid-aspect (buildProjection) — 10/9 taller
     * than the software raster's 3/4 — so the box's fractional Y uses 5/6 too, or it
     * would drift vertically from the 3D target proportional to screen Y. The integer
     * vproj.y.lo keeps 3/4 for the software backend and the reticle range tests. */
    g_hudProjXf = (float)(camX << 8) / (float)camDepth + 160.0f;
    g_hudProjYf = (float)(camY << 8) / (float)camDepth * (5.0f / 6.0f) + ((g_pageFront[8] == 199) ? 100.0f : 56.0f);

    /* camDepth is in 2^sh-fine units; the coarse depth is camDepth * 2^(sh-5) and
     * g_projDepth is that >> 3, i.e. camDepth >> (8 - sh). */
    g_projDepth = (sh <= 8) ? (int)(camDepth >> (8 - sh)) : (int)(camDepth << (sh - 8));

    if (vtxScratch.vproj.x.lo < 0 || vtxScratch.vproj.x.lo > 319) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
    if (vtxScratch.vproj.y.lo < 0 || g_pageFront[8] < vtxScratch.vproj.y.lo) {
        g_offscreenProjX = vtxScratch.vproj.x.lo;
        vtxScratch.vproj.x.lo = -1;
    }
}

// ==== seg000:0xc661 ====
long rotateVectorComponent(int axis, int vecX, int vecY, int vecZ) {
    long sum;

    sum = (long)fixedMulQ14(g_camRotMatrix[axis], vecX);
    sum += (long)fixedMulQ14(g_camRotMatrix[3 + axis], vecZ);
    sum += (long)fixedMulQ14(g_camRotMatrix[6 + axis], vecY);
    return sum;
}

int findWaypointEntry(int mapX, int mapY) {
    int idx;

    if ((g_nearestTileObj = findNearestTileObject((int32)mapX << 5, (0x8000L - (int32)mapY) << 5))) {
        mapX = g_nearestTileObj->x >> 5;
        mapY = -((int)(g_nearestTileObj->y >> 5) - 0x8000);
        for (idx = 1; idx < g_planeCount; idx++) {
            if (g_planeTable.planes[idx].mapX == mapX && g_planeTable.planes[idx].mapY == mapY) {
                return idx;
            }
        }
        g_planeTable.planes[0].mapX = mapX;
        g_planeTable.planes[0].mapY = mapY;
        g_planeTable.planes[0].nameIndex = g_nearestTileObj->id + 0x100;
        if (g_smokeSourceIdx == 0) {
            g_smokeSourceIdx = -1;
        }
        return 0;
    } else {
        return -1;
    }
}

// ==== seg000:0xc7a2 ====
int computeMapTargetRange(int targetIdx) {
    return computeTargetBearing(g_planeTable.planes[targetIdx].mapX, g_planeTable.planes[targetIdx].mapY, 1);
}

// ==== seg000:0xc7c6 ====
int computeSimObjectRange(int objIdx) {
    return computeTargetBearing(g_simObjects[objIdx].posX, g_simObjects[objIdx].posY, 0);
}

// ==== seg000:0xc7ea ====
int computeTargetBearing(int targetX, int targetY, int wantBearing) {
    int dx;
    int dy;
    dx = g_viewX_ - targetX;
    dy = g_viewY_ - targetY;
    if (wantBearing != 0) {
        g_targetBearing = computeBearing(-dx, dy);
    }
    g_targetRange = rangeApprox(dx, dy);
    return g_targetRange;
}

// ==== seg000:0xc82d ====
int computeLoftAngle() {
    return (int)((unsigned long)((long)(0x4000 - abs(g_ourPitch)) << 12) / (unsigned long)(unsigned int)(g_viewZ + 0x1000)) - 0x4000;
}

// ==== seg000:0xc864 ====
int getTargetSymbol(int wpIdx) {
    if (g_planeTable.planes[wpIdx].flags & 0x80) {
        return (isTargetOverWater(wpIdx) ? (int)(char)g_waterTargetId[0] : (int)(char)g_landTargetId[0]) + 0x100;
    }
    return g_planeTable.planes[wpIdx].nameIndex;
}

// ==== seg000:0xc8a4 ====
int isTargetOverWater(int wpIdx) {
    int category;

    category = ((char *)g_shapeTargetCategory)[g_planeTable.planes[wpIdx].nameIndex & 0x7f] & 0x0f;
    return (category == 12 || category == 9 || category == 11) ? 1 : 0;
}
