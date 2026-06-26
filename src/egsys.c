/*
 * egsys.c - DOS system services for egame (file I/O, game loop, DAC palette),
 * isolated here so a fork can replace just this file.
 *
 * The file-handle primitives mirror egcode.asm (small model: near buffer/path
 * pointers are DGROUP-relative, so DS is left at the current DGROUP via segread):
 *   openFile/createFile  - INT 21h/3Dh,3Ch; path is a near (DS) pointer
 *   closeFile            - INT 21h/3Eh
 *   readFile1            - INT 21h/3Fh into DGROUP:bufOffset
 *   readFile2            - INT 21h/3Fh into bufSegment:bufOffset
 *   writeFileAtRaw       - INT 21h/40h from bufSegment:(bufOffset+offsetAddend)
 * The ASM also reset the global fileReadPos to 0x200 on open/create for its own
 * buffered PIC reader; the C PIC decoder (shared/picimpl.c) keeps its own buffer
 * position, so that side effect is intentionally dropped here.
 */

#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "inttype.h"
#include "pointers.h"
#include "gfx.h"
#include <dos.h>

/* per-frame work reconstructed in their own TUs (egflight/egtacmap/egframe),
 * not surfaced in a header; declared here for the game loop below. */
void renderFrame(void);
void renderHudFrame(int unused);
void stepFlightModel(void);
void updateFrame(void);

/* ---- Fixed-timestep sim + interpolated rendering (see docs/render-sim-decouple.md) ----
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
 * precision) and slow-motion still lowers it. */

#define NS_PER_SEC 1000000000ULL

/* The authoritative sim state the camera is derived from each renderFrame()
 * (forward view: the g_camEye and g_view globals are recomputed from these). We
 * snapshot prev/next and write an interpolated copy into the live globals just
 * for the render, then restore the authoritative "next" values for the next
 * sim step. Objects (g_simObjects[] etc.) are not yet interpolated - they render
 * at their latest sim position; camera motion dominates the visible flow. */
typedef struct {
    int32 viewX, viewY, viewZ;
    int32 head, pitch, roll;
} CamSnapshot;

static void camCapture(CamSnapshot *s) {
    s->viewX = g_ViewX;
    s->viewY = g_ViewY;
    s->viewZ = g_viewZ;
    s->head = g_ourHead;
    s->pitch = g_ourPitch;
    s->roll = g_ourRoll;
}

static void camRestore(const CamSnapshot *s) {
    g_ViewX = s->viewX;
    g_ViewY = s->viewY;
    g_viewZ = (int16)s->viewZ;
    g_ourHead = (int16)s->head;
    g_ourPitch = s->pitch;
    g_ourRoll = (int16)s->roll;
}

static int32 lerpLinear(int32 a, int32 b, int64 num, int64 den) {
    return a + (int32)(((int64)(b - a) * num) / den);
}

/* Shortest-arc interpolation in 16-bit angle space (heading wraps; pitch/roll
 * don't in practice, where the int16 delta degenerates to a plain difference). */
static int32 lerpAngle(int32 a, int32 b, int64 num, int64 den) {
    int16 d = (int16)(b - a);
    return a + (int32)(((int64)d * num) / den);
}

static void camApplyInterp(const CamSnapshot *p, const CamSnapshot *n, int64 num, int64 den) {
    g_ViewX = lerpLinear(p->viewX, n->viewX, num, den);
    g_ViewY = lerpLinear(p->viewY, n->viewY, num, den);
    g_viewZ = (int16)lerpLinear(p->viewZ, n->viewZ, num, den);
    g_ourHead = (int16)lerpAngle(p->head, n->head, num, den);
    g_ourPitch = lerpAngle(p->pitch, n->pitch, num, den);
    g_ourRoll = (int16)lerpAngle(p->roll, n->roll, num, den);
}

static uint64 simStepNsNow(void) {
    int scaling = g_frameRateScaling;
    if (scaling < 1) scaling = 1;
    return NS_PER_SEC / (uint64)scaling;
}

/* The target/director (0x88/0x89/0x8b) and crash (0x8c) camera modes derive the
 * eye and look-at from a tracked world object (a projectile / sim object / other
 * plane, or g_crashCam*) whose position only updates at the sim rate. Camera
 * interpolation moves the *player* coords (g_ViewX etc.) at the render rate; in
 * these views that desyncs the smooth camera from the stepped target and the
 * relative motion jitters. Render those views at the coherent sim snapshot so
 * camera, target and world all step together (the player-centric views —
 * forward/side/rear/chase — stay interpolated). */
static int viewTracksObject(int kv) {
    return kv == 0x88 || kv == 0x89 || kv == 0x8b || kv == 0x8c;
}

/* gameMainLoop / runGameLoop. Each rendered frame composites the 3D world
 * (renderFrame), the tac map / 2D overlays (renderHudFrame) and the dynamic
 * gauges (unless a menu key overlay is up), then presents (gfx_dacAnimate, slot
 * 0x2c, vsync-paced). Between presents the sim is stepped fixed-rate and the
 * camera interpolated; under load the sim catches up and frames are dropped
 * rather than the game slowing. */
void gameMainLoop(void) {
    CamSnapshot camPrev, camNext;
    uint64 simStepNs = simStepNsNow();
    uint64 accumNs = 0;
    uint64 prevNs = timerNowNs();
    int steps;

    camCapture(&camNext);
    camPrev = camNext; /* first frame renders the spawn state, no interpolation */

    do {
        uint64 nowNs = timerNowNs();
        accumNs += nowNs - prevNs;
        prevNs = nowNs;
        timerPump(); /* advance the 60 Hz tick counters + per-tick / colour-cycle hook */

        steps = 0;
        while (accumNs >= simStepNs) {
            accumNs -= simStepNs;
            camPrev = camNext;
            stepFlightModel();
            updateFrame();
            camCapture(&camNext);
            if (g_initPhase < 2)
                camPrev = camNext; /* don't interpolate across mission init's state jumps */
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
        g_simStepsThisFrame = steps; /* paces render-rate animations (g_spinAngle) to the sim */

        if (!viewTracksObject(keyValue))
            camApplyInterp(&camPrev, &camNext, (int64)accumNs, (int64)simStepNs);
        /* else: leave the live globals at the authoritative sim state (camNext) */
        renderFrame();
        renderHudFrame(0);
        if (keyValue == 0)
            drawInstrumentGaugesFar();
        gfx_dacAnimate();
        camRestore(&camNext); /* restore authoritative sim state for the next step */
    } while (g_missionEndedFlag[0] == 0);
}

void runGameLoop(void) {
    gameMainLoop();
}

void closeFile(int handle) {
    union REGS r;
    r.h.ah = 0x3E;
    r.x.bx = handle;
    intdos(&r, &r);
}

int readFile1(int handle, int count, int bufOffset) {
    union REGS r;
    struct SREGS s;
    segread(&s); /* DS = DGROUP: read into DGROUP:bufOffset */
    r.h.ah = 0x3F;
    r.x.bx = handle;
    r.x.cx = count;
    r.x.dx = bufOffset;
    intdosx(&r, &r, &s);
    return r.x.cflag ? -1 : r.x.ax;
}

int readFile2(int handle, int count, int bufOffset, int bufSegment) {
    union REGS r;
    struct SREGS s;
    segread(&s);
    r.h.ah = 0x3F;
    r.x.bx = handle;
    r.x.cx = count;
    r.x.dx = bufOffset;
    s.ds = (uint16)bufSegment;
    intdosx(&r, &r, &s);
    return r.x.cflag ? -1 : r.x.ax;
}

int writeFileAtRaw(int handle, int count, int bufOffset, int bufSegment, int offsetAddend) {
    union REGS r;
    struct SREGS s;
    segread(&s);
    r.h.ah = 0x40;
    r.x.bx = handle;
    r.x.cx = count;
    r.x.dx = (uint16)(bufOffset + offsetAddend);
    s.ds = (uint16)bufSegment;
    intdosx(&r, &r, &s);
    return r.x.cflag ? -1 : r.x.ax;
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
