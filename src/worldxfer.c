/* worldxfer.c — mission world-data hand-off between START, EGAME and END.
 *
 * The three former DOS EXEs are one process now, so their globals persist and can
 * be converted directly instead of marshalled through the old commData->worldBuf
 * byte cursor. Each module keeps its own typed view of the same records; the two
 * seams below are explicit typed conversions between those views.
 *
 * The plane table is the one non-trivial case: START/END describe it as
 * `struct WorldObject` while EGAME plays it as `struct MapTarget`, and EGAME's
 * table has a 2-byte `nameIndexLead` word before `planes[]`. The original loaded
 * the WorldObject block straight into that buffer starting at the lead word, so
 * every MapTarget is shifted +2 bytes relative to its WorldObject — which is
 * exactly what lines the columns up (x_coord->mapX, objectIdx->nameIndex, and the
 * NEXT object's unitRef -> this plane's secondaryNameIndex). occupantType and
 * patrolCount are carried into the alertLevel/threatTimer slots (EGAME does not
 * re-zero them). Reproduced here as explicit field moves rather than a byte copy. */

#include <string.h>
#include "inttype.h"
#include "struct.h"
#include "endtypes.h"

/* ---- START / shared source globals (defined in stdata.c) ---- */
extern struct WorldObject worldObjects[]; /* shared START/END plane block */
extern struct FlightUnit flightUnits[];
extern struct Target targets[];
extern char terrainGrid[256];
extern uint8 wldReadBuf1[];
extern uint8 wldReadBuf7[];
extern uint8 wldReadBuf8[];
extern char wldReadBuf11[];
extern int readItemSize;
extern int16 groundUnitCount;
extern uint16 worldObjectCount; /* shared START/END */
extern int flightUnitCount;
extern int16 missionDistAccum;
extern int16 escortMissionFlag;
extern int16 missionMidX;
extern int16 missionMidY;
extern unsigned int missionTargetX;
extern unsigned int missionTargetY;
extern int16 missionTarget2X;
extern int16 missionTarget2Y;
extern int16 missionBase2X;
extern int16 missionBase2Y;

/* ---- EGAME live gameplay globals (defined in egdata.c) ---- */
extern uint8 g_landTargetId[];
extern uint8 g_waterTargetId[];
extern int16 g_planeCount;
extern int16 g_targetEntityCount;
extern int16 g_planeScanCount;
extern struct GroundTargetTable g_planeTable;
extern int16 g_groundUnitCount;
extern struct SimObject g_simObjects[];
extern uint8 g_shapeTargetCategory[];
extern uint8 g_tileKillTally[];
extern char g_stringPool[];
extern uint8 g_mapCellFlags[];
extern int16 g_unusedSavedWord;
extern int16 g_padlockAircraft;
extern struct Waypoint waypoints[];
extern struct TargetSlot g_targetSlots[];
extern struct ReplayLog g_replayLog;

/* ---- END debrief globals (defined in endata.c) ---- */
extern int16 worldWaypointCount;
extern uint8 worldRouteTable[];
extern int16 worldRouteCount;
extern uint16 worldSamCount;
extern uint8 worldSamTable[];
extern char unitTypeTable[];
extern uint8 worldUnitFlags[];
extern char worldStringBuf[];
extern uint8 gridFlags[];
extern int16 worldGridSize;
extern uint8 worldMiscHeader[];
extern struct WeaponDataBlock weaponDataBlock;
extern TargetBlock targetBlock;
extern uint8 flightDataBuf[0x600];

#define CATEGORY_BYTES 100 /* BUF7SIZE */
#define KILLTALLY_BYTES 100
#define STRINGPOOL_BYTES 750
#define GRIDFLAGS_BYTES 0x100
#define REPLAYLOG_BYTES 0x600

/* START/shared globals -> EGAME g_*. Called once at mission start (was START's
 * exportWorldToComm + EGAME's moveStuff load). Destinations that DOS re-zeroed
 * for free on each EXE reload are cleared here (the plane table). */
void worldImportToEgame(void) {
    int i;
    int planeCnt = readItemSize;
    int flightCnt = flightUnitCount;

    g_landTargetId[0] = wldReadBuf1[0];
    g_waterTargetId[0] = wldReadBuf1[1];
    g_planeCount = (int16)readItemSize;
    g_targetEntityCount = groundUnitCount;
    g_planeScanCount = (int16)worldObjectCount;

    memset(&g_planeTable, 0, sizeof(g_planeTable));
    if (planeCnt > 0) {
        g_planeTable.nameIndexLead = (int16)worldObjects[0].unitRef;
    }
    for (i = 0; i < planeCnt; i++) {
        struct MapTarget *p = &g_planeTable.planes[i];
        p->mapX = worldObjects[i].x_coord;
        p->mapY = worldObjects[i].y_coord;
        p->active = worldObjects[i].unitType;
        p->flags = worldObjects[i].targetFlags;
        p->alertLevel = worldObjects[i].occupantType;
        p->threatTimer = worldObjects[i].patrolCount;
        p->nameIndex = worldObjects[i].objectIdx;
        if (i + 1 < planeCnt) {
            p->secondaryNameIndex = (int16)worldObjects[i + 1].unitRef;
        }
        /* last plane's secondaryNameIndex stays 0 — the original copy stopped one
         * word short and read it out of fresh (zeroed) BSS. */
    }

    g_groundUnitCount = (int16)flightUnitCount;
    memcpy(g_simObjects, flightUnits, (size_t)flightCnt * sizeof(struct SimObject));

    memcpy(g_shapeTargetCategory, wldReadBuf7, CATEGORY_BYTES);
    memcpy(g_tileKillTally, wldReadBuf8, KILLTALLY_BYTES);
    memcpy(g_stringPool, wldReadBuf11, STRINGPOOL_BYTES);
    memcpy(g_mapCellFlags, terrainGrid, GRIDFLAGS_BYTES);

    g_unusedSavedWord = missionDistAccum;
    g_padlockAircraft = escortMissionFlag;

    waypoints[0].mapX = (uint16)missionMidX;
    waypoints[0].mapY = (uint16)missionMidY;
    waypoints[1].mapX = (uint16)missionTargetX;
    waypoints[1].mapY = (uint16)missionTargetY;
    waypoints[2].mapX = (uint16)missionTarget2X;
    waypoints[2].mapY = (uint16)missionTarget2Y;
    waypoints[3].mapX = (uint16)missionBase2X;
    waypoints[3].mapY = (uint16)missionBase2Y;

    memcpy(g_targetSlots, targets, 2 * sizeof(struct Target));
}

/* EGAME g_* -> END globals. Called once at mission end (was EGAME's moveDataFar +
 * END's readWorldData). worldObjects/worldObjectCount are the shared START/END
 * array, overwritten here with the post-mission plane state. */
void worldExportToEnd(void) {
    int i;
    int planeCnt = g_planeCount;

    worldWaypointCount = (int16)(g_landTargetId[0] | (g_waterTargetId[0] << 8));
    worldObjectCount = (uint16)g_planeCount;
    memcpy(worldRouteTable, &g_targetEntityCount, 2);
    worldRouteCount = g_planeScanCount;

    for (i = 0; i < planeCnt; i++) {
        struct MapTarget *p = &g_planeTable.planes[i];
        worldObjects[i].unitRef =
            (uint16)((i == 0) ? g_planeTable.nameIndexLead : g_planeTable.planes[i - 1].secondaryNameIndex);
        worldObjects[i].x_coord = p->mapX;
        worldObjects[i].y_coord = p->mapY;
        worldObjects[i].unitType = p->active;
        worldObjects[i].targetFlags = p->flags;
        worldObjects[i].occupantType = p->alertLevel;
        worldObjects[i].patrolCount = p->threatTimer;
        worldObjects[i].objectIdx = p->nameIndex;
    }

    worldSamCount = (uint16)g_groundUnitCount;
    memcpy(worldSamTable, g_simObjects, (size_t)g_groundUnitCount * sizeof(struct SimObject));

    memcpy(unitTypeTable, g_shapeTargetCategory, CATEGORY_BYTES);
    memcpy(worldUnitFlags, g_tileKillTally, KILLTALLY_BYTES);
    memcpy(worldStringBuf, g_stringPool, STRINGPOOL_BYTES);
    memcpy(gridFlags, g_mapCellFlags, GRIDFLAGS_BYTES);

    worldGridSize = g_unusedSavedWord;
    memcpy(worldMiscHeader, &g_padlockAircraft, 2);

    /* weaponDataBlock's first 16 bytes are the mission waypoint/coord block; the
     * rest of the block is loaded separately from disk in END. */
    memcpy(&weaponDataBlock, waypoints, 16);
    memcpy(&targetBlock, g_targetSlots, sizeof(TargetBlock));
    memcpy(flightDataBuf, g_replayLog.events, REPLAYLOG_BYTES);
}
