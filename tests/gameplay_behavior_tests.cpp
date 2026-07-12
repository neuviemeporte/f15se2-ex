// EGAME combat/flight gameplay behavior tests (LINK_CORE + headless).
//
// Exercises the real threat/combat/target/flight/keys logic against the linked
// core library. Scoped to functions whose behavior is observable through game
// state (threat scoring, SAM acquisition, target/mission bookkeeping, replay
// log, director scheduling, wreck physics, timing/LOD math).
#include "egdata.h"
#include "egmath.h"
#include "inttype.h"
#include "struct.h"
#include "comm.h"
#include "headless.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern int16 computeThreatRangeBearing(int16 threatX, int16 threatY, int16 threatAlt, int16 threatType,
                                     int16 *outBearing, int16 *outRange);
extern int16 computeThreatScore(void);
extern void updateThreatAlert(void);
extern int samCanAcquireTarget(int slot, int targetX, int targetY, int targetAlt, int mode);
extern int16 markTargetReached(int16 targetIdx);
extern void appendMapEvent(int16 eventType, int16 eventArg);
extern void scheduleTimedEvent(ViewMode viewMode, int16 delay);
extern void scheduleEventCheck(int16 eventObjIdx, uint16 priority);
extern void placeString(int16 waypointIdx);
extern void finalizeMission(int outcome);
extern void buildRangeString(int16 rangeRaw);
extern void applyGravityFall(void);
extern void tickMessageTimers(void);
extern void resetSimObjectLocks(void);
extern void recalcTimeScale(void);
extern void exitSlowMotion(void);
extern void setupLodDistances(void);
extern int rangeApprox(int deltaX, int deltaY);

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

// Reset only the state the focused tests read/write. (DOS zero-inited EGAME BSS
// per mission; the merged process does not, so each case starts from a clean
// slate.)
void resetGameplayState() {
    std::memset(&g_planeTable, 0, sizeof(g_planeTable));
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 12);
    std::memset(g_targetSlots, 0, sizeof(struct TargetSlot) * 2);
    std::memset(mapEvents, 0, sizeof(struct MapEvent) * 4);
    std::memset(&g_replayLog, 0, sizeof(g_replayLog));
    strBuf[0] = '\0';
    g_viewX_ = g_viewY_ = 0;
    g_viewZ = 0;
    g_ourHead = 0;
    g_frameRateScaling = 60;
    g_missionStatus = 0;
    g_difficultyTier = 0;
    g_targetEntityCount = 0;
    g_planeScanCount = 0;
    g_playerPlaneFlags = 0;
    waypointIndex = 0;
    g_acqRange = 0;
    g_acqAimY = 0;
    g_eventLogCount = 0;
    g_directorMode = 0;
    g_directorEventDeadline = -1;
    g_viewMode = VIEW_COCKPIT;
    g_viewTargetObj = 0;
    frameTick = 0;
    g_ejectState = 0;
    g_missionEndedFlag[0] = 0;
    g_bombDamageMask = 0;
    g_gunHits = 0;
    g_finalThreatScore = 0;
    g_resupplyCount = 0;
    g_missionTick = 0;
    g_threatActiveTimer = 0;
    g_threatTimerInit = 0;
    g_threatDisplayTtl = 0;
    g_threatRefX = g_threatRefY = g_threatRefZ = g_threatRefHead = 0;
    g_wreckAlt = 0;
    g_wreckFallVel = 0;
    g_slowMotionMode = 0;
    g_frameSyncWait = 0;
    g_bulletTrackCount = 0;
    g_detailLevel = 0;
    g_lodDistBase = 0x800;
    g_lodDistScale = 0x1000;
    g_lodDistNear = 0;
    g_lodDistFar = 0;
    g_trackedEnemyIdx = 0;
    g_strTruncDot = 0;
    g_strTruncTerm[0] = 0;
}

} // namespace

int main() {
    test_headless_init();

    // --- Threat range/bearing/score (egthreat) ------------------------------
    // Score is altitude-weighted; range is rangeApprox in km units (>>6);
    // bearing uses inverted map Y. Golden values pin the scoring formula.
    resetGameplayState();
    g_viewX_ = 0x2000;
    g_viewY_ = 0x3000;
    g_viewZ = 0x1000;
    g_missionStatus = 1;
    int16 bearing = 0;
    int16 range = 0;
    const int threatType = 4; // SA-10: lethality 320, dangerTier 7
    const int score = computeThreatRangeBearing(0x1C00, 0x3400, 123, threatType, &bearing, &range);
    require(range == (rangeApprox(0x0400, -0x0400) >> 6),
            "computeThreatRangeBearing stores rangeApprox distance in km units");
    require((uint16)bearing == (uint16)computeBearing(0x0400, 0x0400),
            "computeThreatRangeBearing stores bearing with inverted map Y");
    require(score == 240,
            "computeThreatRangeBearing preserves altitude-weighted threat score");
    require(computeThreatRangeBearing(0, 0, 0, 0, &bearing, &range) == 0,
            "computeThreatRangeBearing ignores empty threat type");
    require(computeThreatRangeBearing(0, 0, 0, -1, &bearing, &range) == 0,
            "computeThreatRangeBearing ignores sentinel threat type");

    resetGameplayState();
    g_missionStatus = 2;
    g_targetEntityCount = 4;
    g_planeTable.planes[0].active = 2; // SA-5
    g_planeTable.planes[1].active = 0; // empty slot, skipped
    g_planeTable.planes[2].active = 4; // SA-10
    g_planeTable.planes[3].active = 7; // SA-13
    require(computeThreatScore() == 2,
            "computeThreatScore sums only active slots with original divisors");

    // --- updateThreatAlert reference copy + alert clamp (egthreat) -----------
    resetGameplayState();
    g_threatTimerInit = 42;
    g_viewX_ = 0x1234;
    g_viewY_ = 0x2345;
    g_viewZ = 0x3456;
    g_ourHead = 0x6789;
    g_planeScanCount = 1;
    g_planeTable.planes[0].active = 1;
    g_planeTable.planes[0].alertLevel = 300; // above the 255 cap
    updateThreatAlert();
    require(g_threatActiveTimer == 42, "updateThreatAlert arms the active timer");
    require(g_threatRefX == 0x1234 && g_threatRefY == 0x2345,
            "updateThreatAlert takes the player position when no map event is live");
    require(g_threatRefZ == 0x3456 && g_threatRefHead == 0x6789,
            "updateThreatAlert copies player altitude and heading");
    require(g_planeTable.planes[0].alertLevel == 255,
            "updateThreatAlert clamps active alert levels to the max");
    mapEvents[0].ttl = 1;
    mapEvents[0].mapX = 0x1111;
    mapEvents[0].mapY = 0x2222;
    updateThreatAlert();
    require(g_threatRefX == 0x1111 && g_threatRefY == 0x2222,
            "updateThreatAlert takes the map-event position when one is live");

    // --- samCanAcquireTarget (egcombat) -------------------------------------
    resetGameplayState();
    g_projectiles[0].mapX = 1000;
    g_projectiles[0].mapY = 1000;
    g_projectiles[0].speed = 200;
    g_projectiles[0].worldX = 0x4000;
    g_frameRateScaling = 40;
    require(samCanAcquireTarget(0, 1100, 1000, 0, 0) == 1,
            "samCanAcquireTarget succeeds when projectile reaches target this frame");
    require(g_acqRange == rangeApprox(100, 0),
            "samCanAcquireTarget stores acquisition range");

    resetGameplayState();
    g_projectiles[0].mapX = 1000;
    g_projectiles[0].mapY = 1000;
    g_projectiles[0].speed = 10;
    g_projectiles[0].worldX = 0;
    g_projectiles[0].ttl = 1000;
    g_frameRateScaling = 20;
    require(samCanAcquireTarget(0, 1000, 3000, 0, 1) == 0,
            "samCanAcquireTarget rejects targets outside the turn cone");
    require(g_projectiles[0].ttl == (g_frameRateScaling << 4),
            "samCanAcquireTarget clamps far off-boresight SAM ttl for active slots");

    resetGameplayState();
    g_projectiles[0].mapX = 1000;
    g_projectiles[0].mapY = 1000;
    g_projectiles[0].speed = 10;
    g_projectiles[0].worldX = 0;
    g_ourHead = 0x8000;
    g_frameRateScaling = 20;
    require(samCanAcquireTarget(0, 3000, 1000, 0, 0) == 0,
            "samCanAcquireTarget mode 0 requires the forward-heading cone");

    // --- markTargetReached (egcombat) ---------------------------------------
    resetGameplayState();
    g_targetSlots[0].state = 2;
    require(markTargetReached(0) == 1, "markTargetReached accepts primary target first hit");
    require((g_playerPlaneFlags & 0x4000) != 0, "markTargetReached sets primary target flag");
    require(waypointIndex == 2, "markTargetReached advances primary waypoint");
    require(std::strcmp(strBuf, "Primary target") == 0, "markTargetReached writes primary message");
    require(markTargetReached(0) == 0, "markTargetReached rejects repeated primary hit");
    g_targetSlots[1].state = 2;
    require(markTargetReached(1) == 1, "markTargetReached accepts secondary target hit");
    require((g_playerPlaneFlags & 0x2000) != 0, "markTargetReached sets secondary target flag");
    require(waypointIndex == 3, "markTargetReached moves to exit waypoint after both hits");
    require(std::strcmp(strBuf, "Second. target") == 0, "markTargetReached writes secondary message");

    resetGameplayState();
    g_targetSlots[0].state = 4; // terminal lock state -> emits replay event
    require(markTargetReached(0) == 1 && g_eventLogCount == 1 &&
                g_replayLog.events[0].type == 0x8b,
            "markTargetReached appends the primary replay event for terminal states");
    resetGameplayState();
    g_targetSlots[1].state = 3;
    require(markTargetReached(1) == 1 && g_eventLogCount == 1 &&
                g_replayLog.events[0].type == 0x4b,
            "markTargetReached appends the secondary replay event for terminal states");

    // --- appendMapEvent replay log (egframe) --------------------------------
    resetGameplayState();
    g_missionTick = 1234;
    g_viewX_ = 0x1234;
    g_viewY_ = 0x5678;
    appendMapEvent(6, 42);
    require(g_eventLogCount == 1, "appendMapEvent increments the replay-log count");
    require(g_replayLog.events[0].coord == g_missionTick &&
                g_replayLog.events[0].screenX == (uint8)((unsigned)g_viewX_ >> 7) &&
                g_replayLog.events[0].screenY == (uint8)((unsigned)g_viewY_ >> 7) &&
                g_replayLog.events[0].type == 6 && g_replayLog.events[0].arg == 42,
            "appendMapEvent stores tick, map screen coords, type, and arg");
    require(g_replayLog.events[1].type == 0, "appendMapEvent writes a terminator event");
    g_eventLogCount = 255;
    g_replayLog.events[255].type = 77;
    appendMapEvent(1, 2);
    require(g_eventLogCount == 255 && g_replayLog.events[255].type == 77,
            "appendMapEvent ignores events once the log is full");

    // --- director event scheduling (egframe) --------------------------------
    resetGameplayState();
    g_frameRateScaling = 20;
    frameTick = 100;
    scheduleTimedEvent((ViewMode)0x77, 4);
    require(g_viewMode == VIEW_COCKPIT && g_directorEventDeadline == -1,
            "scheduleTimedEvent ignores requests when director mode is off");
    g_directorMode = 2;
    scheduleTimedEvent((ViewMode)0x77, 4);
    require(g_viewMode == (ViewMode)0x77 && g_directorEventDeadline == 180,
            "scheduleTimedEvent stores key and frame-rate-scaled deadline");
    g_directorEventDeadline = -1;
    scheduleEventCheck((ViewMode)0x55, 3);
    require(g_viewTargetObj == 0 && g_directorEventDeadline == -1,
            "scheduleEventCheck rejects priorities above director mode");
    scheduleEventCheck((ViewMode)0x55, 2);
    require(g_viewTargetObj == 0x55 && g_viewMode == (ViewMode)0x89 &&
                g_directorEventDeadline == frameTick + 4 * g_frameRateScaling,
            "scheduleEventCheck schedules a mode-2 director event");
    g_directorMode = 1;
    g_directorEventDeadline = -1;
    scheduleEventCheck(0x66, 1);
    require(g_viewTargetObj == 0x66 &&
                g_directorEventDeadline == frameTick + 3 * g_frameRateScaling,
            "scheduleEventCheck schedules a mode-1 director event with shorter delay");
    const int pendingDeadline = g_directorEventDeadline;
    scheduleEventCheck(0x77, 1);
    require(g_viewTargetObj == 0x66 && g_directorEventDeadline == pendingDeadline,
            "scheduleEventCheck ignores requests while a director event is pending");

    // --- placeString target-name compose + truncation (egframe) -------------
    // Secondary name is read through the documented stride-8 alias, so
    // planes[0].secondaryNameIndex feeds placeString(1)'s "at <base>" clause.
    resetGameplayState();
    g_targetNameTable[2] = const_cast<char *>("Long Primary Radar Site");
    g_targetNameTable[3] = const_cast<char *>("Coastal Airbase");
    g_planeTable.planes[1].nameIndex = 2;
    g_planeTable.planes[0].secondaryNameIndex = 3;
    g_strTruncTerm[0] = 'X';
    placeString(1);
    require(std::strcmp(strBuf, "Long Primary Radar Site at Coastal Airbase") == 0,
            "placeString combines primary and secondary target names");
    require(g_strTruncDot == '.' && g_strTruncTerm[0] == 0,
            "placeString marks long target strings for truncation");

    // --- finalizeMission debrief handoff (egframe) --------------------------
    // Regression guard for the fixed byte-offset poke bug: worldX/worldY (which
    // enbrief.c reads for the debrief map grid) must survive, not get clobbered.
    struct GameComm comm = {};
    struct Game game = {};
    commData = &comm;
    gameData = &game;
    resetGameplayState();
    comm = {};
    g_viewX_ = 0x1234;
    g_viewY_ = 0x5678;
    g_bombDamageMask = 0x1357;
    g_gunHits = 0x2468;
    g_finalThreatScore = 77;
    g_resupplyCount = 5;
    finalizeMission(0);
    require(g_missionEndedFlag[0] == 1 && comm.bailoutSurvived == 0 && comm.landingType == 3,
            "finalizeMission records a safe-landing outcome");
    require(comm.worldX == (uint16)g_viewX_ && comm.worldY == (uint16)g_viewY_,
            "finalizeMission preserves the player end position for the debrief grid");
    require(comm.weaponCount[0] == g_finalThreatScore && comm.weaponCount[1] == g_resupplyCount,
            "finalizeMission stores threat/resupply counters in weaponCount slots");
    require(comm.gunHits == g_gunHits, "finalizeMission stores the gun-hit count");
    require(g_eventLogCount == 1 && g_replayLog.events[0].type == 8,
            "finalizeMission appends the mission-end replay event");

    resetGameplayState();
    comm = {};
    finalizeMission(2);
    require(comm.landingType == 1 && comm.bailoutSurvived == 2,
            "finalizeMission records a crash for a nonzero non-eject result");

    resetGameplayState();
    comm = {};
    g_ejectState = 1;
    finalizeMission(0);
    require(comm.landingType == 2 && comm.bailoutSurvived == 0,
            "finalizeMission records an ejected landing type when eject is active");
    resetGameplayState();
    comm = {};
    g_ejectState = 1;
    finalizeMission(1);
    require(g_missionEndedFlag[0] == 0 && comm.landingType == 0 && g_eventLogCount == 0,
            "finalizeMission ignores nonzero outcomes after ejection started");

    // --- buildRangeString km fraction (egtarget) ----------------------------
    resetGameplayState();
    buildRangeString((12 << 6) + 40); // (40*2)/13 == 6
    require(std::strcmp(strBuf, "Range 12.6 km") == 0,
            "buildRangeString preserves the integer km fraction conversion");

    // --- applyGravityFall wreck physics (egframe) ---------------------------
    resetGameplayState();
    g_wreckAlt = 1000;
    g_wreckFallVel = 0;
    applyGravityFall();
    require(g_wreckAlt == 988 && g_wreckFallVel == -12,
            "applyGravityFall accelerates the wreck downward while above ground");
    applyGravityFall();
    applyGravityFall();
    require(g_wreckFallVel == -24,
            "applyGravityFall clamps to terminal fall velocity");
    g_wreckAlt = 0;
    const int16 fellVel = g_wreckFallVel;
    applyGravityFall();
    require(g_wreckAlt == 0 && g_wreckFallVel == fellVel,
            "applyGravityFall ignores wrecks already at ground level");

    // --- tickMessageTimers ttl countdown (egframe) --------------------------
    resetGameplayState();
    mapEvents[0].ttl = 2;
    mapEvents[0].type = 5;
    mapEvents[1].ttl = 1;
    mapEvents[1].type = 9;
    tickMessageTimers();
    require(mapEvents[0].ttl == 1 && mapEvents[0].type == 5,
            "tickMessageTimers decrements live events");
    require(mapEvents[1].ttl == 0 && mapEvents[1].type == 0,
            "tickMessageTimers clears the event type at zero ttl");

    // --- resetSimObjectLocks (egframe) --------------------------------------
    resetGameplayState();
    g_trackedEnemyIdx = 5;
    resetSimObjectLocks();
    require(g_trackedEnemyIdx == -1, "resetSimObjectLocks clears the tracked-enemy sentinel");

    // --- recalcTimeScale / exitSlowMotion timing (egkeys) -------------------
    resetGameplayState();
    g_frameRateScaling = 60;
    g_slowMotionMode = 0;
    recalcTimeScale();
    require(g_frameSyncWait == 3 && g_frameRateScaling == 15 && g_bulletTrackCount == 16,
            "recalcTimeScale clamps frame-rate scaling and derives frame-sync wait");
    require(g_threatTimerInit == 250 * 15 && g_threatDisplayTtl == 200 * 15,
            "recalcTimeScale scales the threat timers by frame-rate scaling");

    resetGameplayState();
    g_slowMotionMode = 2;
    g_frameRateScaling = 8;
    exitSlowMotion();
    require(g_slowMotionMode == 1 && g_frameRateScaling == 8,
            "exitSlowMotion leaves ACCEL mode without touching frame-rate scaling "
            "(render/sim decouple: ACCEL scales the wall-clock step rate instead)");
    exitSlowMotion();
    require(g_slowMotionMode == 1 && g_frameRateScaling == 8,
            "exitSlowMotion is a no-op when not in mode 2");

    // --- setupLodDistances thresholds (egkeys) ------------------------------
    resetGameplayState();
    g_detailLevel = 1;
    g_lodDistBase = 0x800;
    g_lodDistScale = 0x1000;
    setupLodDistances();
    require(g_lodDistNear == 0x1800 && g_lodDistFar == 0x2000,
            "setupLodDistances derives near/far distances from the scale/base");
    require(((int16 *)(colorLut + 0x10))[0] == 0x40 && ((int16 *)(colorLut + 0x10))[5] == 0x800,
            "setupLodDistances writes the per-LOD distance table");
    require(*(int16 *)(colorLut + 0x20) == 0x1A0A,
            "setupLodDistances writes the object-cull distance");
    // Detail level >= 4 ("extended"): no distance culling, everything keeps
    // its finest model.
    g_detailLevel = 4;
    setupLodDistances();
    require(((int16 *)(colorLut + 0x10))[0] == 0x7fff && *(int16 *)(colorLut + 0x20) == 0x7fff,
            "setupLodDistances lifts all cull distances at the extended detail level");

    std::cout << "gameplay_behavior_tests passed\n";
    return 0;
}
