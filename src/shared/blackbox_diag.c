/* Deterministic gameplay diagnostics layered on the blackbox stream.
 *
 * This file deliberately reads game state without owning or changing it. The
 * only gameplay hook is one call after each authoritative simulation step; 3D
 * capture sits at the existing backend-independent r3d dispatch seam. */
#include "blackbox_diag.h"

#include "blackbox.h"
#include "blackbox_internal.h"
#include "blackbox_snapshot.h"
#include "blackbox_state.h"
#include "egdata.h"
#include "log.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DIAG_OBJECT_COUNT = 20,
    DIAG_PROJECTILE_COUNT = 12,
    DIAG_RENDER_COMMANDS = 512,
    DIAG_NAME_SIZE = 32
};

typedef struct MarkerEvent {
    uint32 tick;
    char name[DIAG_NAME_SIZE];
    int32 a, b, c;
} MarkerEvent;

typedef struct StateEvent {
    uint32 tick;
    uint32 step;
    char name[DIAG_NAME_SIZE];
    uint32 hash;
} StateEvent;

typedef struct RenderEvent {
    uint32 tick;
    uint32 frame;
    uint32 scene;
    uint32 objects;
    uint32 lines;
    uint32 hash;
} RenderEvent;

typedef struct RenderCommand {
    char type;
    int32 v[8];
} RenderCommand;

static MarkerEvent *s_markers;
static size_t s_markerCount, s_markerCapacity, s_markerPos;
static StateEvent *s_states;
static size_t s_stateCount, s_stateCapacity, s_statePos;
static RenderEvent *s_renders;
static size_t s_renderCount, s_renderCapacity, s_renderPos;
static uint32 s_simStep;
static uint32 s_renderFrame;
static uint32 s_renderScene;
static uint32 s_dumpTick = 0xffffffffu;
static int s_dumpWritten;
static uint32 s_sceneObjects;
static uint32 s_sceneLines;
static uint32 s_sceneHash;
static int s_captureRenderCommands;
static int s_stateBaseline;
static int s_reportedMarkerDivergence;
static int s_reportedStateDivergence;
static int s_reportedRenderDivergence;
static ViewMode s_prevViewMode;
static int16 s_prevTrackedEnemy;
static int16 s_prevAirLock;
static int16 s_prevGroundLock;
static uint8 s_prevMissionEnded;
static int16 s_prevProjectileTtl[DIAG_PROJECTILE_COUNT];
static RenderCommand s_recentCommands[DIAG_RENDER_COMMANDS];
static size_t s_recentCommandCount;
static size_t s_recentCommandDropped;

extern uint8 timerCounter;
extern uint8 timerCounter2;
extern uint8 timerCounter3;
extern uint8 timerCounter4;
extern uint8 timerHandlerInstalled;

static int reserve(void **items, size_t *capacity, size_t count, size_t size) {
    size_t nextCapacity;
    void *next;
    if (count < *capacity) return 1;
    nextCapacity = *capacity ? *capacity * 2u : 128u;
    if (nextCapacity <= *capacity || nextCapacity > SIZE_MAX / size) return 0;
    next = realloc(*items, nextCapacity * size);
    if (!next) return 0;
    *items = next;
    *capacity = nextCapacity;
    return 1;
}

static uint32 hashAdd(uint32 hash, uint32 value) {
    int i;
    for (i = 0; i < 4; i++) {
        hash ^= value & 0xffu;
        hash *= 16777619u;
        value >>= 8;
    }
    return hash;
}

static uint32 hashFlight(void) {
    uint32 h = 2166136261u;
    h = hashAdd(h, (uint32)g_ViewX); h = hashAdd(h, (uint32)g_ViewY);
    h = hashAdd(h, (uint16)g_viewZ); h = hashAdd(h, (uint16)g_ourHead);
    h = hashAdd(h, (uint16)g_ourPitch); h = hashAdd(h, (uint16)g_ourRoll);
    h = hashAdd(h, (uint32)g_velocity); h = hashAdd(h, (uint16)g_knots);
    h = hashAdd(h, (uint32)g_altitude); h = hashAdd(h, (uint16)g_fuelRemaining);
    h = hashAdd(h, (uint16)g_damageTakenFlag); h = hashAdd(h, (uint16)g_playerPlaneFlags);
    return h;
}

static uint32 hashCamera(void) {
    uint32 h = 2166136261u;
    h = hashAdd(h, (uint16)g_viewMode); h = hashAdd(h, (uint16)g_viewHeading);
    h = hashAdd(h, (uint16)g_viewPitch); h = hashAdd(h, (uint16)g_viewRoll);
    h = hashAdd(h, (uint32)g_camEyeX); h = hashAdd(h, (uint32)g_camEyeY);
    h = hashAdd(h, (uint16)g_camEyeZ); h = hashAdd(h, (uint32)g_viewTargetX);
    h = hashAdd(h, (uint32)g_viewTargetY); h = hashAdd(h, (uint16)g_viewTargetAlt);
    h = hashAdd(h, (uint16)g_viewTargetObj); h = hashAdd(h, (uint16)g_externalCamDist);
    return h;
}

static uint32 hashObjects(void) {
    uint32 h = 2166136261u;
    int i;
    for (i = 0; i < DIAG_OBJECT_COUNT; i++) {
        const struct SimObject *o = &g_simObjects[i];
        h = hashAdd(h, (uint16)o->objType); h = hashAdd(h, o->posX);
        h = hashAdd(h, o->posY); h = hashAdd(h, (uint16)o->alt);
        h = hashAdd(h, (uint32)o->worldX); h = hashAdd(h, (uint32)o->worldY);
        h = hashAdd(h, (uint16)o->heading.w); h = hashAdd(h, (uint16)o->pitch);
        h = hashAdd(h, (uint16)o->bank.w); h = hashAdd(h, (uint16)o->spec);
        h = hashAdd(h, o->flags.w); h = hashAdd(h, (uint16)o->speed);
        h = hashAdd(h, (uint16)o->timer); h = hashAdd(h, (uint16)o->weaponType);
        h = hashAdd(h, (uint16)o->damage);
    }
    return h;
}

static uint32 hashWeapons(void) {
    uint32 h = 2166136261u;
    int i;
    for (i = 0; i < DIAG_PROJECTILE_COUNT; i++) {
        const struct Projectile *p = &g_projectiles[i];
        h = hashAdd(h, p->mapX); h = hashAdd(h, p->mapY);
        h = hashAdd(h, (uint16)p->alt); h = hashAdd(h, (uint16)p->speed);
        h = hashAdd(h, (uint16)p->worldX); h = hashAdd(h, (uint16)p->worldY);
        h = hashAdd(h, (uint16)p->worldZ); h = hashAdd(h, (uint16)p->ttl);
        h = hashAdd(h, (uint16)p->specIdx); h = hashAdd(h, (uint16)p->weaponIdx);
        h = hashAdd(h, (uint16)p->targetLock); h = hashAdd(h, (uint16)p->targetRef);
        h = hashAdd(h, (uint32)p->fineX); h = hashAdd(h, (uint32)p->fineY);
    }
    h = hashAdd(h, (uint16)g_gunAmmo); h = hashAdd(h, (uint16)g_currentWeaponType);
    h = hashAdd(h, (uint16)g_lastMissileSlot); h = hashAdd(h, (uint16)g_fireCooldown);
    return h;
}

static uint32 hashTargetMission(void) {
    uint32 h = 2166136261u;
    h = hashAdd(h, (uint16)g_trackedEnemyIdx); h = hashAdd(h, (uint16)g_airTargetLock);
    h = hashAdd(h, (uint16)g_groundTargetLock); h = hashAdd(h, (uint16)g_aamLockActive);
    h = hashAdd(h, (uint16)g_targetInHudFlag); h = hashAdd(h, (uint16)g_targetLeadAngle);
    h = hashAdd(h, (uint16)g_missionStatus); h = hashAdd(h, (uint16)g_missionTick);
    h = hashAdd(h, g_missionEndedFlag[0]); h = hashAdd(h, (uint16)g_enemyAirRemaining);
    h = hashAdd(h, (uint16)g_enemyGroundRemaining); h = hashAdd(h, (uint16)g_finalThreatScore);
    if (gameData) {
        h = hashAdd(h, gameData->totalScore); h = hashAdd(h, (uint16)gameData->campaignProgress);
    }
    return h;
}

static void emitState(const char *name, uint32 hash) {
    FILE *file = blackbox_internalRecordFile();
    if (blackbox_recording() && file) {
        fprintf(file, "state %u %u %s %08x\n", (unsigned)blackbox_tick(),
                (unsigned)s_simStep, name, (unsigned)hash);
    } else if (blackbox_replaying() && s_stateCount) {
        if (s_statePos < s_stateCount) {
            const StateEvent *e = &s_states[s_statePos++];
            if (!s_reportedStateDivergence &&
                (e->tick != blackbox_tick() || e->step != s_simStep ||
                 strcmp(e->name, name) != 0 || e->hash != hash)) {
                s_reportedStateDivergence = 1;
                log_error("blackbox: subsystem divergence at tick %u step %u %s: expected tick %u step %u %s %08x, got %08x",
                          (unsigned)blackbox_tick(), (unsigned)s_simStep, name,
                          (unsigned)e->tick, (unsigned)e->step, e->name,
                          (unsigned)e->hash, (unsigned)hash);
            }
        } else if (!s_reportedStateDivergence) {
            s_reportedStateDivergence = 1;
            log_error("blackbox: subsystem hash stream exhausted at tick %u", (unsigned)blackbox_tick());
        }
    }
}

void blackbox_diagMarker(const char *name, int32 a, int32 b, int32 c) {
    FILE *file = blackbox_internalRecordFile();
    if (!blackbox_enabled() || !name || !*name) return;
    if (blackbox_recording() && file) {
        fprintf(file, "marker %u %s %d %d %d\n", (unsigned)blackbox_tick(),
                name, (int)a, (int)b, (int)c);
    } else if (blackbox_replaying() && s_markerCount) {
        if (s_markerPos < s_markerCount) {
            const MarkerEvent *e = &s_markers[s_markerPos++];
            if (!s_reportedMarkerDivergence &&
                (e->tick != blackbox_tick() || strcmp(e->name, name) != 0 ||
                 e->a != a || e->b != b || e->c != c)) {
                s_reportedMarkerDivergence = 1;
                log_error("blackbox: marker divergence at tick %u %s(%d,%d,%d): expected tick %u %s(%d,%d,%d)",
                          (unsigned)blackbox_tick(), name, (int)a, (int)b, (int)c,
                          (unsigned)e->tick, e->name, (int)e->a, (int)e->b, (int)e->c);
            }
        } else if (!s_reportedMarkerDivergence) {
            s_reportedMarkerDivergence = 1;
            log_error("blackbox: marker stream exhausted at tick %u", (unsigned)blackbox_tick());
        }
    }
}

static void captureBaseline(void) {
    int i;
    s_prevViewMode = g_viewMode;
    s_prevTrackedEnemy = g_trackedEnemyIdx;
    s_prevAirLock = g_airTargetLock;
    s_prevGroundLock = g_groundTargetLock;
    s_prevMissionEnded = g_missionEndedFlag[0];
    for (i = 0; i < DIAG_PROJECTILE_COUNT; i++) s_prevProjectileTtl[i] = g_projectiles[i].ttl;
    s_stateBaseline = 1;
}

void blackbox_diagCaptureSimStep(void) {
    int i;
    if (!blackbox_enabled()) return;
    s_simStep++;
    if (!s_stateBaseline) {
        captureBaseline();
        blackbox_diagMarker("sim_begin", g_viewMode, g_planeCount, g_missionStatus);
    } else {
        if (s_prevViewMode != g_viewMode)
            blackbox_diagMarker("view_change", s_prevViewMode, g_viewMode, 0);
        if (s_prevTrackedEnemy != g_trackedEnemyIdx || s_prevAirLock != g_airTargetLock ||
            s_prevGroundLock != g_groundTargetLock)
            blackbox_diagMarker("target_change", g_trackedEnemyIdx, g_airTargetLock, g_groundTargetLock);
        for (i = 0; i < DIAG_PROJECTILE_COUNT; i++) {
            if (s_prevProjectileTtl[i] == 0 && g_projectiles[i].ttl != 0)
                blackbox_diagMarker("projectile_launch", i, g_projectiles[i].weaponIdx,
                                    g_projectiles[i].targetLock);
            else if (s_prevProjectileTtl[i] != 0 && g_projectiles[i].ttl == 0)
                blackbox_diagMarker("projectile_remove", i, g_projectiles[i].weaponIdx,
                                    g_projectiles[i].targetLock);
        }
        if (!s_prevMissionEnded && g_missionEndedFlag[0])
            blackbox_diagMarker("mission_end", g_missionStatus, commData ? commData->landingType : 0, 0);
        captureBaseline();
    }
    emitState("flight", hashFlight());
    emitState("camera", hashCamera());
    emitState("objects", hashObjects());
    emitState("weapons", hashWeapons());
    emitState("target_mission", hashTargetMission());
}

static int32 meshOffset(R3DMesh mesh) {
    uintptr_t p = (uintptr_t)mesh;
    uintptr_t base = (uintptr_t)g_world3dData;
    if (p < base || p >= base + WORLD3D_DATA_SIZE) return -1;
    return (int32)(p - base);
}

static void rememberCommand(char type, const int32 *values, int count) {
    RenderCommand *cmd;
    int i;
    if (s_recentCommandCount >= DIAG_RENDER_COMMANDS) {
        s_recentCommandDropped++;
        return;
    }
    cmd = &s_recentCommands[s_recentCommandCount++];
    cmd->type = type;
    for (i = 0; i < 8; i++) cmd->v[i] = i < count ? values[i] : 0;
}

void blackbox_diagBeginRenderFrame(void) {
    if (!blackbox_enabled()) return;
    s_renderFrame++;
    s_renderScene = 0;
    s_recentCommandCount = 0;
    s_recentCommandDropped = 0;
}

void blackbox_diagRenderBeginScene(const R3DScene *scene) {
    FILE *file = blackbox_internalRecordFile();
    int32 values[8];
    if (!blackbox_enabled() || !scene) return;
    s_renderScene++;
    s_sceneObjects = s_sceneLines = 0;
    s_sceneHash = 2166136261u;
    values[0] = scene->angleX; values[1] = scene->angleY; values[2] = scene->angleZ;
    values[3] = scene->posX; values[4] = scene->posY; values[5] = scene->posZ;
    values[6] = scene->renderScene;
    rememberCommand('S', values, 7);
    for (int i = 0; i < 7; i++) s_sceneHash = hashAdd(s_sceneHash, (uint32)values[i]);
    if (blackbox_recording() && s_captureRenderCommands && file)
        fprintf(file, "render_scene %u %u %u %d %d %d %d %d %d %d\n",
                (unsigned)blackbox_tick(), (unsigned)s_renderFrame, (unsigned)s_renderScene,
                values[0], values[1], values[2], values[3], values[4], values[5], values[6]);
}

void blackbox_diagRenderSubmit(const R3DSubmit *sub) {
    FILE *file = blackbox_internalRecordFile();
    int32 values[8];
    if (!blackbox_enabled() || !sub) return;
    values[0] = meshOffset(sub->mesh); values[1] = sub->yaw; values[2] = sub->pitch;
    values[3] = sub->roll; values[4] = sub->posX; values[5] = sub->posY;
    values[6] = sub->posZ; values[7] = sub->shadow;
    rememberCommand('O', values, 8);
    for (int i = 0; i < 8; i++) s_sceneHash = hashAdd(s_sceneHash, (uint32)values[i]);
    s_sceneObjects++;
    if (blackbox_recording() && s_captureRenderCommands && file)
        fprintf(file, "render_object %u %u %u %u %d %d %d %d %d %d %d %d\n",
                (unsigned)blackbox_tick(), (unsigned)s_renderFrame, (unsigned)s_renderScene,
                (unsigned)s_sceneObjects, values[0], values[1], values[2], values[3],
                values[4], values[5], values[6], values[7]);
}

void blackbox_diagRenderLine(const R3DLine *line) {
    FILE *file = blackbox_internalRecordFile();
    int32 values[8];
    if (!blackbox_enabled() || !line) return;
    values[0] = (int32)line->baseXA; values[1] = (int32)line->camXA;
    values[2] = (int32)line->camYA; values[3] = (int32)line->baseXB;
    values[4] = (int32)line->camXB; values[5] = (int32)line->camYB;
    values[6] = line->color;
    rememberCommand('L', values, 7);
    for (int i = 0; i < 7; i++) s_sceneHash = hashAdd(s_sceneHash, (uint32)values[i]);
    s_sceneLines++;
    if (blackbox_recording() && s_captureRenderCommands && file)
        fprintf(file, "render_line %u %u %u %u %d %d %d %d %d %d %d\n",
                (unsigned)blackbox_tick(), (unsigned)s_renderFrame, (unsigned)s_renderScene,
                (unsigned)s_sceneLines, values[0], values[1], values[2], values[3],
                values[4], values[5], values[6]);
}

void blackbox_diagRenderEndScene(void) {
    FILE *file = blackbox_internalRecordFile();
    RenderEvent got;
    if (!blackbox_enabled()) return;
    got.tick = blackbox_tick(); got.frame = s_renderFrame; got.scene = s_renderScene;
    got.objects = s_sceneObjects; got.lines = s_sceneLines; got.hash = s_sceneHash;
    if (blackbox_recording() && file) {
        fprintf(file, "render_hash %u %u %u %u %u %08x\n", (unsigned)got.tick,
                (unsigned)got.frame, (unsigned)got.scene, (unsigned)got.objects,
                (unsigned)got.lines, (unsigned)got.hash);
    } else if (blackbox_replaying() && s_renderCount) {
        if (s_renderPos < s_renderCount) {
            const RenderEvent *e = &s_renders[s_renderPos++];
            if (!s_reportedRenderDivergence && memcmp(e, &got, sizeof(got)) != 0) {
                s_reportedRenderDivergence = 1;
                log_error("blackbox: render submission divergence at tick %u frame %u scene %u: expected %u objects/%u lines hash %08x, got %u/%u %08x",
                          (unsigned)got.tick, (unsigned)got.frame, (unsigned)got.scene,
                          (unsigned)e->objects, (unsigned)e->lines, (unsigned)e->hash,
                          (unsigned)got.objects, (unsigned)got.lines, (unsigned)got.hash);
            }
        } else if (!s_reportedRenderDivergence) {
            s_reportedRenderDivergence = 1;
            log_error("blackbox: render hash stream exhausted at tick %u", (unsigned)got.tick);
        }
    }
}

int blackbox_diagWriteDump(const char *path) {
    FILE *f;
    int i;
    BlackboxDebugState debug;
    if (!path || !*path) return 0;
    f = fopen(path, "w");
    if (!f) return 0;
    blackbox_getDebugState(&debug);
    fprintf(f, "F15SE2 diagnostic dump\nraw_tick=%u displayed_time=%u\n",
            (unsigned)blackbox_tick(),
            (unsigned)((blackbox_tick() / 60u) * 100u + blackbox_tick() % 60u));
    fprintf(f, "provenance build=%s replay_build=%s mode=%d\n",
            blackbox_state_currentBuildVersion(),
            blackbox_state_replayBuildVersion(), (int)debug.mode);
    fprintf(f, "blackbox input_pump=%u seed=%u rng_state=%u cursors=key:%u/%u axes:%u/%u seed:%u/%u rng:%u/%u frame:%u/%u index:%u\n",
            (unsigned)debug.inputPump, (unsigned)debug.configuredSeed,
            (unsigned)debug.rngState, (unsigned)debug.keyPosition,
            (unsigned)debug.keyCount, (unsigned)debug.axesPosition,
            (unsigned)debug.axesCount, (unsigned)debug.seedPosition,
            (unsigned)debug.seedCount, (unsigned)debug.rngPosition,
            (unsigned)debug.rngCount, (unsigned)debug.framePosition,
            (unsigned)debug.frameCount, (unsigned)debug.frameIndex);
    fprintf(f, "timing installed=%u counters=%u,%u,%u,%u frame_tick=%d sim_steps=%d render_alpha_q12=%d game_rng_seed=%d\n",
            (unsigned)timerHandlerInstalled, (unsigned)timerCounter,
            (unsigned)timerCounter2, (unsigned)timerCounter3,
            (unsigned)timerCounter4, (int)frameTick, g_simStepsThisFrame,
            g_renderAlphaQ12, (int)g_rngSeed);
    fprintf(f, "subsystem flight=%08x camera=%08x objects=%08x weapons=%08x target_mission=%08x\n",
            (unsigned)hashFlight(), (unsigned)hashCamera(), (unsigned)hashObjects(),
            (unsigned)hashWeapons(), (unsigned)hashTargetMission());
    fprintf(f, "flight view=(%d,%d,%d) pose=(%d,%d,%d) speed=%d knots=%d altitude=%u fuel=%d\n",
            (int)g_ViewX, (int)g_ViewY, (int)g_viewZ, (int)g_ourHead,
            (int)g_ourPitch, (int)g_ourRoll, g_velocity, (int)g_knots,
            (unsigned)g_altitude, (int)g_fuelRemaining);
    fprintf(f, "camera mode=%d eye=(%d,%d,%d) view=(%d,%d,%d) target=(%d,%d,%d,obj=%d)\n",
            (int)g_viewMode, (int)g_camEyeX, (int)g_camEyeY, (int)g_camEyeZ,
            (int)g_viewHeading, (int)g_viewPitch, (int)g_viewRoll,
            (int)g_viewTargetX, (int)g_viewTargetY, (int)g_viewTargetAlt,
            (int)g_viewTargetObj);
    fprintf(f, "target tracked=%d air_lock=%d ground_lock=%d aam=%d mission_status=%d ended=%u\n",
            (int)g_trackedEnemyIdx, (int)g_airTargetLock, (int)g_groundTargetLock,
            (int)g_aamLockActive, (int)g_missionStatus, (unsigned)g_missionEndedFlag[0]);
    for (i = 0; i < DIAG_OBJECT_COUNT; i++) {
        const struct SimObject *o = &g_simObjects[i];
        fprintf(f, "object[%02d] type=%d flags=%04x pos=(%d,%d,%d) pose=(%d,%d,%d) spec=%d speed=%d timer=%d damage=%d\n",
                i, (int)o->objType, (unsigned)o->flags.w, (int)o->worldX,
                (int)o->worldY, (int)o->alt, (int)o->heading.w, (int)o->pitch,
                (int)o->bank.w, (int)o->spec, (int)o->speed, (int)o->timer,
                (int)o->damage);
    }
    for (i = 0; i < DIAG_PROJECTILE_COUNT; i++) {
        const struct Projectile *p = &g_projectiles[i];
        fprintf(f, "projectile[%02d] ttl=%d weapon=%d spec=%d target=%d ref=%d pos=(%d,%d,%d) fine=(%d,%d)\n",
                i, (int)p->ttl, (int)p->weaponIdx, (int)p->specIdx,
                (int)p->targetLock, (int)p->targetRef, (int)p->mapX,
                (int)p->mapY, (int)p->alt, (int)p->fineX, (int)p->fineY);
    }
    fprintf(f, "render frame=%u commands=%u dropped=%u\n", (unsigned)s_renderFrame,
            (unsigned)s_recentCommandCount, (unsigned)s_recentCommandDropped);
    for (i = 0; i < (int)s_recentCommandCount; i++) {
        const RenderCommand *cmd = &s_recentCommands[i];
        fprintf(f, "render[%03d] %c %d %d %d %d %d %d %d %d\n", i, cmd->type,
                cmd->v[0], cmd->v[1], cmd->v[2], cmd->v[3], cmd->v[4], cmd->v[5],
                cmd->v[6], cmd->v[7]);
    }
    fclose(f);
    log_info("blackbox: wrote diagnostic dump '%s'", path);
    return 1;
}

void blackbox_diagWriteAutomaticDump(void) {
    char textPath[96];
    char jsonPath[96];
    uint32 tick = blackbox_tick();
    uint32 shown = (tick / 60u) * 100u + tick % 60u;
    snprintf(textPath, sizeof(textPath), "blackbox-dump-%u.txt", (unsigned)shown);
    snprintf(jsonPath, sizeof(jsonPath), "blackbox-dump-%u.json", (unsigned)shown);
    if (!blackbox_diagWriteDump(textPath))
        log_error("blackbox: cannot write diagnostic dump '%s'", textPath);
    if (!blackbox_snapshotWriteJson(jsonPath))
        log_error("blackbox: cannot write state snapshot '%s'", jsonPath);
    else
        log_info("blackbox: wrote state snapshot '%s'", jsonPath);
}

void blackbox_diagSetDumpTick(uint32 tick) {
    s_dumpTick = tick;
    s_dumpWritten = 0;
    log_info("blackbox: automatic dump tick=%u", (unsigned)tick);
}

void blackbox_diagOnTick(void) {
    /* timerPump calls this after the optional per-tick gameplay hook. Capturing
     * here is deterministic, works in menus and flight, and does not depend on
     * renderer presentation or host timing. */
    if (!s_dumpWritten && s_dumpTick != 0xffffffffu &&
        blackbox_tick() >= s_dumpTick) {
        s_dumpWritten = 1;
        blackbox_diagWriteAutomaticDump();
    }
}

void blackbox_diagSetRenderCapture(int enabled) { s_captureRenderCommands = enabled != 0; }

void blackbox_diagReset(void) {
    free(s_markers); free(s_states); free(s_renders);
    s_markers = NULL; s_states = NULL; s_renders = NULL;
    s_markerCount = s_markerCapacity = s_markerPos = 0;
    s_stateCount = s_stateCapacity = s_statePos = 0;
    s_renderCount = s_renderCapacity = s_renderPos = 0;
    s_dumpTick = 0xffffffffu;
    s_dumpWritten = 0;
    s_simStep = s_renderFrame = s_renderScene = 0;
    s_sceneObjects = s_sceneLines = 0; s_sceneHash = 0;
    s_captureRenderCommands = 0; s_stateBaseline = 0;
    s_reportedMarkerDivergence = s_reportedStateDivergence = s_reportedRenderDivergence = 0;
    s_recentCommandCount = s_recentCommandDropped = 0;
}

int blackbox_diagParseReplayLine(const char *line) {
    unsigned tick, step, frame, scene, objects, lines, hash;
    int a, b, c;
    char name[DIAG_NAME_SIZE];
    if (sscanf(line, "marker %u %31s %d %d %d", &tick, name, &a, &b, &c) == 5) {
        MarkerEvent *e;
        if (!reserve((void **)&s_markers, &s_markerCapacity, s_markerCount, sizeof(*s_markers))) return -1;
        e = &s_markers[s_markerCount++]; e->tick = tick;
        strcpy(e->name, name); e->a = a; e->b = b; e->c = c;
        return 1;
    }
    if (sscanf(line, "state %u %u %31s %x", &tick, &step, name, &hash) == 4) {
        StateEvent *e;
        if (!reserve((void **)&s_states, &s_stateCapacity, s_stateCount, sizeof(*s_states))) return -1;
        e = &s_states[s_stateCount++]; e->tick = tick; e->step = step;
        strcpy(e->name, name); e->hash = hash;
        return 1;
    }
    if (sscanf(line, "render_hash %u %u %u %u %u %x", &tick, &frame, &scene, &objects, &lines, &hash) == 6) {
        RenderEvent *e;
        if (!reserve((void **)&s_renders, &s_renderCapacity, s_renderCount, sizeof(*s_renders))) return -1;
        e = &s_renders[s_renderCount++]; e->tick = tick; e->frame = frame;
        e->scene = scene; e->objects = objects; e->lines = lines; e->hash = hash;
        return 1;
    }
    /* Detailed commands are retained for human/agent inspection. Replay proves
     * equivalence with the compact render_hash emitted at every scene end. */
    if (strncmp(line, "render_scene ", 13) == 0 ||
        strncmp(line, "render_object ", 14) == 0 ||
        strncmp(line, "render_line ", 12) == 0) return 1;
    return 0;
}

int blackbox_diagValidateReplay(void) { return 1; }

void blackbox_diagShutdown(int pausedForInspection) {
    if (blackbox_replaying() && !pausedForInspection) {
        if (s_markerCount && s_markerPos != s_markerCount)
            log_error("blackbox: replay consumed %u of %u diagnostic markers", (unsigned)s_markerPos, (unsigned)s_markerCount);
        if (s_stateCount && s_statePos != s_stateCount)
            log_error("blackbox: replay consumed %u of %u subsystem hashes", (unsigned)s_statePos, (unsigned)s_stateCount);
        if (s_renderCount && s_renderPos != s_renderCount)
            log_error("blackbox: replay consumed %u of %u render hashes", (unsigned)s_renderPos, (unsigned)s_renderCount);
    }
    blackbox_diagReset();
}
