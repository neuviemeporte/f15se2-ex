// seg000 optimized code (/Ot)
#include "inttype.h"
#include "eg3dcam.h"
#include "eg3dload.h"
#include "eg3dmap.h"
#include "egcode.h"
#include "egdata.h"
#include "egframe.h"
#include "egmath.h"
#include "egtacmap.h"
#include "egthreat.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "slot.h"
#include "const.h"
#include "r3d.h"

#include <dos.h>
#include <memory.h>

// ==== seg000:0xc8de ====

void load15Flt3d3() {
    int16 bytesLeft, chunkSize;
    struct SREGS segs;
    char FAR *dest;
    strcpyFromDot(a15flt_xxx, ".3D3");
    fileHandle = openFile(a15flt_xxx, 0);
    if (fileHandle == NULL) {
        printError("Open Error on *.3D3");
        return;
    }
    fileRead(&flt15HeaderWord, 2, 1, fileHandle);
    fileRead(&flt15_size, 2, 1, fileHandle);
    fileRead(flt15_buf1, 2, flt15_size, fileHandle);
    fileRead(&bytesLeft, 2, 1, fileHandle);
    dest = g_aircraftModels;
    /* Original staged each chunk through a near buffer and movedata'd it into the
     * far model region; natively g_aircraftModels is a real buffer, read into it. */
    while (bytesLeft > 0) {
        chunkSize = bytesLeft <= 0x800 ? bytesLeft : 0x800;
        fileRead(dest, 1, chunkSize, fileHandle);
        bytesLeft -= 0x800;
        dest += 0x800;
    }
    fileClose(fileHandle);
}

void drawWorldObject(int16 shapeId, int32 worldX, int32 worldY, int16 altitude, int16 objYaw, int16 objPitch, int16 objRoll, int16 scaleShift) {
    int16 *drawPg;
    int16 dataOff;
    int32 relX, relY;
    int16 altDiff, shiftAmt;
    int efx = 0, efy = 0, efz = 0;
    int vfx, vfy, vfz;

    dataOff = shapeDataOffset(shapeId);
    drawPg = g_pageFront;
    relX = worldX - g_ViewX;
    relY = worldY + g_ViewY - 0x01000000L;
    altDiff = altitude - g_viewZ;
    if ((keyValue & 0x80) != 0) {
        relX += g_ViewX - g_camEyeX;
        relY += g_camEyeY - g_ViewY;
        altDiff += g_viewZ - g_camEyeZ;
        /* Q8 remainder of the external eye position (true eye is frac/256
         * further along each axis than the integer subtracted above). */
        efx = g_camEyeFracX;
        efy = g_camEyeFracY;
        efz = g_camEyeFracZ;
    }
    scaleShift = (g_halfScaleRender != 0) ? (scaleShift - 2) : (scaleShift - 3);
    if (scaleShift > 0) {
        shiftLongLeftInPlace(scaleShift, &relX);
        shiftLongLeftInPlace(scaleShift, &relY);
        altDiff <<= (char)scaleShift;
        vfx = efx << scaleShift;
        vfy = efy << scaleShift;
        vfz = efz << scaleShift;
    } else if (scaleShift < 0) {
        /* full int, not just the low byte: native shift uses all 32 bits */
        long preX = relX, preY = relY;
        int preZ = altDiff;
        shiftAmt = -scaleShift;
        shiftLongRightInPlace(shiftAmt, &relX);
        shiftLongRightInPlace(shiftAmt, &relY);
        altDiff >>= (char)shiftAmt;
        /* Viewer fraction (Q8, submit units) combining the fraction the
         * arithmetic shift floors away (up to 8 fine units at full scale) with
         * the eye remainder — close objects (own plane in chase view,
         * just-fired missiles) otherwise step. Signs follow the submit below:
         * posX carries +relX, posY carries −relY, viewPosZ = −altDiff. */
        vfx = (efx - ((int)(preX - (relX << shiftAmt)) << 8)) >> shiftAmt;
        vfy = (((int)(preY - (relY << shiftAmt)) << 8) + efy) >> shiftAmt;
        vfz = (efz - ((preZ - (altDiff << shiftAmt)) << 8)) >> shiftAmt;
    } else {
        vfx = efx;
        vfy = efy;
        vfz = efz;
    }
    if ((long)(int16)labs(relX) < (long)0x7FFF) {
        if ((long)(int16)labs(relY) < (long)0x7FFF) {
            setViewPosition(0, 0, -altDiff);
            setViewPositionFrac(vfx, vfy, vfz);
            g_curLod = 1;
            {
                R3DSubmit obj = {g_world3dData + dataOff, -objYaw, objPitch, objRoll,
                                 (int16)relX, -(int16)relY, altitude != 0};
                r3d_submit(&obj);
            }
        }
    }
}

/* Transform one world point (worldX/worldY in drawWorldObject's long convention,
 * altitude) into scene camera space, mirroring drawWorldObject's origin math at
 * scaleShift 0. A bare origin's projected screen position is scaleShift-invariant,
 * so this lands exactly where a smoke particle at the point would. Returns 1 if
 * the relative offset overflows int16 (too far — drop the endpoint). */
static int worldPointToCamera(long worldX, long worldY, int altitude,
                              long *baseX, long *camX, long *camY) {
    long relX, relY;
    int altDiff, shiftAmt;

    relX = worldX - g_ViewX;
    relY = worldY + g_ViewY - 0x01000000L;
    altDiff = altitude - g_viewZ;
    if ((keyValue & 0x80) != 0) {
        relX += g_ViewX - g_camEyeX;
        relY += g_camEyeY - g_ViewY;
        altDiff += g_viewZ - g_camEyeZ;
    }
    /* scaleShift 0 -> -2 (half-scale) / -3 (full) in drawWorldObject; a right shift. */
    shiftAmt = (g_halfScaleRender != 0) ? 2 : 3;
    shiftLongRightInPlace(shiftAmt, &relX);
    shiftLongRightInPlace(shiftAmt, &relY);
    altDiff >>= (char)shiftAmt;
    if ((long)(int16)labs(relX) >= (long)0x7FFF) return 1;
    if ((long)(int16)labs(relY) >= (long)0x7FFF) return 1;
    /* Matches projectSceneObject's inputs after setViewPosition(0,0,-altDiff):
     * g_objRelX = (int16)relX, g_objRelY = -(int16)relY, g_objTransform[0] = altDiff
     * (+1 when altitude != 0, from posZ). */
    r3d_worldPointToCameraFar(-(int16)relY, altDiff + (altitude != 0),
                              (int16)relX, baseX, camX, camY);
    return 0;
}

/* Submit a world-space 3D line segment (cannon tracer / explosion spark) into the
 * current scene. worldX/worldY are in drawWorldObject's long convention (fine map
 * coord << 5), alt is altitude, color is a final palette index. Drawn like a model
 * line: depth-sorted + occluded (software), z-tested + fogged (GL). */
void drawWorldLine(int32 worldX1, int32 worldY1, int16 alt1,
                   int32 worldX2, int32 worldY2, int16 alt2, int color) {
    R3DLine ln;
    if (worldPointToCamera(worldX1, worldY1, alt1, &ln.baseXA, &ln.camXA, &ln.camYA))
        return;
    if (worldPointToCamera(worldX2, worldY2, alt2, &ln.baseXB, &ln.camXB, &ln.camYB))
        return;
    ln.color = color;
    r3d_submitLine(&ln);
}

// ==== seg000:0xcb42 ====
void drawTargetView(int16 shapeId, int16 worldX, int16 worldY, int16 altitude, int16 objYaw, int16 objPitch, int16 objRoll, int16 mode, int16 shift) {
    int16 unused, horizonY, bearing, range, pitchDelta, category, pitch, dataOff, colorIdx, radius, bearingDelta, relX, relY, relZ;
    char categoryLow;

    g_targetInHudFlag = 1;

    dataOff = shapeDataOffset(shapeId);
    *g_targetViewParams = 1;

    if (mode < 2) {
        g_trkRoll = 0;
        relX = worldX - g_viewX_;
        relY = worldY - g_viewY_;
        relZ = (altitude - g_viewZ) >> 5;
        bearing = computeBearing(relX, -relY);
        pitch = computeBearing(relZ, rangeApprox(relX, relY));
        range = rangeApprox(relZ, rangeApprox(relX, relY));

        if (mode == 1) {
            g_trkRange = range;
            g_trkSize = (range >> 4) + 400;
            g_trkScale = (g_trkSize << 5) / (range + 1);
            range = g_trkSize << 2;
            g_trkBearing = bearing;
            g_trkPitch = pitch;
        } else {
            g_trkScale = (g_trkRange << 5) / (range + 1);
            if (g_trkScale > 0x100) {
                g_trkScale = 0x100;
            }
            if (g_trkScale < 4) {
                g_trkScale = 4;
            }
            bearingDelta = ((bearing - g_trkBearing) >> 5) * g_trkScale;
            pitchDelta = ((pitch - g_trkPitch) >> 5) * g_trkScale;
            if (abs(bearingDelta) > 0x1000) {
                return;
            }
            if (abs(pitchDelta) > 0x1000) {
                return;
            }
            bearing = (bearingDelta << 2) + g_trkBearing;
            pitch = (pitchDelta << 2) + g_trkPitch;
            range = (g_trkSize << 5) / g_trkScale << 2;
        }

        radius = cosMul(pitch, range);
        g_extraScaleShift = 2;
        if (shift < 0) {
            g_extraScaleShift = (uint8)(shift + 2);
            shift = 0;
        }
        relX = sinMul(bearing, radius) >> (char)shift;
        relY = -(cosMul(bearing, radius)) >> (char)shift;
        relZ = sinMul(pitch, range) >> (char)shift;
    } else {
        relX = (worldX - g_viewX_) << 4;
        relY = (worldY - g_viewY_) << 4;
        relZ = (altitude - g_viewZ) >> 1;
        g_trkBearing = g_ourHead;
        g_trkPitch = g_extViewPitch;
        g_trkRoll = g_ourRoll;
        g_trkScale = 0x20;
        g_extraScaleShift = 2;
    }
    if (mode == 1 || mode == 3) {
        horizonY = (int16)((int32)g_trkScale * (int32)(g_trkPitch >> 2) >> 5) + 156;
        if (horizonY < 128 || g_trkPitch < (int16)0xe800) {
            horizonY = 128;
        }
        if (horizonY > 184 || g_trkPitch > 0x1800) {
            horizonY = 184;
        }
        *(g_targetViewParams + 2) = colorLut[3];
        if (horizonY != 128) {
            fillSpanRect(g_targetViewParams, 232, 128, 304, horizonY);
        }
        colorIdx = g_world3dData[0x2f];
        category = (int16)(signed char)g_shapeTargetCategory[shapeId & 0x7f];
        if (category & 0x10) {
            colorIdx = 8;
        }
        categoryLow = (char)(category & 0xf);
        if (categoryLow == 12 || categoryLow == 9 || categoryLow == 11) {
            colorIdx = 1;
        }
        *(g_targetViewParams + 2) = colorLut[colorIdx];
        if (horizonY != 184) {
            fillSpanRect(g_targetViewParams, 232, horizonY, 304, 184);
        }
    }

    g_offscreenRender = 1;
    {
        R3DScene scene = {g_targetViewParams, -g_trkBearing, g_trkPitch, g_trkRoll, 0, 0, 0, 0};
        R3DSubmit obj = {g_world3dData + dataOff, -objYaw, objPitch, objRoll, relX, -relY, relZ};
        r3d_beginScene(&scene);
        r3d_submit(&obj);
        r3d_endScene();
    }
    g_offscreenRender = 0;

    if (mode == 1) {
        strcpy(strBuf, "BRG ");
        /* DOS `unsigned int` was 16-bit: a negative bearing wrapped into 0-359°.
         * Truncate to uint16 before the divide so the native 32-bit unsigned cast
         * doesn't blow a small negative up into millions. */
        strcat(strBuf, itoa((uint16)g_trkBearing / 0xb6, g_itoaScratch, 10));
        drawStringActivePage(strBuf, 248, 176, 0xf);
    }
    g_extraScaleShift = 0;
}

// ==== seg000:0xcf32 ====
int16 shapeDataOffset(int16 shapeId) {
    if (shapeId & 0x100) {
        return buf3d3[shapeId & 0x7f];
    }
    return (int16)(&g_aircraftModels[((int16 *)flt15_buf1)[shapeId]] - g_world3dData);
}

// ==== seg000:0xcf64 clamp ====
int16 clampRange(int16 value, int16 minVal, int16 maxVal) { /* Original: rng(x,a,b). Clamp value, preserving the <= -0x4000 wrap-to-max case. */
    enum { RNG_WRAP_FLOOR = -0x4000 };
    /* Unlike a plain clamp, very negative wrapped angles select the high end. */
    if (value > maxVal) {
        return maxVal;
    }
    if (value >= minVal) {
        return value;
    }
    if (value <= RNG_WRAP_FLOOR) {
        return maxVal;
    }
    return minVal;
}

// ==== seg000:0xcf8e ====
int16 clampValue(int16 value, int16 minVal, int16 maxVal) { /* Original: rng2(x,a,b). Plain clamp between min and max. */
    if (value > maxVal) {
        return maxVal;
    }
    if (value < minVal) {
        return minVal;
    }
    return value;
}

#define XYDIST_MAX 0x7FFF

// ==== seg000:0xcfa6 ====
int16 rangeApprox(int16 deltaX, int16 deltaY) { /* Original: xydist(x,y). Fast 2D distance approximation capped at 0x7fff. */
    int32 dist;
    deltaX = abs16Compat(deltaX);
    deltaY = abs16Compat(deltaY);
    /* Fast 2D distance approximation: max(abs) + half of min(abs). */
    if (deltaX > deltaY)
        dist = (int32)(deltaY >> 1) + (int32)deltaX;
    else
        dist = (int32)(deltaX >> 1) + (int32)deltaY;
    if (dist > XYDIST_MAX) dist = XYDIST_MAX;
    return (int16)dist;
}

// ==== seg000:0xd008 ====
int16 computeBearing(int16 deltaX, int16 deltaY) {
    int16 angle, result;
    int32 numer;
    int16 denom, swapped, ratio;

    if (deltaX == 0) {
        if (deltaY > 0) return 0;
        return BEARING_SOUTH;
    }
    if (deltaY == 0) {
        if (deltaX > 0) return BEARING_EAST;
        return BEARING_WEST;
    }
    if (abs16Compat(deltaX) > abs16Compat(deltaY)) {
        numer = (int32)abs16Compat(deltaY) << 0xe;
        denom = abs16Compat(deltaX);
        swapped = 1;
    } else {
        numer = (int32)abs16Compat(deltaX) << 0xe;
        denom = abs16Compat(deltaY);
        swapped = 0;
    }
    ratio = numer / (int32)denom;
    angle = ((0x2800L - (((int32)abs(0x1333 - ratio) * 0xB00L) >> 0xe)) * (int32)ratio) >> 0xe;
    if (deltaX > 0) {
        if (deltaY > 0)
            result = swapped ? BEARING_EAST - angle : angle;
        else
            result = swapped ? angle + BEARING_EAST : BEARING_SOUTH - angle;
    } else {
        if (deltaY > 0)
            result = swapped ? angle + BEARING_WEST : -angle;
        else
            result = swapped ? BEARING_WEST - angle : angle + BEARING_SOUTH;
    }
    return result;
}

// ==== seg000:0xd178 sinMul ====
int16 sinMul(int16 angle, int16 value) { /* Original: sinX(angle,x). Fixed-point sine lookup multiplied by value. */
    /* Sine table values are fixed-point; fixedMulQ14 applies the scale. */
    return fixedMulQ14(sine(angle), value);
}

// ==== seg000:0xd190 cosMul ====
int16 cosMul(int16 angle, int16 value) { /* Original: cosX(angle,x). Cosine via sine phase shift. */
    enum { WORD_DEGREES_QUARTER_TURN = 0x4000 };
    return sinMul(angle + WORD_DEGREES_QUARTER_TURN, value);
}

/* sinMul/cosMul keeping 8 fractional bits (result = sinMul<<8 without the Q15
 * truncation) — for the external camera eye, where a whole-unit result makes
 * the view lurch as the offset rotates. */
long sinMulQ8(int angle, int value) {
    return ((long)sine(angle) * value) >> 7;
}

long cosMulQ8(int angle, int value) {
    return sinMulQ8(angle + 0x4000, value);
}

// ==== seg000:0xd1c8 ====
int16 signOf(int16 value) { /* Original: sgn(x). Return -1, 0, or 1. */
    if (value == 0) {
        return 0;
    }
    if (value > 0) {
        return 1;
    }
    return -1;
}

void seedRng(void) {
    if (g_inputDisabled == 0) {
        g_rngSeed = getTimeOfDay();
    }
    srand(g_rngSeed);
}

#define RAND_SCALE_SHIFT 15

// ==== seg000:0xd200 randomRange ====
int16 randomRange(int16 maxVal) { /* Original: rnd(Max). Deterministic ((long)Max * rand()) >> 15 range scaling. */
    /* DOS rand() is 15-bit (RAND_MAX 0x7fff); mask to match so the >>15 scaling yields [0, maxVal). */
    return (int16)(((int32)(rand() & 0x7fff) * (int32)maxVal) >> RAND_SCALE_SHIFT);
}

// ==== seg000:0xd21e ====
int16 readAxisInput(int16 axisIdx) {
    int16 value;

    if (g_inputDisabled) {
        value = 0;
    } else {
        value = ((commData->setupUseJoy) ? misc_readJoystick(axisIdx) : 0) + g_axisInputAccum[axisIdx];
    }
    return value;
}
