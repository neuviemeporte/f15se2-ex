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

static int isPhotoShapeSlot(int shapeId) {
    return (shapeId & 0x100) && (shapeId & 0x7f) >= (int)size3d3;
}

static int replacementShapeSlot(int shapeId) {
    const int slot = shapeId & 0x7f;
    return isPhotoShapeSlot(shapeId) ? slot - (int)size3d3 : slot;
}

static const char *replacementShapeContainer(int shapeId) {
    if (isPhotoShapeSlot(shapeId)) return "PHOTO.3D3";
    return (shapeId & 0x100) ? regnStr : a15flt_xxx;
}

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

static void drawWorldObjectCore(int16 shapeId, int isShadow,
                                int32 worldX, int32 worldY, int16 altitude, int16 objYaw,
                                int16 objPitch, int16 objRoll, int16 scaleShift) {
    int16 *drawPg;
    int dataOff;
    long relX;
    long relY;
    int altDiff;
    int shiftAmt;
    int efx = 0, efy = 0, efz = 0;
    int vfx, vfy, vfz;

    dataOff = shapeDataOffset(shapeId);
    drawPg = g_pageFront;
    relX = worldX - g_ViewX;
    relY = worldY + g_ViewY - 0x01000000L;
    altDiff = altitude - g_viewZ;
    if ((g_viewMode & 0x80) != 0) {
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
                R3DSubmit obj = {g_world3dData + dataOff,
                                 replacementShapeSlot(shapeId),
                                 replacementShapeContainer(shapeId),
                                 -objYaw, objPitch, objRoll,
                                 (int16)relX, -(int16)relY, altitude != 0, isShadow};
                r3d_submit(&obj);
            }
        }
    }
}

void drawWorldObject(int16 shapeId, int32 worldX, int32 worldY, int16 altitude, int16 objYaw, int16 objPitch, int16 objRoll, int16 scaleShift) {
    drawWorldObjectCore(shapeId, 0, worldX, worldY, altitude, objYaw, objPitch, objRoll, scaleShift);
}

/* An aircraft's ground shadow: its own model (`shapeId`) at the terrain altitude,
 * carrying the plane's true attitude. The GPU backend flattens the oriented model
 * onto the ground plane and draws it translucent (a banking plane's shadow
 * foreshortens); the software backend draws it level in flat black. */
void drawAircraftShadow(int16 shapeId, int32 worldX, int32 worldY, int16 groundAltitude,
                        int16 objYaw, int16 objPitch, int16 objRoll, int16 scaleShift) {
    drawWorldObjectCore(shapeId, 1, worldX, worldY, groundAltitude, objYaw, objPitch, objRoll, scaleShift);
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
    if ((g_viewMode & 0x80) != 0) {
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
void drawWorldLine(long worldX1, long worldY1, int alt1,
                   long worldX2, long worldY2, int alt2, int color) {
    R3DLine ln;
    if (worldPointToCamera(worldX1, worldY1, alt1, &ln.baseXA, &ln.camXA, &ln.camYA))
        return;
    if (worldPointToCamera(worldX2, worldY2, alt2, &ln.baseXB, &ln.camXB, &ln.camYB))
        return;
    ln.color = color;
    r3d_submitLine(&ln);
}

static int roundToInt(float v) {
    return (int)(v < 0.0f ? v - 0.5f : v + 0.5f);
}

/* Fast 2D distance approximation (rangeApprox) at full 32-bit width: no int16
 * truncation and no 0x7FFF cap, so it can feed a bearing off FINE world deltas. */
static int32 rangeApprox32(int32 dx, int32 dy) {
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx > dy ? dx + (dy >> 1) : dy + (dx >> 1);
}

/* computeBearing on a delta pair too wide for its int16 inputs: shift both by the
 * same amount (the angle depends only on the ratio) until they fit. Lets the target
 * view take the bearing off the FINE world deltas instead of the coarse (>>5) map
 * units the original fed it — those coarse deltas requantize every render frame
 * under the sim/render decouple, so the tracked model shivered; the fine deltas
 * vary smoothly, so it steps at most one pixel at a time. */
static int bearingFromFine(int32 deltaX, int32 deltaY) {
    int32 m = (deltaX < 0 ? -deltaX : deltaX) | (deltaY < 0 ? -deltaY : deltaY);
    int sh = 0;
    while ((m >> sh) > 0x3fff) sh++;
    return computeBearing((int)(deltaX >> sh), (int)(deltaY >> sh));
}

// ==== seg000:0xcb42 ====
/* worldX/worldY are the target's FINE world position (mapX<<5 scale, matching
 * g_ViewX/g_ViewY and SimObject.worldX). The original took the coarse map coords
 * (posX/mapX) and subtracted the coarse g_viewX_; with the port's render/sim
 * decouple both operands round to the ÷32 grid independently and beat ±1 against
 * each other between sim ticks, so the tracked model jittered on a ~32-unit grid.
 * Differencing the fine coords (once, below) and taking the bearing/pitch off those
 * fine deltas removes both the beat and the coarse angular snapping. */
void drawTargetView(int shapeId, int32 worldX, int32 worldY, int altitude, int objYaw, int objPitch, int objRoll, int mode, int shift) {
    int32 dxFine, dyFine, dzFine;
    int unused;
    int horizonY;
    int bearing;
    int range;
    int pitchDelta;
    int category;
    int pitch;
    int dataOff;
    int colorIdx;
    int bearingDelta;
    int relX;
    int relY;
    int relZ;
    int fracX = 0, fracY = 0, fracZ = 0; /* Q8 sub-unit remainders of the submit position */
    char categoryLow;

    g_targetInHudFlag = 1;

    dataOff = shapeDataOffset(shapeId);
    *g_targetViewParams = 1;

    dxFine = worldX - g_ViewX;
    dyFine = worldY - g_ViewY;
    dzFine = altitude - g_viewZ;

    if (mode < 2) {
        g_trkRoll = 0;
        relX = (int)(dxFine >> 5);
        relY = (int)(dyFine >> 5);
        relZ = (int)(dzFine >> 5);
        /* Bearing/pitch off the fine deltas so the tracked model glides; the range
         * (model size / tracking scale) keeps the original coarse magnitude. */
        bearing = bearingFromFine(dxFine, -dyFine);
        pitch = bearingFromFine(dzFine, rangeApprox32(dxFine, dyFine));
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

        g_extraScaleShift = 2;
        if (shift < 0) {
            g_extraScaleShift = (uint8)(shift + 2);
            shift = 0;
        }
        /* The MFD is a ~1° telephoto: one whole unit of this offset is ~3 screen
         * pixels, and the original's integer sinMul/cosMul (truncating to whole
         * units) made the model saw-tooth around the aim point as the quantized
         * bearing/position raced each other. Compute the offset in float off the
         * same sine table the view matrix uses, round once, and hand the sub-unit
         * remainder to the transform as the Q8 viewer fraction — the model then
         * tracks the quantized camera exactly and sits still. */
        {
            float radiusF = (float)cosine(pitch) * (float)range * (1.0f / 32768.0f);
            float div = (float)(1 << (char)shift);
            float relXf = (float)sine(bearing) * radiusF * (1.0f / 32768.0f) / div;
            float relYf = -((float)cosine(bearing) * radiusF * (1.0f / 32768.0f)) / div;
            float relZf = (float)sine(pitch) * (float)range * (1.0f / 32768.0f) / div;
            relX = roundToInt(relXf);
            relY = roundToInt(relYf);
            relZ = roundToInt(relZf);
            /* transformAndCullObject folds `true rel = rel - frac/256`; the Y axis
             * is submitted negated (see the R3DSubmit below). */
            fracX = roundToInt((relX - relXf) * 256.0f);
            fracY = roundToInt((relYf - relY) * 256.0f);
            fracZ = roundToInt((relZ - relZf) * 256.0f);
        }
    } else {
        relX = (int)(dxFine >> 5) << 4;
        relY = (int)(dyFine >> 5) << 4;
        relZ = (int)(dzFine >> 1);
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
            fillSpanRectImmediate(g_targetViewParams, 232, 128, 304, horizonY);
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
            fillSpanRectImmediate(g_targetViewParams, 232, horizonY, 304, 184);
        }
    }

    g_offscreenRender = 1;
    {
        R3DScene scene = {g_targetViewParams, -g_trkBearing, g_trkPitch, g_trkRoll, 0, 0, 0, 0};
        R3DSubmit obj = {g_world3dData + dataOff,
                         replacementShapeSlot(shapeId),
                         replacementShapeContainer(shapeId),
                         -objYaw, objPitch, objRoll, relX, -relY, relZ};
        r3d_beginScene(&scene);
        /* after beginScene: setup3DTransform's setViewPosition cleared the frac */
        setViewPositionFrac(fracX, fracY, fracZ);
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
int shapeDataOffset(int shapeId) {
    if (shapeId & 0x100) {
        return buf3d3[shapeId & 0x7f];
    }
    return (int)(&g_aircraftModels[((int16 *)flt15_buf1)[shapeId]] - g_world3dData);
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
int egClampValue(int value, int minVal, int maxVal) { /* Original: rng2(x,a,b). Plain clamp between min and max. */
    if (value > maxVal) {
        return maxVal;
    }
    if (value < minVal) {
        return minVal;
    }
    return value;
}

// ==== seg000:0xcfa6 ====
int rangeApprox(int deltaX, int deltaY) { /* Original: xydist(x,y). Fast 2D distance approximation capped at 0x7fff. */
    enum { XYDIST_MAX = 0x7FFF };
    long dist;
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
        numer = (long)abs16Compat(deltaY) << 0xe;
        denom = abs16Compat(deltaX);
        swapped = 1;
    } else {
        numer = (long)abs16Compat(deltaX) << 0xe;
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

// ==== seg000:0xd200 randomRange ====
int randomRange(int maxVal) { /* Original: rnd(Max). Deterministic ((long)Max * rand()) >> 15 range scaling. */
    enum { RAND_SCALE_SHIFT = 15 };
    /* DOS rand() is 15-bit (RAND_MAX 0x7fff); mask to match so the >>15 scaling yields [0, maxVal). */
    return (int)(((long)(rand() & 0x7fff) * (long)maxVal) >> RAND_SCALE_SHIFT);
}

/* One axis of a gun's dispersion, in 16-bit angle units (0x10000 = full turn,
 * so 1 milliradian ~= 10.4 units). The M61's spec cone is ~8 mil diameter; the
 * sum of two rolls gives a triangular distribution peaked on the aim line. */
int16 gunSpreadAngle(void) {
    return (int16)(randomRange(45) + randomRange(45) - 44);
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
