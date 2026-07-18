/* Versioned JSON snapshot for offline blackbox investigation.
 *
 * Fields are serialized individually instead of dumping struct bytes. This
 * avoids compiler padding, host endianness, pointer values, and uninitialized
 * storage, while retaining every field of the runtime object records. */
#include "blackbox_snapshot.h"

#include "blackbox.h"
#include "blackbox_state.h"
#include "egdata.h"

#include <stdio.h>

enum {
    SNAPSHOT_FORMAT_VERSION = 1,
    SNAPSHOT_OBJECT_COUNT = 20,
    SNAPSHOT_PROJECTILE_COUNT = 12,
    SNAPSHOT_BULLET_COUNT = 20
};

extern uint8 timerCounter;
extern uint8 timerCounter2;
extern uint8 timerCounter3;
extern uint8 timerCounter4;
extern uint8 timerHandlerInstalled;

static unsigned displayedTime(unsigned tick) {
    return (tick / 60u) * 100u + tick % 60u;
}

static const char *modeName(BlackboxMode mode) {
    switch (mode) {
    case BLACKBOX_DEBUG: return "debug";
    case BLACKBOX_RECORD: return "record";
    case BLACKBOX_REPLAY: return "replay";
    default: return "off";
    }
}

static void writeJsonString(FILE *f, const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    fputc('"', f);
    for (; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', f);
            fputc(*p, f);
        } else if (*p < 0x20) {
            fprintf(f, "\\u%04x", (unsigned)*p);
        } else {
            fputc(*p, f);
        }
    }
    fputc('"', f);
}

static void writeMissionTables(FILE *f) {
    int i, j;
    fputs("  \"waypoints\": [", f);
    for (i = 0; i < 4; i++) {
        fprintf(f, "%s{\"slot\":%d,\"map_x\":%u,\"map_y\":%u}",
                i ? "," : "", i, (unsigned)waypoints[i].mapX,
                (unsigned)waypoints[i].mapY);
    }
    fputs("],\n  \"target_slots\": [\n", f);
    for (i = 0; i < 2; i++) {
        const struct TargetSlot *slot = &g_targetSlots[i];
        fprintf(f,
                "    {\"slot\":%d,\"state\":%d,\"plane_index\":%d,"
                "\"view_index\":%d,\"flags\":%d,\"seed_noise\":%d,\"unused\":[",
                i, (int)slot->state, (int)slot->planeIndex,
                (int)slot->viewIndex, (int)slot->flags, (int)slot->seedNoise);
        for (j = 0; j < 4; j++) fprintf(f, "%s%d", j ? "," : "", (int)slot->unused[j]);
        fprintf(f, "]}%s\n", i == 1 ? "" : ",");
    }
    fputs("  ],\n  \"map_targets\": [\n", f);
    for (i = 0; i < 74; i++) {
        const struct MapTarget *target = &g_planeTable.planes[i];
        fprintf(f,
                "    {\"slot\":%d,\"position\":{\"map_x\":%u,\"map_y\":%u},"
                "\"active\":%d,\"flags\":%d,\"alert_level\":%d,"
                "\"threat_timer\":%d,\"name_index\":%d,"
                "\"secondary_name_index\":%d}%s\n",
                i, (unsigned)target->mapX, (unsigned)target->mapY,
                (int)target->active, (int)target->flags,
                (int)target->alertLevel, (int)target->threatTimer,
                (int)target->nameIndex, (int)target->secondaryNameIndex,
                i == 73 ? "" : ",");
    }
    fputs("  ],\n", f);
}

static void writeSimObjects(FILE *f) {
    int i;
    fputs("  \"sim_objects\": [\n", f);
    for (i = 0; i < SNAPSHOT_OBJECT_COUNT; i++) {
        const struct SimObject *o = &g_simObjects[i];
        fprintf(f,
                "    {\"slot\":%d,\"obj_type\":%d,"
                "\"position\":{\"coarse_x\":%u,\"coarse_y\":%u,"
                "\"world_x\":%d,\"world_y\":%d,\"altitude\":%d},"
                "\"orientation\":{\"heading\":%d,\"pitch\":%d,\"bank\":%d},"
                "\"spec\":%d,\"flags\":%u,\"speed\":%d,\"timer\":%d,"
                "\"weapon_type\":%d,\"terrain_color\":%d,\"damage\":%d}%s\n",
                i, (int)o->objType, (unsigned)o->posX, (unsigned)o->posY,
                (int)o->worldX, (int)o->worldY, (int)o->alt,
                (int)o->heading.w, (int)o->pitch, (int)o->bank.w,
                (int)o->spec, (unsigned)o->flags.w, (int)o->speed,
                (int)o->timer, (int)o->weaponType, (int)o->terrainColor,
                (int)o->damage, i + 1 == SNAPSHOT_OBJECT_COUNT ? "" : ",");
    }
    fputs("  ],\n", f);
}

static void writeProjectiles(FILE *f) {
    int i;
    fputs("  \"projectiles\": [\n", f);
    for (i = 0; i < SNAPSHOT_PROJECTILE_COUNT; i++) {
        const struct Projectile *p = &g_projectiles[i];
        fprintf(f,
                "    {\"slot\":%d,\"position\":{\"map_x\":%u,\"map_y\":%u,"
                "\"fine_x\":%d,\"fine_y\":%d,\"interpolated_x\":%d,"
                "\"interpolated_y\":%d,\"altitude\":%d},"
                "\"orientation\":{\"world_x\":%d,\"world_y\":%d,\"world_z\":%d},"
                "\"speed\":%d,\"ttl\":%d,\"spec_index\":%d,"
                "\"weapon_index\":%d,\"target_lock\":%d,\"target_ref\":%d}%s\n",
                i, (unsigned)p->mapX, (unsigned)p->mapY, (int)p->fineX,
                (int)p->fineY, (int)g_projInterpX[i], (int)g_projInterpY[i],
                (int)p->alt, (int)p->worldX, (int)p->worldY, (int)p->worldZ,
                (int)p->speed, (int)p->ttl, (int)p->specIdx,
                (int)p->weaponIdx, (int)p->targetLock, (int)p->targetRef,
                i + 1 == SNAPSHOT_PROJECTILE_COUNT ? "" : ",");
    }
    fputs("  ],\n", f);
}

static void writeBullets(FILE *f) {
    int i;
    fputs("  \"bullet_tracks\": [\n", f);
    for (i = 0; i < SNAPSHOT_BULLET_COUNT; i++) {
        const struct BulletTrack *b = &bulletTracks[i];
        fprintf(f,
                "    {\"slot\":%d,\"position\":{\"x\":%d,\"y\":%d,\"altitude\":%d},"
                "\"velocity\":{\"x\":%d,\"y\":%d,\"z\":%d}}%s\n",
                i, (int)b->posX, (int)b->posY, (int)b->alt,
                (int)b->velX, (int)b->velY, (int)b->velZ,
                i + 1 == SNAPSHOT_BULLET_COUNT ? "" : ",");
    }
    fputs("  ]\n", f);
}

int blackbox_snapshotWriteJson(const char *path) {
    FILE *f;
    unsigned tick;
    int closeResult;
    BlackboxDebugState debug;
    if (!path || !*path) return 0;
    f = fopen(path, "w");
    if (!f) return 0;

    tick = (unsigned)blackbox_tick();
    blackbox_getDebugState(&debug);
    fprintf(f, "{\n  \"format\":\"f15se2-blackbox-state\",\n  \"version\":%d,\n",
            SNAPSHOT_FORMAT_VERSION);
    fputs("  \"provenance\":{\"build_version\":", f);
    writeJsonString(f, blackbox_state_currentBuildVersion());
    fputs(",\"replay_build_version\":", f);
    writeJsonString(f, blackbox_state_replayBuildVersion());
    fputs("},\n", f);
    fprintf(f, "  \"time\":{\"raw_tick\":%u,\"displayed\":%u},\n",
            tick, displayedTime(tick));
    fprintf(f,
            "  \"blackbox\":{\"mode\":\"%s\",\"input_pump\":%u,"
            "\"configured_seed\":%u,\"rng_state\":%u,"
            "\"pause_tick\":%u,\"fast_forward_tick\":%u,"
            "\"cursors\":{\"keys\":[%u,%u],\"axes\":[%u,%u],"
            "\"seeds\":[%u,%u],\"rng\":[%u,%u],\"frames\":[%u,%u],"
            "\"frame_index\":%u}},\n",
            modeName(debug.mode), (unsigned)debug.inputPump,
            (unsigned)debug.configuredSeed, (unsigned)debug.rngState,
            (unsigned)debug.pauseTick, (unsigned)debug.fastForwardTick,
            (unsigned)debug.keyPosition, (unsigned)debug.keyCount,
            (unsigned)debug.axesPosition, (unsigned)debug.axesCount,
            (unsigned)debug.seedPosition, (unsigned)debug.seedCount,
            (unsigned)debug.rngPosition, (unsigned)debug.rngCount,
            (unsigned)debug.framePosition, (unsigned)debug.frameCount,
            (unsigned)debug.frameIndex);
    fprintf(f,
            "  \"timing\":{\"timer_installed\":%u,\"counters\":[%u,%u,%u,%u],"
            "\"frame_tick\":%d,\"sim_steps_this_frame\":%d,"
            "\"render_alpha_q12\":%d,\"game_rng_seed\":%d},\n",
            (unsigned)timerHandlerInstalled, (unsigned)timerCounter,
            (unsigned)timerCounter2, (unsigned)timerCounter3,
            (unsigned)timerCounter4, (int)frameTick, g_simStepsThisFrame,
            g_renderAlphaQ12, (int)g_rngSeed);
    fprintf(f,
            "  \"flight\":{\"position\":{\"x\":%d,\"y\":%d,\"altitude\":%d},"
            "\"orientation\":{\"heading\":%d,\"pitch\":%d,\"roll\":%d},"
            "\"velocity\":%d,\"knots\":%d,\"fuel_remaining\":%d,"
            "\"damage_flag\":%d,\"plane_flags\":%d},\n",
            (int)g_ViewX, (int)g_ViewY, (int)g_viewZ, (int)g_ourHead,
            (int)g_ourPitch, (int)g_ourRoll, g_velocity, (int)g_knots,
            (int)g_fuelRemaining, (int)g_damageTakenFlag, (int)g_playerPlaneFlags);
    fprintf(f,
            "  \"camera\":{\"mode\":%d,\"eye\":{\"x\":%d,\"y\":%d,\"z\":%d},"
            "\"orientation\":{\"heading\":%d,\"pitch\":%d,\"roll\":%d},"
            "\"target\":{\"x\":%d,\"y\":%d,\"altitude\":%d,\"object\":%d},"
            "\"external_distance\":%d},\n",
            (int)g_viewMode, (int)g_camEyeX, (int)g_camEyeY, (int)g_camEyeZ,
            (int)g_viewHeading, (int)g_viewPitch, (int)g_viewRoll,
            (int)g_viewTargetX, (int)g_viewTargetY, (int)g_viewTargetAlt,
            (int)g_viewTargetObj, (int)g_externalCamDist);
    fprintf(f,
            "  \"target_mission\":{\"tracked_enemy\":%d,\"air_lock\":%d,"
            "\"ground_lock\":%d,\"aam_lock_active\":%d,\"target_in_hud\":%d,"
            "\"target_lead_angle\":%d,\"mission_status\":%d,\"mission_tick\":%d,"
            "\"mission_ended\":%u,\"enemy_air_remaining\":%d,"
            "\"enemy_ground_remaining\":%d,\"final_threat_score\":%d},\n",
            (int)g_trackedEnemyIdx, (int)g_airTargetLock,
            (int)g_groundTargetLock, (int)g_aamLockActive,
            (int)g_targetInHudFlag, (int)g_targetLeadAngle,
            (int)g_missionStatus, (int)g_missionTick,
            (unsigned)g_missionEndedFlag[0], (int)g_enemyAirRemaining,
            (int)g_enemyGroundRemaining, (int)g_finalThreatScore);
    fprintf(f,
            "  \"weapons\":{\"gun_ammo\":%d,\"current_type\":%d,"
            "\"last_missile_slot\":%d,\"fire_cooldown\":%d,"
            "\"active_bullet_tracks\":%d},\n",
            (int)g_gunAmmo, (int)g_currentWeaponType,
            (int)g_lastMissileSlot, (int)g_fireCooldown,
            (int)g_bulletTrackCount);
    writeMissionTables(f);
    writeSimObjects(f);
    writeProjectiles(f);
    writeBullets(f);
    fputs("}\n", f);

    closeResult = fclose(f);
    return closeResult == 0;
}
