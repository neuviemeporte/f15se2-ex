/* egtarget.c — target lock + HUD overlay (reads g_viewZ as uint16). */
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
#include "r2d.h"
#include "r3dmesh.h"
#include "const.h"

#include "comm.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>

/* Private helpers for this translation unit. */
void drawTargetBox(int16, int16, int16, int16);
void drawTargetBoxF(float centerX, float centerY, int size, int mode);
void drawMissileLock(void);
void drawTargetLabel(const char *, int16, int16);
void buildRangeString(int16 rangeRaw);
void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
void projectWorldToHudFine(int32 fineX, int32 fineY, int fineZ);
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
int16 computeMapTargetRange(int16 targetIdx);
int16 computeSimObjectRange(int16 objIdx);
int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);

void updateTargetLock(void) {
    int16 p, a, b, range, d, e, marker, idx, depthShift, i, j, k, best, m, n;
    int16 p0, a0, b0, c0, d0, e0, deadInit, lockedRange, h0;
    int16 dk;
    int16 lodM, planeModelDepth, planeFineDepth;
    int16 airSelect;

    deadInit = 0;

    /* Fire at g_viewMode == 0x8b (sidewinder lock) */
    if (g_viewMode == VIEW_TARGET) {
        drawWorldObject(6, (int32)g_ViewX, 0x01000000L - g_ViewY,
                        g_viewZ + 0x10, g_ourHead, g_ourPitch, g_ourRoll, 2);
    }

    if (g_aamLockCooldown != 0) {
        g_aamLockCooldown--;
    }

    if (!(g_groundTargetLock & 0x80)) {
        if (frameTick & 0x0f) goto skip_aam;
        if (g_aamLockActive != 0) goto skip_aam;
    }
    if (g_activePanelMode != 0x13) goto skip_aam;
    if (g_aamLockCooldown != 0) goto skip_aam;
    if (g_currentWeaponType == 1) goto skip_aam;
    if (g_viewMode & 0x80) goto skip_aam;

    if (!(g_groundTargetLock & 0x80)) {
        g_groundTargetLock = best = -1;
    }

    range = 100 << (6 - (uint8)g_nightMode);

    if (g_groundTargetLock != -1) {
        idx = g_groundTargetLock - 0x80;
        lockedRange = computeMapTargetRange(idx) - 1;
        if (g_planeTable.planes[idx].active != 0) {
            lockedRange -= 0x280;
        }
        if (idx < 3) {
            lockedRange -= 0x0a00;
        }
        if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) > 0x2000) {
            lockedRange = -32000;
            goto after_lock;
        }
        g_aamLockActive = 1;
    after_lock:;
    } else {
        g_aamLockActive = 0;
        lockedRange = -32000;
    }

    best = -1;
    for (idx = 1; idx < g_planeCount; idx++) {
        computeMapTargetRange(idx);
        if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) < 0x1800 &&
            idx + 0x80 != g_groundTargetLock && !(g_planeTable.planes[idx].flags & 0x80)) {
            if (g_planeTable.planes[idx].active != 0) {
                g_targetRange -= 0x280;
            }
            if (idx == g_targetSlots[0].planeIndex || idx == g_targetSlots[1].planeIndex) {
                g_targetRange -= 0x0a00;
            }
            if (range > g_targetRange && lockedRange < g_targetRange) {
                best = idx;
                range = g_targetRange;
            }
        }
    }

    if (best & 0x80) {
        if (g_groundTargetLock == -1) {
            g_aamLockCooldown = 4;
        } else {
            g_groundTargetLock = -1;
        }
    } else {
        g_groundTargetLock = best;
        g_lockedTargetKilled = 0;
    }

skip_aam:
    /* Missile/chaff loop (8 entries, stride 8) */
    for (idx = 0; idx < 8; idx++) {
        if (g_particles[idx].posX != 0) {
            projectWorldToHud(g_particles[idx].posX,
                              g_particles[idx].posY,
                              g_particles[idx].alt);
            if (g_projDepth < 0 && g_projDepth > -0x100) {
                drawWorldObject(
                    (uint8)(((uint8)g_smokeParticleSlot - (uint8)idx) & 7) < 4 ? 3 : 17,
                    (int32)(uint16)g_particles[idx].posX << 5,
                    (int32)(uint16)g_particles[idx].posY << 5,
                    g_particles[idx].alt, 0,
                    g_particles[idx].spin, 0, 0);
            }
        }
    }

    /* Air-to-ground targeting */
    range = 0x4b << (6 - (uint8)g_nightMode);

    /* depthShift is the original distance/altitude "spottability" zoom: with a far
       target (or high altitude) it right-shifts world objects' positions in
       drawWorldObject to draw them as if closer — bigger — without scaling their
       geometry, so distant aircraft/scenery balloon and then shrink to true size as
       the range closes. Detail level 4 ("no LOD tricks") disables it, so world
       objects hold true perspective size at any range (far ones resolve to the
       spottable single-pixel dot via the model-depth gate instead of ballooning). */
    if (g_detailLevel >= 4) {
        depthShift = 0;
    } else {
        depthShift = (g_hudVisible != 0 && (uint16)(g_nearestThreatRange + g_viewZ) > 1500) ? 1 : 0;
        if (g_hudVisible != 0 && (uint16)(g_nearestThreatRange + g_viewZ) > 4000) {
            depthShift = 2;
        }
    }

    /* Detail level 4 widens the depth band over which an air contact resolves
       from a single pixel (the drawViewportLine fallback below) to a full model,
       reaching -0x100 — the engine's established far model range (cf. the
       particle/wreck loops, which draw models out to -0x100). Levels 0-3 keep the
       original -0x20 gate. */
    lodM = (g_detailLevel >= 4) ? 8 : 1;
    planeModelDepth = -0x20 * lodM; /* dot -> model gate: -0x20 (normal) / -0x100 (detail 4) */
    planeFineDepth = -0x10 * lodM;  /* coarse -> fine model: -0x10 (normal) / -0x80 (detail 4) */

    /* Air-target select. The 0x80 bit means "(re)acquire": either no current
       lock (-1) or the T key just requested the next target. With an A2A missile
       selected the lock is then sticky (T cycles, like the ground-target lock);
       other weapons keep auto-acquiring the nearest contact every frame. */
    if (g_airTargetLock & 0x80) {
        airSelect = 1;
        if (g_airTargetLock != -1) {
            idx = g_airTargetLock - 0x80;
            lockedRange = computeTargetBearing(g_simObjects[idx].posX, g_simObjects[idx].posY, 1);
            if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) > 0x2000) {
                lockedRange = 0;
            }
        } else {
            lockedRange = 0;
        }
    } else if (g_currentWeaponType == 1 &&
               (g_simObjects[g_airTargetLock].flags.b[0] & 0x22) == 2) {
        /* A2A missile selected and the designated target is still a live
           contact: hold the lock (T cycles to the next one). */
        airSelect = 0;
        lockedRange = 0;
    } else {
        airSelect = 1;
        lockedRange = 0;
    }

    best = -1;
    for (idx = 0; idx < g_groundUnitCount; idx++) {
        if (!(g_simObjects[idx].flags.b[0] & 2))
            goto next2;

        if (computeSimObjectRange(idx) >= 4800 && g_directorMode == 0)
            goto next2;

        if (airSelect && range > g_targetRange && lockedRange < g_targetRange && !(g_viewMode & 0x80) &&
            !(g_simObjects[idx].flags.b[0] & 0x20) &&
            g_simObjects[idx].speed != 0) {
            computeTargetBearing(g_simObjects[idx].posX, g_simObjects[idx].posY, 1);
            if (abs((int16)(g_ourHead + g_viewHeadingOffset - g_targetBearing)) < 0x2000) {
                range = g_targetRange;
                best = idx;
            }
        }

        projectWorldToHud(g_simObjects[idx].posX, g_simObjects[idx].posY, g_simObjects[idx].alt);

        if (g_projDepth >= 0)
            goto next2;

        g_projDepth >>= depthShift;

        if (g_projDepth > planeModelDepth) {
            if (g_simObjects[idx].alt < 999 && g_nightMode == 0) {
                marker = 0;
                if ((g_planeTable.planes[g_closestThreatIndex].flags & 0x200) &&
                    abs(g_simObjects[idx].posX - g_planeTable.planes[g_closestThreatIndex].mapX) < g_attackRangeX >> 5 &&
                    abs(g_simObjects[idx].posY - g_planeTable.planes[g_closestThreatIndex].mapY) < g_attackRangeY >> 5) {
                    marker = 0x80;
                }
                if (g_viewZ != 0x80 || marker == 0x80) {
                    drawWorldObject(5, g_simObjects[idx].worldX, g_simObjects[idx].worldY,
                                    marker, g_simObjects[idx].heading.w, 0, 0,
                                    -(signOf(depthShift) - 2));
                }
            }

            /* Draw the target */
            drawWorldObject(
                (&aircraftTypes[g_simObjects[idx].spec].viewModelId)[(g_projDepth > planeFineDepth) ? 0 : 1],
                g_simObjects[idx].worldX, g_simObjects[idx].worldY, g_simObjects[idx].alt,
                g_simObjects[idx].heading.w, g_simObjects[idx].pitch,
                g_simObjects[idx].bank.w, 2 - depthShift);
        } else {
            setDrawColor(COLOR_WHITE);;
            drawViewportLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
        }
    next2:;
    }

    if (best != -1) {
        g_airTargetLock = best;
        g_lockedTargetKilled = 0;
    }
    if (g_airTargetLock & 0x80) {
        g_airTargetLock = -1;
    }

    /* SAM/missile visual loop (12 entries, stride 0x18) */
    for (idx = 0; idx < 12; idx++) {
        if (g_projectiles[idx].ttl != 0) {
            projectWorldToHud(g_projectiles[idx].mapX, g_projectiles[idx].mapY, g_projectiles[idx].alt);

            if (vtxScratch.vproj.x.lo == -1)
                goto next3;

            if (g_projDepth > -0x20) {
                drawWorldObject(sams[g_projectiles[idx].specIdx].modelId,
                                g_projInterpX[idx],
                                g_projInterpY[idx],
                                g_projectiles[idx].alt,
                                g_projectiles[idx].worldX, g_projectiles[idx].worldY,
                                g_projectiles[idx].worldZ + 0x2000,
                                ((g_viewMode & 0x80) && g_viewMode != 0x8b) ? 3 : 1);
            } else {
                setDrawColor(idx < 8 ? COLOR_LIGHTRED : COLOR_FLAMING);
                drawViewportLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
            }
        }
    next3:;
    }

    /* Runway/base visual */
    if (g_wreckAlt > 0) {
        projectWorldToHud(g_wreckX, g_wreckY, g_wreckAlt);
        if (g_projDepth < 0 && g_projDepth > -0x100) {
            drawWorldObject(14,
                            (int32)(uint16)g_wreckX << 5,
                            (int32)(uint16)g_wreckY << 5,
                            g_wreckAlt, 0, 0, 0,
                            g_wreckFallVel > 0 ? 4 : 3);
        }
    }

    /* Player's own aircraft fire */
    if (!(g_viewMode & 0x80)) goto done;
    if (g_viewMode == VIEW_TARGET) goto done;
    if (g_viewZ == 0 && g_ejectState != 0) goto done;

    drawWorldObject(((g_playerPlaneFlags & 1) == 0) + 6, (int32)g_ViewX,
                    0x01000000L - g_ViewY, g_viewZ + 0x10, g_ourHead, g_ourPitch, g_ourRoll,
                    2 - depthShift);

    if ((uint16)g_viewZ < 1000 && g_nightMode == 0) {
        drawWorldObject(21, (int32)g_ViewX, 0x01000000L - g_ViewY,
                        g_groundAltitude, g_ourHead, 0, 0, 2);
    }

done:;
}

/* World-space radius of an explosion burst, in fine map units (the alt axis
 * shares the same 1/32-coarse scale). Tunable by eye. */
static const int EXPLOSION_WORLD_RADIUS = 0x20;

/* Gun hit box for aircraft, sized to the actual model. The original hit test
 * used a flat radius (0x200/isqrt(frameRateScaling*4+8), ~64 map units at full
 * rate / difficulty 0) for every target, so a bomber and a fighter shared the
 * same oversized box and shots well off-target still scored. Instead we derive
 * the box from each type's 3D model bounding size, computed once at load.
 *
 * s_aircraftModelRadius[spec] is the model-space bounding-cube half-extent of
 * the type's near view model, in model units; 1 model unit == 2 fine world units
 * (drawWorldObject scale at true size), so *2 converts to the fine units the
 * swept hit test works in. Fighters come out ~30 (=>~60 fine), bombers ~64.
 *
 * The hit test is a point-to-segment distance: because rounds are only sampled
 * once per 15 Hz sim step they leap many units between samples, so a box that
 * exactly hugs a ~60-fine plane is nearly unhittable by point sampling. Testing
 * the round's swept path this step (pos-vel .. pos) against the target sphere
 * makes the tight, model-sized box actually connect. GUN_AIRCRAFT_HIT_SCALE_Q8
 * is a Q8 slack on the radius (256 = exactly the model bound). */
static const int GUN_AIRCRAFT_HIT_SCALE_Q8 = 256;
/* Gun-vs-ground box, Q8 slack on the ground target's world-shape footprint.
 * GROUND_HIT_MIN_MAP keeps tiny/point-model targets (footprint ~0) hittable
 * against the coarse ground-impact sampling, while staying far tighter than the
 * original flat 12-map radius. */
static const int GUN_GROUND_HIT_SCALE_Q8 = 256;
static const int GROUND_HIT_MIN_MAP = 6;

/* Model-space bounding half-extents (model units). Aircraft models come from the
 * constant 15FLT.3D3, world (ground/tile) shapes from the per-theater region, so
 * both are refilled each mission by computeHitRadii(). */
static int16 s_aircraftModelRadius[19];
static int16 s_worldShapeRadius[100]; /* indexed by (nameIndex & 0x7f), cf. buf3d3[] */

void computeHitRadii(void) {
    const uint8 *base = (const uint8 *)g_world3dData;
    const uint8 *limit = base + WORLD3D_DATA_SIZE;
    int i;
    for (i = 0; i < 19; i++)
        s_aircraftModelRadius[i] =
            (int16)r3dmesh_boundRadius(base + shapeDataOffset(aircraftTypes[i].viewModelId), limit);
    /* Cover the appended photo/target models too: they sit at buf3d3[size3d3]
     * and [size3d3+1] (eg3dload.c), past the main region shapes, and ground
     * targets reference them by nameIndex. */
    for (i = 0; i <= (int)size3d3 + 1 && i < 100; i++)
        s_worldShapeRadius[i] = (int16)r3dmesh_boundRadius(base + buf3d3[i], limit);
}

/* Model bounding half-extent (model units) of an aircraft type / a ground
 * (tile-object) target's world shape, for hit-box sizing across weapons. */
int aircraftModelRadius(int spec) {
    if (spec < 0 || spec >= 19) spec = 0;
    return s_aircraftModelRadius[spec];
}
int groundModelRadius(int nameIndex) {
    int s = nameIndex & 0x7f;
    return (s >= 0 && s < 100) ? s_worldShapeRadius[s] : 0;
}

/* Ground target gun/impact radius in map units, Q8-scaled and difficulty-tightened. */
static int groundHitRadiusMap(int nameIndex) {
    int r = (groundModelRadius(nameIndex) * GUN_GROUND_HIT_SCALE_Q8) >> (8 + 4);
    if (r < GROUND_HIT_MIN_MAP) r = GROUND_HIT_MIN_MAP;
    r /= (g_missionStatus + 1);
    return r < 1 ? 1 : r;
}

/* Model-derived gun hit radius (fine world units) for a target, with the
 * original difficulty tightening kept (higher g_missionStatus -> smaller). */
static int aircraftGunRadiusFine(int spec) {
    int r;
    if (spec < 0 || spec >= 19) spec = 0;
    r = ((int)s_aircraftModelRadius[spec] * 2 * GUN_AIRCRAFT_HIT_SCALE_Q8) >> 8;
    r /= (g_missionStatus + 1);
    return r < 1 ? 1 : r;
}

/* Shortest wrapped delta on the 21-bit fine world torus (BULLET_FINE_MASK). */
static long fineWrapDelta(long d) {
    d &= BULLET_FINE_MASK;
    if (d & ((BULLET_FINE_MASK + 1) >> 1)) d -= (BULLET_FINE_MASK + 1);
    return d;
}

/* Squared closest-approach distance (fine units) between the round idx's swept
 * path this step (previous pos = pos-vel, to current pos) and target objIdx's
 * center. X/Y wrap on the world torus; alt does not. */
static long roundToTargetDist2(int idx, int objIdx) {
    long ax = fineWrapDelta((long)bulletTracks[idx].posX - ((long)g_simObjects[objIdx].posX << 5));
    long ay = fineWrapDelta((long)bulletTracks[idx].posY - ((long)g_simObjects[objIdx].posY << 5));
    long az = (long)bulletTracks[idx].alt - (long)g_simObjects[objIdx].alt;
    /* segment vector = the round's per-step travel (= velocity) */
    long vx = bulletTracks[idx].velX, vy = bulletTracks[idx].velY, vz = bulletTracks[idx].velZ;
    long seg2 = vx * vx + vy * vy + vz * vz;
    long cx, cy, cz;
    if (seg2 == 0) {
        cx = ax; cy = ay; cz = az;
    } else {
        /* project the previous endpoint (a - v) onto the segment, clamped */
        long dot = -((ax - vx) * vx + (ay - vy) * vy + (az - vz) * vz);
        long t256 = dot <= 0 ? 0 : (dot >= seg2 ? 256 : (dot * 256 / seg2));
        cx = (ax - vx) + vx * t256 / 256;
        cy = (ay - vy) + vy * t256 / 256;
        cz = (az - vz) + vz * t256 / 256;
    }
    return cx * cx + cy * cy + cz * cz;
}

/* Cannon tracers + explosion sparks as real world-space 3D line geometry
 * (drawWorldLine): submitted into the scene BEFORE r3d_endScene so the software
 * depth sort occludes them and the GL backend z-tests + fogs them. Kept separate
 * from drawHudWorldOverlay (which does the 2D HUD symbology) so the effects join
 * the 3D pass; game-logic order is tracer hit-detect then explosion. */
void drawWorldEffects(void) {
    int hitFlag, tmp, idx, radius, objIdx, pointY, pointX, dist, wpEntry, prevX, gunRadius;
    /* Rounds advance and the burst timer ticks per SIM step; this runs per
     * render frame, so the game-logic half (hit tests, expiry, timer) fires only
     * on frames that consumed a step — the drawing half runs every frame. */
    int stepped = g_simStepsThisFrame > 0;
    int16 bx, by;

    gunRadius = 0x200 / isqrt(g_frameRateScaling * 4 + 8);

    for (idx = 0; idx < g_bulletTrackCount + 4; idx++) {
        long ax, ay, az, ex, ey, ez;
        if (bulletTracks[idx].posX == 0) continue;

        /* Render-interpolated position: rounds fly straight at constant speed,
         * so pos + vel*alpha is exact between sim steps (no snapshots needed). */
        ax = (bulletTracks[idx].posX + (((int32)bulletTracks[idx].velX * g_renderAlphaQ12) >> 12)) & BULLET_FINE_MASK;
        ay = (bulletTracks[idx].posY + (((int32)bulletTracks[idx].velY * g_renderAlphaQ12) >> 12)) & BULLET_FINE_MASK;
        az = bulletTracks[idx].alt + (((int32)bulletTracks[idx].velZ * g_renderAlphaQ12) >> 12);
        ex = (ax + (bulletTracks[idx].velX >> 1)) & BULLET_FINE_MASK;
        ey = (ay + (bulletTracks[idx].velY >> 1)) & BULLET_FINE_MASK;
        ez = az + (bulletTracks[idx].velZ >> 1);

        projectWorldToHudFine(ax, ay, (int)az);
        prevX = vtxScratch.vproj.x.lo;
        projectWorldToHudFine(ex, ey, (int)ez);
        if (vtxScratch.vproj.x.lo == -1 || prevX == -1) continue;

        /* The projectWorldToHudFine pair above gates on-screen visibility (as the
         * original did); the tracer itself is a real world-space 3D segment
         * (round -> half a velocity-step ahead) so it perspective-projects,
         * occludes and hazes with the scene instead of overlaying a flat line. */
        drawWorldLine(ax, ay, (int)az, ex, ey, (int)ez,
                      idx < g_bulletTrackCount ? 0x0d : 0x0c);

        if (!stepped) continue;

        hitFlag = 0;
        bx = (int16)(bulletTracks[idx].posX >> 5);
        by = (int16)(bulletTracks[idx].posY >> 5);

        if (idx < g_bulletTrackCount) {
            for (objIdx = 0; objIdx < g_groundUnitCount; objIdx++) {
                if ((g_simObjects[objIdx].flags.b[0] & 0x22) == 2) {

                    dist = (abs((int16)(bulletTracks[idx].alt - g_simObjects[objIdx].alt)) >> 5) +
                           abs((int16)(bx - g_simObjects[objIdx].posX)) +
                           abs((int16)(by - g_simObjects[objIdx].posY));
                    dist = abs(dist);

                    /* Broad phase (cheap, and bounds the squared math below): only
                     * targets within the old generous radius are worth the precise
                     * swept-path test against the model-sized box. */
                    if (dist < gunRadius / (g_missionStatus + 1)) {
                        int rFine = aircraftGunRadiusFine(g_simObjects[objIdx].spec);
                        long d2 = roundToTargetDist2(idx, objIdx);
                        long r2 = (long)rFine * rFine;

                        if (d2 < r2) {

                            hitFlag = 1;
                            g_simObjects[objIdx].flags.b[0] |= 0x10;
                            g_hitEffectTimer = 1;

                            if (d2 * 4 < r2) {
                                destroyAircraft(objIdx);
                                strcat(strBuf, " destroyed by gunfire");
                                hudMessage(strBuf);
                                g_hitEffectTimer = 8;
                                bulletTracks[idx].posX = 0;
                            }
                        }
                    }
                }
            }
        } else {
            dist = (abs((int16)(bulletTracks[idx].alt - g_viewZ)) >> 5) + abs((int16)(bx - g_viewX_)) + abs((int16)(by - g_viewY_));
            dist = abs(dist);
            if (dist < 0x20) {
                hitFlag = 1;
                hudMessage("Hit by gunfire");
                if (0x20 / (4 - g_missionStatus) > dist) {
                    bombTarget();
                }
            }
        }

        if (hitFlag) {
            g_hitMapX = bx;
            g_hitMapY = by;
            g_hitAlt = (int16)bulletTracks[idx].alt;
            g_hitEffectTimer = -1;
        }

        if (bulletTracks[idx].alt < 0) {
            if (g_hitEffectTimer <= 0) {
                g_hitMapX = bx;
                g_hitMapY = by;
                g_hitAlt = (int16)bulletTracks[idx].alt;
                g_hitEffectTimer = -1;
            }
            bulletTracks[idx].posX = 0;

            wpEntry = findWaypointEntry(g_hitMapX, g_hitMapY);
            if (wpEntry != -1 && !(g_planeTable.planes[wpEntry].flags & 0x80)) {
                pointX = (int16)(g_nearestTileObj->x >> 5);
                pointY = 0x8000 - (int16)(g_nearestTileObj->y >> 5);

                if (rangeApprox(g_hitMapX - pointX, g_hitMapY - pointY) <
                        groundHitRadiusMap(g_planeTable.planes[wpEntry].nameIndex) &&
                    (g_planeTable.planes[wpEntry].nameIndex & 0x7f) != *(uint8 *)g_landTargetId) {
                    destroyGroundTarget(wpEntry);
                    strcat(strBuf, " destroyed by gunfire");
                    hudMessage(strBuf);
                    g_hitEffectTimer = 8;
                    g_hitAlt = 0;
                }
            }
        }
    }

    if (g_hitEffectTimer != 0) {
        /* Explosion burst as world-space 3D sparks radiating from the hit point:
         * each is a real line (drawWorldLine) so the star has perspective, occludes
         * and hazes — not a flat screen-space starburst. The projectWorldToHud call
         * only gates on-screen visibility (the sparks are re-randomised every frame
         * the timer is active, giving the flicker). radius is a WORLD radius (fine
         * map units), so the burst shrinks with distance instead of a screen-px fan. */
        projectWorldToHud(g_hitMapX, g_hitMapY, g_hitAlt);
        if (vtxScratch.vproj.x.lo != -1) {
            long hx = (long)(uint16)g_hitMapX << 5;
            long hy = (long)(uint16)g_hitMapY << 5;
            radius = EXPLOSION_WORLD_RADIUS;
            for (idx = 0; idx < 8; idx++) {
                int color = randomRange(4) + COLOR_LIGHTRED;
                long ex, ey, ez;
                if (g_hitAlt > 0) {
                    /* airburst: scatter in a world-space sphere around the hit */
                    ex = hx + randomRange(radius << 1) - radius;
                    ey = hy + randomRange(radius << 1) - radius;
                    ez = g_hitAlt + randomRange(radius << 1) - radius;
                } else {
                    /* ground burst: fan horizontally and plume upward */
                    tmp = randomRange(0x8000) - 0x4000;
                    dist = randomRange(radius);
                    ex = hx + sinMul(tmp, dist);
                    ey = hy - cosMul(tmp, dist);
                    ez = g_hitAlt + randomRange(radius);
                }
                drawWorldLine(hx, hy, g_hitAlt, ex, ey, (int)ez, color);
            }
        }
        if (stepped) g_hitEffectTimer -= signOf(g_hitEffectTimer);
    } else {
        g_lockedTargetKilled = 0;
    }
}

void drawHudWorldOverlay(void) {
    int p, lockFlag, r, wpEntry, tmp, t, missileSpecD, loftDist, e, missileSpec, marker, idx, g, radius, objIdx, pointY, pointX, dist, wpIdx, prevX, compat, prevY;

    g_prevKillMarker = g_targetInHudFlag;
    g_targetInHudFlag = 0;

    for (idx = 0; idx < 12; idx++) {
        if (g_projectiles[idx].ttl != 0) {
            projectWorldToHudFine(g_projInterpX[idx], g_projInterpY[idx], g_projectiles[idx].alt);
            if (vtxScratch.vproj.x.lo != -1) {
                setDrawColor(idx < 8 ? 0x0e : 0x0a);
                drawTargetBoxF(g_hudProjXf, g_hudProjYf, 6, 0);
            }
        }
    }

    if (g_hudVisible == 0) return;

    if (g_unusedHudFlag != 0) {
        g_unusedHudFlag = 0;
    }

    loadColorPalette(g_nightMode != 0 ? 2 : g_nightMode);
    setDrawColor(COLOR_WHITE);;
    drawFullscreenLine(319, 199, 319, 199);
    g_lockToneFlag = 0;

    if (g_currentWeaponType == 2) {
        if (g_viewMode == VIEW_COCKPIT) {
            if (g_groundTargetLock >= 0) {

                projectWorldToHudFine((int32)g_planeTable.planes[g_groundTargetLock].mapX << 5,
                                      (int32)g_planeTable.planes[g_groundTargetLock].mapY << 5, 0);

                missileSpec = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;

                if (missileSpec == 28 && computeMapTargetRange(g_groundTargetLock) < (g_viewZ >> 5) * 5 && g_projDepth < 0) {
                    g_lockToneFlag = 1;
                }

                if (vtxScratch.vproj.x.lo != -1) {

                    setDrawColor(g_nightMode != 0 ? COLOR_DARKGRAY : COLOR_BLACK);
                    lockFlag = 0;

                    compat = missileTargetCompat(missleSpec[missileSpecIndex].weaponIdx, g_groundTargetLock) != 0 ? 4 : 0;

                    if (compat != 0 && (missileSpec != 4 || g_planeTable.planes[g_groundTargetLock].active != 0)) {
                        if (missleSpec[missileSpecIndex].ammo != 0) {
                            setDrawColor(COLOR_WHITE);;
                            if ((rangeApprox(vtxScratch.vproj.x.lo - 160, vtxScratch.vproj.y.lo - 56) < 48 || g_lockToneFlag != 0) &&
                                -g_projDepth / 7 < sams[missileSpec].lockRange &&
                                sams[missileSpec].weaponClass != 7) {
                                if (sams[missileSpec].weaponClass != 28 || g_lockToneFlag != 0) {
                                    g_lockToneFlag = 1;
                                    lockFlag = 1;
                                    if (sams[missileSpec].lockRange > (-g_projDepth >> 1 >> 1)) {
                                        setDrawColor(COLOR_LIGHTRED);
                                    }
                                }
                            } else {
                                g_lockToneFlag = 0;
                            }
                        }
                    } else {
                        if (missileSpec != -1) {
                            setDrawColor(g_nightMode != 0 ? COLOR_DARKGRAY : COLOR_BLACK);
                        }
                        g_lockToneFlag = 0;
                    }

                    drawTargetBoxF(g_hudProjXf, g_hudProjYf, compat != 0 ? compat + 5 : 9, lockFlag);
                }
            }
        }
    }

    if (g_scopeSweepTimer > 0 && g_threatLabelTarget >= 0) {
        projectWorldToHudFine((int32)g_planeTable.planes[g_threatLabelTarget].mapX << 5,
                              (int32)g_planeTable.planes[g_threatLabelTarget].mapY << 5, 0);
        drawTargetLabel(g_targetNameTable[((int16 *)&g_planeTable)[g_threatLabelTarget * 8]], g_scopeArcColor, g_frameRateScaling - g_scopeSweepTimer);
    }

    g_playerPlaneFlags &= ~0x200;
    g_pageFront[1] = 4;

    if (g_activePanelMode == 0x13) {
        if (g_currentWeaponType == 2 || g_currentWeaponType == 0) {
            if (g_groundTargetLock != -1) {

                wpIdx = g_groundTargetLock & 0x7f;

                /* Ground targets only carry coarse map coords; scale to the fine
                 * (mapX<<5) space drawTargetView now differences against g_ViewX. */
                drawTargetView(getTargetSymbol(wpIdx),
                               (int32)g_planeTable.planes[wpIdx].mapX << 5,
                               (int32)g_planeTable.planes[wpIdx].mapY << 5,
                               0, 0, 0, 0, 1, -1);
                drawMissileLock();
                buildRangeString(computeMapTargetRange(wpIdx));
                drawStringActivePage(strBuf, 244, 170, 0x0f);

                strcpy(strBuf, g_targetNameTable[g_planeTable.planes[wpIdx].nameIndex & 0x7f]);
                drawStringActivePage(strBuf, -((int16)strlen(strBuf) * 2 - 268), 130, 0x0f);

                if ((int16)strlen(g_targetNameTable[((int16 *)&g_planeTable)[wpIdx * 8]]) != 0) {
                    strcpy(strBuf,
                           strlen(g_targetNameTable[g_planeTable.planes[wpIdx].nameIndex & 0x7f]) != 0 ? " at " : "");
                    strcat(strBuf, g_targetNameTable[((int16 *)&g_planeTable)[wpIdx * 8]]);
                    drawStringActivePage(strBuf, -((int16)strlen(strBuf) * 2 - 268), 136, 0x0f);
                }

                if (g_currentWeaponType == 0) {
                    projectWorldToHudFine((int32)g_planeTable.planes[g_groundTargetLock].mapX << 5,
                                          (int32)g_planeTable.planes[g_groundTargetLock].mapY << 5, 0);
                    setDrawColor(COLOR_WHITE);;
                    drawTargetBoxF(g_hudProjXf, g_hudProjYf, 8, 0);
                } else if (g_targetSlots[0].planeIndex == g_groundTargetLock) {
                    drawStringActivePage(egPrimaryTarget, 0xec, 0x8e, 0x0f);
                } else if (g_targetSlots[1].planeIndex == g_groundTargetLock) {
                    drawStringActivePage("Secondary Target", 236, 142, 0x0f);
                } else if (!(frameTick & 1) &&
                           ((g_difficultyTier < 2 && (g_shapeTargetCategory[g_planeTable.planes[wpIdx].nameIndex & 0x7f] & 0xc0) != 0) ||
                            (g_planeTable.planes[wpIdx].flags & 0x500) != 0 ||
                            (g_mapCellFlags[((uint16)g_planeTable.planes[wpIdx].mapX >> 11) +
                                            ((uint16)g_planeTable.planes[wpIdx].mapY >> 11) * 16] &
                             1) != 0)) {
                    drawStringActivePage("No Target", 252, 142, 0x0f);
                }

                if (abs((int16)((g_ourHead + g_viewHeadingOffset) - g_targetBearing)) > 0x2000) {
                    g_groundTargetLock = -1;
                }
            }
        }
    }

    g_axisInput1 = readAxisInput(1);

    if (g_currentWeaponType == 1) {
        if (g_viewMode == VIEW_COCKPIT) {
            if (!(g_airTargetLock & 0x80)) {

                projectWorldToHudFine(g_simObjects[g_airTargetLock].worldX,
                                      g_simObjects[g_airTargetLock].worldY,
                                      g_simObjects[g_airTargetLock].alt);

                if (vtxScratch.vproj.x.lo != -1) {

                    setDrawColor(g_nightMode != 0 ? COLOR_DARKGRAY : COLOR_BLACK);
                    lockFlag = 0;

                    missileSpec = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;

                    if (missleSpec[missileSpecIndex].ammo != 0 && sams[missileSpec].weaponClass == 7) {
                        setDrawColor(COLOR_WHITE);;
                        if (rangeApprox(vtxScratch.vproj.x.lo - 160, vtxScratch.vproj.y.lo - 56) < 48) {
                            if (-g_projDepth >> 3 < sams[missileSpec].lockRange) {
                                g_lockToneFlag = 1;
                                lockFlag = 1;
                                if (-g_projDepth >> 1 >> 1 < sams[missileSpec].lockRange) {
                                    setDrawColor(COLOR_LIGHTRED);
                                }
                            }
                        }
                    }
                    drawTargetBoxF(g_hudProjXf, g_hudProjYf, 9, lockFlag);
                }
            }
        }
    }

    if (g_activePanelMode == 0x13 && g_currentWeaponType == 1 && g_airTargetLock != -1) {
        wpIdx = g_airTargetLock & 0x7f;

        /* Fine (integrated) world position, not the coarse posX/posY seed, so the
         * tracked model doesn't jitter on the ÷32 grid as the view interpolates. */
        drawTargetView(aircraftTypes[g_simObjects[wpIdx].spec].viewModelId,
                       g_simObjects[wpIdx].worldX,
                       g_simObjects[wpIdx].worldY,
                       g_simObjects[wpIdx].alt,
                       g_simObjects[wpIdx].heading.w,
                       g_simObjects[wpIdx].pitch,
                       g_simObjects[wpIdx].bank.w,
                       1, 1);
        drawMissileLock();
        buildRangeString(rangeApprox(g_viewX_ - g_simObjects[wpIdx].posX,
                                     g_viewY_ - g_simObjects[wpIdx].posY));
        drawStringActivePage(strBuf, 244, 170, 0x0f);

        idx = g_simObjects[wpIdx].spec;
        strcpy(strBuf, aircraftTypes[idx].name);
        strcat(strBuf, aircraftTypes[idx].altName);
        drawStringActivePage(strBuf, 248, 134, 0x0f);

        if (aircraftTypes[idx].modelId == -1 && !(frameTick & 1)) {
            drawStringActivePage("No Target", 252, 140, 0x0f);
        }

        if (g_detailLevel != 0 && (frameTick & 1)) {
            g_aamLeadDist = (int16)(((uint32)(uint16)(0x8000 - g_simObjects[wpIdx].pitch) *
                                     (int32)g_simObjects[wpIdx].speed) >>
                                    15);
            g_aamLeadDist -= abs(sinMul(g_simObjects[wpIdx].bank.w, g_aamLeadDist)) >> 1;
        }
    }

    g_pageFront[1] = 2;

    if (g_scopeSweepTimer > 0 && g_threatLabelTarget < 0) {
        idx = -1 - g_threatLabelTarget;
        projectWorldToHudFine(g_simObjects[idx].worldX,
                              g_simObjects[idx].worldY,
                              g_simObjects[idx].alt);
        drawTargetLabel(aircraftTypes[g_simObjects[idx].spec].name,
                        g_scopeArcColor, g_frameRateScaling - g_scopeSweepTimer);
    }

    if (g_currentWeaponType == 2 && g_viewMode == VIEW_COCKPIT) {
        missileSpecD = missiles[missleSpec[missileSpecIndex].weaponIdx].specIndex;

        if (missileSpecD == 30 && abs((int16)g_ourRoll) < 0x2000) {
            tmp = computeLoftAngle();
            loftDist = cosMul(tmp, g_altitude) / (sinMul(-tmp, 0x20) + 1);
            pointX = sinMul(g_ourHead, loftDist) + g_viewX_;
            pointY = g_viewY_ - cosMul(g_ourHead, loftDist);
            projectWorldToHud(pointX, pointY, 0);
            if (vtxScratch.vproj.x.lo == -1) {
                vtxScratch.vproj.x.lo = (sinMul(g_ourRoll, 96 - g_flightPathMarkerY) << 2) / 3 + 160;
                vtxScratch.vproj.y.lo = 96;
            } else {
                setDrawColor(COLOR_LIGHTRED);
                drawTargetBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, 5, 1);
            }
            setDrawColor(COLOR_WHITE);;
            drawHudViewLine(160, g_flightPathMarkerY, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo);
        }

        if ((missileSpecD == 30 || missileSpecD == 29) && g_groundTargetLock >= 0) {
            projectWorldToHud(g_planeTable.planes[g_groundTargetLock].mapX + sinMul(g_ourHead, 0x80),
                              g_planeTable.planes[g_groundTargetLock].mapY - cosMul(g_ourHead, 0x80),
                              g_viewZ);

            if (vtxScratch.vproj.x.lo != -1) {
                if (missileSpecD == 30) {
                    g_projDepth = clampRange(
                        rangeApprox(pointX - g_planeTable.planes[g_groundTargetLock].mapX,
                                    pointY - g_planeTable.planes[g_groundTargetLock].mapY) >>
                            3,
                        0x0000, 0x0040);
                } else {
                    g_projDepth = clampRange(computeMapTargetRange(g_groundTargetLock) >> 3, 0x0000, 0x0040);
                }
                setDrawColor(COLOR_LIGHTRED);
                drawViewportLine(159 - g_projDepth, 33, 159 - g_projDepth, 30);
                drawViewportLine(g_projDepth + 160, 33, g_projDepth + 160, 30);
                drawViewportLine(159 - g_projDepth, 30, g_projDepth + 160, 30);
                setDrawColor(COLOR_WHITE);;
                drawHudViewLine(vtxScratch.vproj.x.lo - 4, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo - 4);
                drawHudViewLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo - 4, vtxScratch.vproj.x.lo + 4, vtxScratch.vproj.y.lo);
                drawHudViewLine(vtxScratch.vproj.x.lo + 4, vtxScratch.vproj.y.lo, vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo + 4);
                drawHudViewLine(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo + 4, vtxScratch.vproj.x.lo - 4, vtxScratch.vproj.y.lo);
            }
        }
    }

    if (g_hitEffectTimer != 0 && g_activePanelMode == 0x13 && g_lockedTargetKilled != 0 && g_targetInHudFlag != 0) {
        blitSprite(252, 140, (abs(g_hitEffectTimer) - 8) * -32, 0x3f, 32, 32, 0);
    }

    if (g_activePanelMode == 0x13 && g_prevKillMarker != 0 && g_targetInHudFlag == 0) {
        fillPanelBox(3, 3);
    }
}

/* ---- merged from egwaypt.c ---- */
void drawTargetBox(int16 centerX, int16 centerY, int16 size, int16 mode) {
    int16 halfHeight, left, top, right, bottom;

    if (g_hudVisible == 0) {
        return;
    }
    if (g_halfScaleRender != 0) {
        size >>= 1;
    }
    halfHeight = size - (size >> 2);
    right = centerX + size;
    left = centerX - size;
    bottom = centerY + halfHeight;
    top = centerY - halfHeight;
    if (mode == 0) {
        drawHudViewLine(left, top, left, bottom);
        drawHudViewLine(left, bottom, right, bottom);
        drawHudViewLine(right, bottom, right, top);
        drawHudViewLine(right, top, left, top);
    } else {
        drawHudViewLine(centerX, top, right, centerY - (halfHeight >> 1));
        drawHudViewLine(right, centerY - (halfHeight >> 1), right, centerY + (halfHeight >> 1));
        drawHudViewLine(right, (halfHeight >> 1) + centerY, centerX, bottom);
        drawHudViewLine(centerX, bottom, left, (halfHeight >> 1) + centerY);
        drawHudViewLine(left, centerY + (halfHeight >> 1), left, centerY - (halfHeight >> 1));
        drawHudViewLine(left, centerY - (halfHeight >> 1), centerX, top);
    }
}

/* Sub-pixel target box for the GL native-res overlay: identical geometry to
 * drawTargetBox but the corners are offset from the projector's fractional 320-space
 * centre (g_hudProjXf/Yf), so the box glides instead of snapping to the 320x200 grid
 * when the object is close. Box dimensions stay whole-pixel — only the centre is
 * fractional. Without a vector overlay (software backend) it falls back to the
 * integer drawTargetBox on the exact projected pixel, the faithful 320x200 raster. */
void drawTargetBoxF(float centerX, float centerY, int size, int mode) {
    int halfHeight, sz;
    float left, top, right, bottom, hq;

    if (g_hudVisible == 0) {
        return;
    }
    if (!r2d_vectorActive()) {
        drawTargetBox(vtxScratch.vproj.x.lo, vtxScratch.vproj.y.lo, size, mode);
        return;
    }
    sz = size;
    if (g_halfScaleRender != 0) {
        sz >>= 1;
    }
    halfHeight = sz - (sz >> 2);
    right = centerX + sz;
    left = centerX - sz;
    bottom = centerY + halfHeight;
    top = centerY - halfHeight;
    if (mode == 0) {
        drawHudViewLineF(left, top, left, bottom);
        drawHudViewLineF(left, bottom, right, bottom);
        drawHudViewLineF(right, bottom, right, top);
        drawHudViewLineF(right, top, left, top);
    } else {
        hq = (float)(halfHeight >> 1);
        drawHudViewLineF(centerX, top, right, centerY - hq);
        drawHudViewLineF(right, centerY - hq, right, centerY + hq);
        drawHudViewLineF(right, centerY + hq, centerX, bottom);
        drawHudViewLineF(centerX, bottom, left, centerY + hq);
        drawHudViewLineF(left, centerY + hq, left, centerY - hq);
        drawHudViewLineF(left, centerY - hq, centerX, top);
    }
}

// ==== seg000:0xC2F8 ====
void drawMissileLock(void) {
    int16 markX, markY;
    if (g_lockToneFlag != 0 && g_hudVisible != 0) {
        drawStringActivePage("Missile Lock", 244, 150, 14);
        setDrawColor(COLOR_YELLOW);
        markX = 268;
        markY = 156;
        drawFullscreenLine(258, 156, 278, 156);
        drawFullscreenLine(markX, markY - 8, markX, markY + 8);
    }
}

// ==== seg000:0xc371 ====
void drawTargetLabel(const char *text, int16 color, int16 size) {
    if (vtxScratch.vproj.x.lo == -1) {
        return;
    }
    setDrawColor(color);
    if (size < vtxScratch.vproj.x.lo && 319 - size > vtxScratch.vproj.x.lo &&
        size < vtxScratch.vproj.y.lo && 88 - size > vtxScratch.vproj.y.lo) {
        drawTargetBoxF(g_hudProjXf, g_hudProjYf, size, 1);
    }
    if (vtxScratch.vproj.x.lo > 20 && vtxScratch.vproj.x.lo < 280 &&
        vtxScratch.vproj.y.lo > 0 && vtxScratch.vproj.y.lo < 82) {
        drawStringActivePage(text, vtxScratch.vproj.x.lo - (int16)strlen(text) * 2, vtxScratch.vproj.y.lo + 5, g_scopeArcColor);
    }
}

// ==== seg000:0xc40b ====
void buildRangeString(int16 rangeRaw) {
    int16 p, a, b, c, d;

    strcpy(strBuf, "Range ");
    strcat(strBuf, itoa(rangeRaw >> 6, g_itoaScratch, 10));
    strcat(strBuf, ".");
    strcat(strBuf, itoa((rangeRaw & 0x3f) * 2 / 13, g_itoaScratch, 10));
    strcat(strBuf, " km");
}
