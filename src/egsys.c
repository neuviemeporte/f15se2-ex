/*
 * egsys.c - system services for egame (game loop, DAC palette), isolated here
 * so a fork can replace just this file.
 */

#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "inttype.h"
#include "gfx.h"

/* per-frame work reconstructed in their own TUs (egflight/egtacmap/egframe),
 * not surfaced in a header; declared here for the game loop below. */
void renderFrame(void);
void renderHudFrame(int unused);
void stepFlightModel(void);
void updateFrame(void);

/* ---- Fixed-timestep sim + interpolated rendering ----
 *
 * The original ran one sim step per rendered frame and an adaptive governor held
 * the sim cadence constant by *sleeping* the loop (waitFrameSync / g_frameSyncWait),
 * which also pinned the frame rate to the sim rate (~15 fps).
 *
 * Here the sim advances at a fixed wall-clock rate while rendering runs at the
 * display refresh, interpolating the camera between the previous and current sim
 * state. Game speed is unchanged: the governor's invariant was always
 * stepRate == g_frameRateScaling (giving the mission clock 1 Hz), so we step at
 * exactly g_frameRateScaling steps/s. The governor's auto-rescale and the sleep
 * are retired (egframe.c/egkeys.c); g_frameRateScaling is pinned at 15 (max
 * precision). ALT+A "ACCEL" (egkeys.c) multiplies the wall-clock step rate by
 * g_slowMotionMode (see simStepNsNow), a clean 2x time compression at full
 * precision. */

#define NS_PER_SEC 1000000000ULL
#define SIM_OBJ_MAX 20
#define PROJ_MAX 12
#define OBJ_TELEPORT_GUARD 0x2000 /* world units/axis; a real step moves << this */

/* The authoritative sim state the camera is derived from each renderFrame(). We
 * snapshot prev/next and write an interpolated copy into the live globals just
 * for the render, then restore the authoritative "next" values for the next sim
 * step. Beyond the player's own coords (used by the forward/side/rear/chase
 * views), this includes the player *map* coords (g_viewX_/g_viewY_, used for the
 * player->target bearing in the director/target views) and the crash-cam eye
 * (g_crashCam*, the 0x8c view) so those tracking views interpolate coherently
 * too. Moving objects are interpolated separately (object snapshot helpers
 * below). */
typedef struct {
    int32 viewX, viewY, viewZ;
    int32 head, pitch, roll;
    int32 mapX, mapY; /* g_viewX_ / g_viewY_ */
    int32 crashX, crashY, crashZ;
    int32 wreckX, wreckY, wreckAlt; /* downed-aircraft wreck/parachute */
} CamSnapshot;

static int32 iabs32(int32 v) {
    return v < 0 ? -v : v;
}

static void camCapture(CamSnapshot *s) {
    s->viewX = g_ViewX;
    s->viewY = g_ViewY;
    s->viewZ = g_viewZ;
    s->head = g_ourHead;
    s->pitch = g_ourPitch;
    s->roll = g_ourRoll;
    s->mapX = g_viewX_;
    s->mapY = g_viewY_;
    s->crashX = g_crashCamX;
    s->crashY = g_crashCamY;
    s->crashZ = g_crashCamZ;
    s->wreckX = g_wreckX;
    s->wreckY = g_wreckY;
    s->wreckAlt = g_wreckAlt;
}

static void camRestore(const CamSnapshot *s) {
    g_ViewX = s->viewX;
    g_ViewY = s->viewY;
    g_viewZ = (int16)s->viewZ;
    g_ourHead = (int16)s->head;
    g_ourPitch = s->pitch;
    g_ourRoll = (int16)s->roll;
    g_viewX_ = (int16)s->mapX;
    g_viewY_ = (int16)s->mapY;
    g_crashCamX = (int16)s->crashX;
    g_crashCamY = (int16)s->crashY;
    g_crashCamZ = (int16)s->crashZ;
    g_wreckX = (int16)s->wreckX;
    g_wreckY = (int16)s->wreckY;
    g_wreckAlt = (int16)s->wreckAlt;
}

static int32 lerpLinear(int32 a, int32 b, int64 num, int64 den) {
    return a + (int32)(((int64)(b - a) * num) / den);
}

/* Shortest-arc interpolation in 16-bit angle space (heading wraps; pitch/roll
 * don't in practice, where the int16 delta degenerates to a plain difference). */
static int32 lerpAngle(int32 a, int32 b, int64 num, int64 den) {
    int16 d = (int16)(b - a);
    /* A single sim step never rotates the airframe ~90deg from stick input; a
     * delta that large is the discontinuous 0x8000 heading/roll flip
     * computeAttitudeAngles() emits as pitch crosses +/-90deg (gimbal). Tweening
     * across it would sweep the view 180deg for one render frame, so snap to the
     * new pose instead. */
    if (d >= 0x4000 || d <= -0x4000)
        return b;
    return a + (int32)(((int64)d * num) / den);
}

static int angleSnaps(int32 a, int32 b) {
    int16 d = (int16)(b - a);
    return d >= 0x4000 || d <= -0x4000;
}

/* Interpolate a heading/pitch/roll pose. The gimbal flip changes the
 * REPRESENTATION of all three components at once (head/roll jump 0x8000 while
 * pitch reflects with a small delta); snapping only the offending component
 * while the others keep tweening mixes two representations into a visibly
 * wrong pose (the "90deg flip-flop" when pulling through the vertical), so a
 * flip in any component snaps the whole triple to the new pose. */
static void lerpPose(int32 h0, int32 p0, int32 r0, int32 h1, int32 p1, int32 r1,
                     int64 num, int64 den, int32 *h, int32 *p, int32 *r) {
    if (angleSnaps(h0, h1) || angleSnaps(p0, p1) || angleSnaps(r0, r1)) {
        *h = h1;
        *p = p1;
        *r = r1;
    } else {
        *h = lerpAngle(h0, h1, num, den);
        *p = lerpAngle(p0, p1, num, den);
        *r = lerpAngle(r0, r1, num, den);
    }
}

static void camApplyInterp(const CamSnapshot *p, const CamSnapshot *n, int64 num, int64 den) {
    int32 poseH, poseP, poseR;
    g_ViewX = lerpLinear(p->viewX, n->viewX, num, den);
    g_ViewY = lerpLinear(p->viewY, n->viewY, num, den);
    g_viewZ = (int16)lerpLinear(p->viewZ, n->viewZ, num, den);
    lerpPose(p->head, p->pitch, p->roll, n->head, n->pitch, n->roll,
             num, den, &poseH, &poseP, &poseR);
    g_ourHead = (int16)poseH;
    g_ourPitch = poseP;
    g_ourRoll = (int16)poseR;
    g_viewX_ = (int16)lerpLinear(p->mapX, n->mapX, num, den);
    g_viewY_ = (int16)lerpLinear(p->mapY, n->mapY, num, den);
    g_crashCamX = (int16)lerpLinear(p->crashX, n->crashX, num, den);
    g_crashCamY = (int16)lerpLinear(p->crashY, n->crashY, num, den);
    g_crashCamZ = (int16)lerpLinear(p->crashZ, n->crashZ, num, den);
    /* Wreck/parachute: alt falls per sim step. Interpolate only while present in
     * both frames and not jumped to a fresh kill (else leave at the live next). */
    if (p->wreckAlt > 0 && n->wreckAlt > 0 &&
        iabs32(n->wreckX - p->wreckX) < OBJ_TELEPORT_GUARD &&
        iabs32(n->wreckY - p->wreckY) < OBJ_TELEPORT_GUARD) {
        g_wreckX = (int16)lerpLinear(p->wreckX, n->wreckX, num, den);
        g_wreckY = (int16)lerpLinear(p->wreckY, n->wreckY, num, den);
        g_wreckAlt = (int16)lerpLinear(p->wreckAlt, n->wreckAlt, num, den);
    }
}

static uint64 simStepNsNow(void) {
    int scaling = g_frameRateScaling;
    int accel = g_slowMotionMode; /* 2 = ACCEL (ALT+A): step 2x faster in wall-clock */
    if (scaling < 1) scaling = 1;
    if (accel < 1) accel = 1;
    return NS_PER_SEC / ((uint64)scaling * (uint64)accel);
}

/* ---- Object interpolation (stage 2) ----
 * The moving 3D scene objects — enemy aircraft (g_simObjects[]) and in-flight
 * missiles/SAMs (g_projectiles[]) — are drawn from live globals by
 * updateTargetLock() during renderFrame(). Like the camera, snapshot prev/next
 * each sim step, write an interpolated copy into the live fields for the render,
 * then restore the authoritative "next". Identity gating avoids tweening across
 * a slot reuse / teleport (which would streak):
 *   - sim objects: only while alive (flags bit1) in both frames and the world
 *     position moved less than a step could plausibly carry it (a real step
 *     advances a few hundred world units; a reuse/respawn jumps map-scale).
 *   - projectiles: ttl decrements by exactly 1 per step in flight and a reused
 *     slot always passes through ttl==0, so interpolate iff next.ttl==prev.ttl-1.
 * posX/posY mirror worldX/worldY (>>5) so the HUD reticle (projectWorldToHud,
 * which reads posX/posY) and the 3D model (drawWorldObject, worldX/worldY) stay
 * consistent. */
typedef struct {
    int32 worldX, worldY;
    uint16 posX, posY;
    int16 alt, head, pitch, bank;
    uint8 alive;
} SimObjSnap;

typedef struct {
    int32 fineX, fineY, alt; /* fineX/fineY: authoritative mapX<<5-scale position */
    int16 head, pitch;       /* g_projectiles[].worldX/worldY hold the missile yaw/pitch */
    int16 ttl;
} ProjSnap;

static int simObjCount(void) {
    int n = g_groundUnitCount;
    if (n < 0) n = 0;
    if (n > SIM_OBJ_MAX) n = SIM_OBJ_MAX;
    return n;
}

static void objCapture(SimObjSnap *sim, ProjSnap *proj) {
    int i, n = simObjCount();
    for (i = 0; i < n; i++) {
        sim[i].worldX = g_simObjects[i].worldX;
        sim[i].worldY = g_simObjects[i].worldY;
        sim[i].posX = g_simObjects[i].posX;
        sim[i].posY = g_simObjects[i].posY;
        sim[i].alt = g_simObjects[i].alt;
        sim[i].head = g_simObjects[i].heading.w;
        sim[i].pitch = g_simObjects[i].pitch;
        sim[i].bank = g_simObjects[i].bank.w;
        sim[i].alive = (g_simObjects[i].flags.b[0] & 2) ? 1 : 0;
    }
    for (i = 0; i < PROJ_MAX; i++) {
        proj[i].fineX = g_projectiles[i].fineX;
        proj[i].fineY = g_projectiles[i].fineY;
        proj[i].alt = g_projectiles[i].alt;
        proj[i].head = g_projectiles[i].worldX;
        proj[i].pitch = g_projectiles[i].worldY;
        proj[i].ttl = g_projectiles[i].ttl;
    }
}

static void objApplyInterp(const SimObjSnap *sp, const SimObjSnap *sn,
                           const ProjSnap *pp, const ProjSnap *pn,
                           int64 num, int64 den) {
    int i, n = simObjCount();
    for (i = 0; i < n; i++) {
        int32 wx, wy, poseH, poseP, poseR;
        if (!sp[i].alive || !sn[i].alive)
            continue;
        if (iabs32(sn[i].worldX - sp[i].worldX) >= OBJ_TELEPORT_GUARD ||
            iabs32(sn[i].worldY - sp[i].worldY) >= OBJ_TELEPORT_GUARD)
            continue;
        wx = lerpLinear(sp[i].worldX, sn[i].worldX, num, den);
        wy = lerpLinear(sp[i].worldY, sn[i].worldY, num, den);
        g_simObjects[i].worldX = wx;
        g_simObjects[i].worldY = wy;
        g_simObjects[i].posX = (uint16)(wx >> 5);
        g_simObjects[i].posY = (uint16)(wy >> 5);
        g_simObjects[i].alt = (int16)lerpLinear(sp[i].alt, sn[i].alt, num, den);
        /* enemy AI flips its pose representation the same way as the player
         * (egthreat pitch>0x4000: head+=0x8000, bank+=0x8000, pitch reflected) */
        lerpPose(sp[i].head, sp[i].pitch, sp[i].bank, sn[i].head, sn[i].pitch, sn[i].bank,
                 num, den, &poseH, &poseP, &poseR);
        g_simObjects[i].heading.w = (int16)poseH;
        g_simObjects[i].pitch = (int16)poseP;
        g_simObjects[i].bank.w = (int16)poseR;
    }
    for (i = 0; i < PROJ_MAX; i++) {
        /* Default to the authoritative (next) position so a just-fired /
         * non-interpolated slot still has a valid fine value. */
        g_projInterpX[i] = pn[i].fineX;
        g_projInterpY[i] = pn[i].fineY;
        if (pp[i].ttl <= 0 || pn[i].ttl != pp[i].ttl - 1)
            continue;
        g_projectiles[i].fineX = lerpLinear(pp[i].fineX, pn[i].fineX, num, den);
        g_projectiles[i].fineY = lerpLinear(pp[i].fineY, pn[i].fineY, num, den);
        g_projectiles[i].mapX = (uint16)(g_projectiles[i].fineX >> 5);
        g_projectiles[i].mapY = (uint16)(g_projectiles[i].fineY >> 5);
        g_projInterpX[i] = g_projectiles[i].fineX;
        g_projInterpY[i] = g_projectiles[i].fineY;
        /* alt's low bit is the track-state flag (radar draws gray when clear),
         * not real altitude — interpolate the altitude but keep the authoritative
         * flag bit so "lost track" stays gray. */
        g_projectiles[i].alt = ((int16)lerpLinear(pp[i].alt, pn[i].alt, num, den) & ~1) | (pn[i].alt & 1);
        g_projectiles[i].worldX = (int16)lerpAngle(pp[i].head, pn[i].head, num, den);
        g_projectiles[i].worldY = (int16)lerpAngle(pp[i].pitch, pn[i].pitch, num, den);
    }
}

static void objRestore(const SimObjSnap *sn, const ProjSnap *pn) {
    int i, n = simObjCount();
    for (i = 0; i < n; i++) {
        g_simObjects[i].worldX = sn[i].worldX;
        g_simObjects[i].worldY = sn[i].worldY;
        g_simObjects[i].posX = sn[i].posX;
        g_simObjects[i].posY = sn[i].posY;
        g_simObjects[i].alt = sn[i].alt;
        g_simObjects[i].heading.w = sn[i].head;
        g_simObjects[i].pitch = sn[i].pitch;
        g_simObjects[i].bank.w = sn[i].bank;
    }
    for (i = 0; i < PROJ_MAX; i++) {
        g_projectiles[i].fineX = pn[i].fineX;
        g_projectiles[i].fineY = pn[i].fineY;
        g_projectiles[i].mapX = (uint16)(pn[i].fineX >> 5);
        g_projectiles[i].mapY = (uint16)(pn[i].fineY >> 5);
        g_projInterpX[i] = pn[i].fineX;
        g_projInterpY[i] = pn[i].fineY;
        g_projectiles[i].alt = (int16)pn[i].alt;
        g_projectiles[i].worldX = pn[i].head;
        g_projectiles[i].worldY = pn[i].pitch;
    }
}

/* gameMainLoop / runGameLoop. Each rendered frame composites the 3D world
 * (renderFrame), the tac map / 2D overlays (renderHudFrame) and the dynamic
 * gauges (unless a menu key overlay is up), then presents (gfx_dacAnimate, slot
 * 0x2c, vsync-paced). Between presents the sim is stepped fixed-rate and the
 * camera interpolated; under load the sim catches up and frames are dropped
 * rather than the game slowing. */
void gameMainLoop(void) {
    CamSnapshot camPrev, camNext;
    SimObjSnap simPrev[SIM_OBJ_MAX], simNext[SIM_OBJ_MAX];
    ProjSnap projPrev[PROJ_MAX], projNext[PROJ_MAX];
    uint64 simStepNs = simStepNsNow();
    uint64 accumNs = 0;
    uint64 prevNs = timerNowNs();
    int steps;

    camCapture(&camNext);
    camPrev = camNext; /* first frame renders the spawn state, no interpolation */
    objCapture(simNext, projNext);
    objCapture(simPrev, projPrev);

    do {
        uint64 nowNs = timerNowNs();
        accumNs += nowNs - prevNs;
        prevNs = nowNs;
        timerPump(); /* advance the 60 Hz tick counters + per-tick / colour-cycle hook */

        steps = 0;
        while (accumNs >= simStepNs) {
            accumNs -= simStepNs;
            camPrev = camNext;
            objCapture(simPrev, projPrev); /* prev = live = previous step's result */
            stepFlightModel();
            updateFrame();
            camCapture(&camNext);
            objCapture(simNext, projNext); /* next = live = this step's result */
            if (g_initPhase < 2) {
                /* don't interpolate across mission init's state jumps */
                camPrev = camNext;
                objCapture(simPrev, projPrev); /* live == next here, so prev = next */
            }
            simStepNs = simStepNsNow(); /* slow-motion changes the sim rate */
            if (g_missionEndedFlag[0] != 0) {
                accumNs = 0;
                break;
            }
            if (++steps >= 4) {
                accumNs = 0; /* far behind: drop the backlog, don't spiral */
                break;
            }
        }
        g_simStepsThisFrame = steps;                                   /* paces render-rate animations (g_spinAngle) to the sim */
        g_renderAlphaQ12 = (int)(((uint64)accumNs << 12) / simStepNs); /* for renderFrame's own interp (0x84 ring) */

        /* All views interpolate: camera, the player map coords + crash-cam eye,
         * and the moving objects. The director/target/crash views track an
         * object, but that object (and the player coords they bear against) are
         * now interpolated too, so the whole view stays coherent and smooth. */
        camApplyInterp(&camPrev, &camNext, (int64)accumNs, (int64)simStepNs);
        objApplyInterp(simPrev, simNext, projPrev, projNext, (int64)accumNs, (int64)simStepNs);
        renderFrame();
        renderHudFrame(0);
        if (keyValue == 0)
            drawInstrumentGaugesFar();
        gfx_dacAnimate();
        camRestore(&camNext); /* restore authoritative sim state for the next step */
        objRestore(simNext, projNext);
    } while (g_missionEndedFlag[0] == 0);
}

void runGameLoop(void) {
    gameMainLoop();
}

/* setupDac (egcode.asm _setupDac) - load the 256-colour palette: dacValues1 →
 * DAC entries 0x10-0x5F, dacValues (otherDacValues at night) → 0x60-0xFF, and
 * unless g_horizonGroundColor==2 the 16-entry ground ramp (g_dacGroundPaletteSrc)
 * is copied over g_dacGroundPalette (= dacValues+0x30) first. */
void setupDac(void) {
    int i;
    gfx_setDacRange(0x10, 0x50, dacValues1);
    if (g_horizonGroundColor != 2) {
        for (i = 0; i < 0x30; i++)
            dacValues[0x30 + i] = g_dacGroundPaletteSrc[i];
    }
    gfx_setDacRange(0x60, 0xA0, g_nightMode != 0 ? otherDacValues : dacValues);
}
