// Utility behavior tests (LINK_CORE — exercises the real game code).
//
// Scope: deterministic, side-effect-free helper logic — terrain LOD scaling,
// DOS 15-bit rand scaling, integer formatting, string helpers, debrief
// map/scoring math — plus a worldxfer import->export round-trip.
// (approxDistance/calcBearing/clampValue are covered by start_behavior_tests.)

#include "endata.h"    /* pulls struct.h, comm.h, endtypes.h */
#include "worldxfer.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <tuple>

extern int randMul(uint16 arg);
extern uint32 scaleCoordByLevel(int level, uint32 coord);
extern void seedRandom();
extern int getTimeOfDay(void);
extern void my_ltoa(int32 value, char *buf);
extern void my_itoa(int value, char *buf);
extern void mystrcpy(char *dest, const char *source);
extern void mystrcat(char *dst, const char *src);
extern int mystrlen(const char *s);
extern void nearmemset(void *dst, char val, int count);
extern void strcpyFromDot(char *dst, const char *src);
extern char *formatFlightTime(int timeValue, char *buffer);
extern int mapToScreenX(unsigned char mapCoord);
extern int mapToScreenY(unsigned char mapCoord);
extern void computeMissionResult(void);
extern int isPointInRect(const MenuItem *p);
extern long calcMissionScore(int param);

/* START / shared and EGAME globals touched by the worldxfer seams (not exposed
 * by endata.h; declared as in worldxfer.c). */
extern struct WorldObject worldObjects[];
extern int readItemSize;
extern int16 groundUnitCount;
extern int flightUnitCount;
extern uint8 wldReadBuf1[];
extern uint8 wldReadBuf7[];
extern uint8 wldReadBuf8[];
extern char wldReadBuf11[];
extern char terrainGrid[256];
extern struct GroundTargetTable g_planeTable;
extern int16 g_planeCount;
extern int16 g_planeScanCount;

namespace {

enum UtilityConstant : int {
    kRandomScaleShift = 15,
    kOneGameHourTicks = 1800,
    kOneGameMinuteTicks = 30,
    // calcMissionScore weights (byte-identical to the debrief scorer).
    kMaxScoredWeaponCount = 15,
    kScoreAirFactor = 25,
    kScoreSamFactor = 50,
    kScoreGroundFactor = 20,
    kScorePrimaryFactor = 200,
    kScoreSecondaryFactor = 100,
    kScoreEjectNum = 3,
    kScoreEjectDen = 4,
    kGroundClearX = 0x20,
    kGroundClearY = 0x30,
    kGroundBlockedX = 0x40,
    kGroundBlockedY = 0x40,
    // computeMissionResult world-grid lookup.
    kWorldGridShift = 11,
    kWorldGridWidth = 16,
    kMissionResultMask = 3,
    kMissionGridX = 5,
    kMissionGridY = 6,
    kMissionGridFlags = 0x86,
    // worldxfer round-trip fixture.
    kCategoryBytes = 100,
    kKillTallyBytes = 100,
    kStringPoolBytes = 750,
    kGridBytes = 0x100,
    kPlaneCount = 4,
    kWorldObjectCount = 42,
    kGroundUnitCount = 7,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

int gridIndexForRecord(unsigned char mapX, unsigned char mapY) {
    return (((mapY & 0xff) >> 4) << 4) + (mapX >> 4);
}

/* Zero the debrief globals calcMissionScore / computeMissionResult read. */
void resetDebriefState(struct GameComm &comm, struct Game &game) {
    std::memset(&comm, 0, sizeof(comm));
    std::memset(&game, 0, sizeof(game));
    std::memset(flightDataBuf, 0, sizeof(flightDataBuf));
    std::memset(unitTypeTable, 0, kCategoryBytes);
    std::memset(slotInfoTable, 0, 1194);
    std::memset(gridFlags, 0, kGridBytes);
    std::memset(&weaponDataBlock, 0, sizeof(weaponDataBlock));
    commData = &comm;
    gameData = &game;
    missionResult = 0;
    cursorX = 0;
    cursorY = 0;
}

} // namespace

int main() {
    // --- terrain LOD coordinate scaling -------------------------------------
    for (auto [level, coord, expected] : {std::tuple{4, 0x10000u, 0x400u},
                                          std::tuple{3, 0x10000u, 0x1000u},
                                          std::tuple{2, 0x10000u, 0x4000u},
                                          std::tuple{1, 0x10000u, 0x10000u},
                                          std::tuple{0, 0x10000u, 0x20000u},
                                          std::tuple{-1, 0x1234u, 0x2468u}}) {
        require(scaleCoordByLevel(level, coord) == expected,
                "scaleCoordByLevel matches original terrain LOD shifts");
    }

    // --- DOS 15-bit rand scaling --------------------------------------------
    for (int maxVal : {100, 2000, 1}) {
        std::srand(1234);
        const int hostRand = std::rand() & 0x7fff;
        std::srand(1234);
        const int scaled = randMul(static_cast<uint16>(maxVal));
        require(scaled == static_cast<int>((static_cast<long>(hostRand) * maxVal) >> kRandomScaleShift),
                "randMul scales the DOS 15-bit rand output");
        require(scaled >= 0 && scaled <= maxVal, "randMul result stays in range");
    }

    // seedRandom seeds libc rand from getTimeOfDay (wall-clock, 1s resolution).
    {
        const int before = getTimeOfDay();
        seedRandom();
        const int viaHelper = std::rand();
        if (before == getTimeOfDay()) { /* no 1s tick crossed mid-check */
            std::srand(before);
            require(viaHelper == std::rand(),
                    "seedRandom seeds libc rand from the getTimeOfDay source");
        }
    }

    // --- integer formatting --------------------------------------------------
    char buf[32];
    my_itoa(0, buf);
    require(std::strcmp(buf, "0") == 0, "my_itoa formats zero");
    my_itoa(123, buf);
    require(std::strcmp(buf, "123") == 0, "my_itoa formats small positive values");
    my_itoa(1234, buf);
    require(std::strcmp(buf, "1,234") == 0, "my_itoa inserts thousands comma");
    my_itoa(-1234, buf);
    require(std::strcmp(buf, "-1,234") == 0, "my_itoa preserves negative sign");

    my_ltoa(99999, buf);
    require(std::strcmp(buf, "99,999") == 0, "my_ltoa formats five digits with comma");
    my_ltoa(-42, buf);
    require(std::strcmp(buf, "-42") == 0, "my_ltoa formats negative values");

    // --- string helpers ------------------------------------------------------
    char text[32] = "F15";
    mystrcat(text, " SE2");
    require(std::strcmp(text, "F15 SE2") == 0, "mystrcat appends like strcat");
    require(mystrlen(text) == 7, "mystrlen counts bytes like strlen");

    char copied[16] = {};
    mystrcpy(copied, "VIPER");
    require(std::strcmp(copied, "VIPER") == 0, "mystrcpy copies through the terminating nul");

    unsigned char memory[] = {1, 2, 3, 4, 5};
    nearmemset(&memory[1], static_cast<char>(0x5A), 3);
    require(memory[0] == 1 && memory[1] == 0x5A && memory[2] == 0x5A &&
                memory[3] == 0x5A && memory[4] == 5,
            "nearmemset writes exactly the requested byte count");

    char filenameWithDot[] = "IRAN.XXX";
    strcpyFromDot(filenameWithDot, ".3D3");
    require(std::strcmp(filenameWithDot, "IRAN.3D3") == 0,
            "strcpyFromDot replaces from the first dot");
    char filenameWithoutDot[12] = "DESERT";
    strcpyFromDot(filenameWithoutDot, ".3dG");
    require(std::strcmp(filenameWithoutDot, "DESERT.3dG") == 0,
            "strcpyFromDot appends when no dot is present");
    char filenameStartingDot[] = ".OLD";
    strcpyFromDot(filenameStartingDot, ".3dT");
    require(std::strcmp(filenameStartingDot, ".3dT") == 0,
            "strcpyFromDot handles filenames that start at the extension");

    // --- debrief flight-time clock (night/day inference from target block) ---
    char timeBuf[16];
    std::memset(&targetBlock, 0, sizeof(targetBlock));
    require(formatFlightTime(0, timeBuf) == timeBuf, "formatFlightTime returns its caller buffer");
    require(std::strcmp(timeBuf, "20:00:00") == 0,
            "formatFlightTime infers night clock when low target misc bits are zero");

    std::memset(&targetBlock, 0, sizeof(targetBlock));
    targetBlock.target1Type[0] = 1;
    formatFlightTime(kOneGameHourTicks + kOneGameMinuteTicks, timeBuf);
    require(std::strcmp(timeBuf, "11:01:00") == 0,
            "formatFlightTime forces day clock for target type 1");

    std::memset(&targetBlock, 0, sizeof(targetBlock));
    targetBlock.target2Type[0] = 4;
    formatFlightTime(2, timeBuf);
    require(std::strcmp(timeBuf, "20:00:04") == 0,
            "formatFlightTime forces night clock for target type 4");

    std::memset(&targetBlock, 0, sizeof(targetBlock));
    targetBlock.target1MiscBits[0] = 3;
    targetBlock.target2MiscBits[0] = 2;
    formatFlightTime(0, timeBuf);
    require(std::strcmp(timeBuf, "10:42:40") == 0,
            "formatFlightTime adds the low-nibble target time offset");

    // --- map coordinate scaling ---------------------------------------------
    for (unsigned char coord : {static_cast<unsigned char>(0), static_cast<unsigned char>(1),
                                static_cast<unsigned char>(127), static_cast<unsigned char>(255)}) {
        require(mapToScreenX(coord) == ((static_cast<unsigned int>(coord) << 7) / MAP_SCALE_X),
                "mapToScreenX preserves original integer scaling");
        require(mapToScreenY(coord) == ((static_cast<unsigned int>(coord) << 7) / MAP_SCALE_Y),
                "mapToScreenY preserves original integer scaling");
    }

    struct GameComm comm;
    struct Game game;

    // --- computeMissionResult: 16-column world-grid lookup + low-bit mask ----
    resetDebriefState(comm, game);
    comm.worldX = kMissionGridX << kWorldGridShift;
    comm.worldY = kMissionGridY << kWorldGridShift;
    gridFlags[kMissionGridX + kMissionGridY * kWorldGridWidth] = kMissionGridFlags;
    computeMissionResult();
    require(missionResult == (kMissionGridFlags & kMissionResultMask),
            "computeMissionResult preserves the world-grid lookup and low-bit mask");

    // --- isPointInRect: inclusive hit rectangle ------------------------------
    resetDebriefState(comm, game);
    MenuItem hitItem = {};
    hitItem.hitX1 = 30;
    hitItem.hitX2 = 50;
    hitItem.hitY1 = 20;
    hitItem.hitY2 = 40;
    cursorX = 42;
    cursorY = 27;
    require(isPointInRect(&hitItem) == 1, "isPointInRect includes points inside the rectangle");
    cursorX = 75;
    require(isPointInRect(&hitItem) == 0, "isPointInRect rejects points outside the rectangle");

    // --- calcMissionScore: counters, weapon cap, waypoint scale, eject penalty
    resetDebriefState(comm, game);
    comm.weaponCount[0] = 20;
    comm.landingType = LANDING_EJECTED;
    game.difficulty = 2;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[1].status = EVENT_AIR_KILL | STATUS_PRIMARY_HIT;
    flightRecords[2].status = EVENT_AIR_KILL;
    flightRecords[2].unitId = 4;
    flightRecords[3].status = EVENT_AIR_KILL2;
    flightRecords[3].unitId = 5;
    unitTypeTable[5] = 0x40;
    flightRecords[4].status = EVENT_SAM_KILL;
    flightRecords[4].unitId = 6;
    planeArray[7].validFlag = -1;
    flightRecords[5].status = EVENT_SAM_KILL | STATUS_SECONDARY_HIT;
    flightRecords[6].status = EVENT_GROUND_KILL;
    flightRecords[6].mapX = static_cast<char>(kGroundClearX);
    flightRecords[6].mapY = static_cast<char>(kGroundClearY);
    flightRecords[7].status = EVENT_GROUND_KILL;
    flightRecords[7].mapX = static_cast<char>(kGroundBlockedX);
    flightRecords[7].mapY = static_cast<char>(kGroundBlockedY);
    gridFlags[gridIndexForRecord(kGroundBlockedX, kGroundBlockedY)] = 1;
    flightRecords[8].status = EVENT_EJECTED;
    const long rawScore =
        ((2 - 1 * 2) * kMaxScoredWeaponCount * kScoreAirFactor) +
        ((1 - 1 * 2) * (game.difficulty + 1) * kScoreSamFactor) +
        ((1 - 1 * 2) * kMaxScoredWeaponCount * kScoreGroundFactor) +
        (kMaxScoredWeaponCount * kScorePrimaryFactor) +
        (kMaxScoredWeaponCount * kScoreSecondaryFactor);
    const long waypointScaled = rawScore * 2 / 3;
    require(calcMissionScore(8) == waypointScaled * kScoreEjectNum / kScoreEjectDen,
            "calcMissionScore preserves counters, weapon cap, waypoint scale, and eject penalty");
    require(primaryHit == 1 && secondaryHit == 1 && airKilled == 2 && airMissed == 1 &&
                samKilled == 1 && samMissed == 1 && groundKilled == 1 && groundMissed == 1,
            "calcMissionScore updates the debrief hit/miss counters");

    resetDebriefState(comm, game);
    comm.weaponCount[0] = 1;
    comm.landingType = LANDING_CRASHED;
    flightRecords[0].status = EVENT_AIR_KILL;
    flightRecords[0].unitId = 3;
    unitTypeTable[3] = 0x40;
    flightRecords[1].status = EVENT_EJECTED;
    require(calcMissionScore(1) == 0, "calcMissionScore floors negative ejected scores before crash penalty");

    resetDebriefState(comm, game);
    comm.weaponCount[0] = 2;
    game.difficulty = 0;
    flightRecords[0].status = EVENT_AIR_KILL | STATUS_SECONDARY_HIT;
    flightRecords[1].status = EVENT_AIR_KILL;
    flightRecords[1].unitId = 1;
    *reinterpret_cast<int *>(&slotInfoTable[1 * 16]) = 0x500;
    flightRecords[2].status = EVENT_SAM_KILL;
    flightRecords[2].unitId = 2;
    planeArray[3].validFlag = 0;
    flightRecords[3].status = EVENT_GROUND_KILL | STATUS_SECONDARY_HIT;
    flightRecords[4].status = EVENT_GROUND_KILL;
    flightRecords[4].unitId = 4;
    unitTypeTable[4] = 0x40;
    calcMissionScore(4);
    require(secondaryHit == 1 && airKilled == 1 && airMissed == 1 && samKilled == 1 &&
                groundKilled == 1 && groundMissed == 1,
            "calcMissionScore preserves secondary/slot-miss/normal-SAM/ground-miss classifications");

    // --- worldxfer round-trip (replaces the deleted worldBuf byte cursor) ----
    // START/shared globals -> EGAME g_* (worldImportToEgame) -> END globals
    // (worldExportToEnd). worldObjects is the shared START/END array, so an
    // import→export cycle over it must recover every plane field, including the
    // deliberate +2-byte-shifted name-index aliasing (nameIndexLead /
    // secondaryNameIndex). The bulk buffers pass through the g_* views unchanged.
    {
        std::memset(worldObjects, 0, kPlaneCount * sizeof(struct WorldObject));
        struct WorldObject orig[kPlaneCount];
        for (int i = 0; i < kPlaneCount; i++) {
            const uint16 base = static_cast<uint16>((i + 1) * 0x100);
            worldObjects[i].unitRef = static_cast<uint16>(base | 1);
            worldObjects[i].x_coord = static_cast<uint16>(base | 2);
            worldObjects[i].y_coord = static_cast<uint16>(base | 3);
            worldObjects[i].unitType = static_cast<int16>(base | 4);
            worldObjects[i].targetFlags = static_cast<int16>(base | 5);
            worldObjects[i].occupantType = static_cast<int16>(base | 6);
            worldObjects[i].patrolCount = static_cast<int16>(base | 7);
            worldObjects[i].objectIdx = static_cast<int16>(base | 8);
            orig[i] = worldObjects[i];
        }
        readItemSize = kPlaneCount;
        worldObjectCount = kWorldObjectCount;
        groundUnitCount = kGroundUnitCount;
        flightUnitCount = 0;
        wldReadBuf1[0] = 0x34;
        wldReadBuf1[1] = 0x12;

        for (int i = 0; i < kCategoryBytes; i++) wldReadBuf7[i] = static_cast<uint8>(0x40 + (i % 7));
        for (int i = 0; i < kKillTallyBytes; i++) wldReadBuf8[i] = static_cast<uint8>(0x80 + (i % 5));
        for (int i = 0; i < kStringPoolBytes; i++) wldReadBuf11[i] = static_cast<char>(1 + (i % 9));
        for (int i = 0; i < kGridBytes; i++) terrainGrid[i] = static_cast<char>(i % 13);

        worldImportToEgame();

        require(g_planeCount == kPlaneCount && g_planeScanCount == kWorldObjectCount,
                "worldImportToEgame carries plane and scan counts into EGAME");
        require(g_planeTable.nameIndexLead == static_cast<int16>(orig[0].unitRef),
                "worldImportToEgame seeds the +2-shift lead word from the first unitRef");
        for (int i = 0; i < kPlaneCount; i++) {
            require(g_planeTable.planes[i].mapX == orig[i].x_coord &&
                        g_planeTable.planes[i].mapY == orig[i].y_coord &&
                        g_planeTable.planes[i].nameIndex == orig[i].objectIdx,
                    "worldImportToEgame aligns MapTarget columns with WorldObject");
            const int16 expectSecondary =
                (i + 1 < kPlaneCount) ? static_cast<int16>(orig[i + 1].unitRef) : 0;
            require(g_planeTable.planes[i].secondaryNameIndex == expectSecondary,
                    "worldImportToEgame threads the next unitRef into secondaryNameIndex");
        }

        // Clear the shared array to prove the export path repopulates it.
        std::memset(worldObjects, 0, kPlaneCount * sizeof(struct WorldObject));
        worldExportToEnd();

        require(worldObjectCount == kPlaneCount && worldRouteCount == kWorldObjectCount,
                "worldExportToEnd republishes plane count and scan count to END");
        require(worldWaypointCount == 0x1234,
                "worldExportToEnd recomposes the packed waypoint count word");
        for (int i = 0; i < kPlaneCount; i++) {
            require(worldObjects[i].unitRef == orig[i].unitRef &&
                        worldObjects[i].x_coord == orig[i].x_coord &&
                        worldObjects[i].y_coord == orig[i].y_coord &&
                        worldObjects[i].unitType == orig[i].unitType &&
                        worldObjects[i].targetFlags == orig[i].targetFlags &&
                        worldObjects[i].occupantType == orig[i].occupantType &&
                        worldObjects[i].patrolCount == orig[i].patrolCount &&
                        worldObjects[i].objectIdx == orig[i].objectIdx,
                    "worldxfer import->export recovers every WorldObject field, incl. shifted unitRef");
        }

        require(std::memcmp(unitTypeTable, wldReadBuf7, kCategoryBytes) == 0,
                "worldxfer carries the unit category table through g_shapeTargetCategory");
        require(std::memcmp(worldUnitFlags, wldReadBuf8, kKillTallyBytes) == 0,
                "worldxfer carries the kill-tally table through g_tileKillTally");
        require(std::memcmp(worldStringBuf, wldReadBuf11, kStringPoolBytes) == 0,
                "worldxfer carries the string pool through g_stringPool");
        require(std::memcmp(gridFlags, terrainGrid, kGridBytes) == 0,
                "worldxfer carries the map grid through g_mapCellFlags");
    }

    std::cout << "utility_behavior_tests passed\n";
    return 0;
}
