#ifndef F15_SE2_ENDATA
#define F15_SE2_ENDATA
/* Public data globals defined in endata.c (debrief state, strings, sprites). */
#include "inttype.h"
#include "struct.h"
#include "comm.h"
#include "endtypes.h"
#include <dos.h>
#include <stdio.h>

typedef struct SDL_IOStream SDL_IOStream;

extern unsigned char timerCounter;
extern int16 lineX1;
extern int16 lineY1;
extern int16 lineX2;
extern int16 lineY2;
extern int16 clipMaxX;
extern int16 clipMaxY;
extern char popupVisible;
extern char blinkMarker; /* debriefPresent blinks the current-event map marker */
/* How far the debrief flight path currently extends: the highest flightRecords
 * index revealed (review), ALL_RECORDS for the full mission-summary path, or 0
 * when the map holds no path. debriefPresent redraws the map, path and markers
 * up to this record every frame. */
extern int pathExtent;
extern int32 missionScore;
extern int popupX;
extern int popupY;
extern char scoreString[];
extern TargetBlock targetBlock;
extern int16 randSeed;
extern int16 randState;
extern const int mapViewX1;
extern const int mapViewY1;
extern const int mapViewX2;
extern const int mapViewY2;
extern uint16 cursorX;
extern uint16 cursorY;
extern struct GameComm far *commData;
extern uint8 quitFlag;
extern unsigned int *colorTablePtr;
extern char inputChanged;
extern int colorAnimIdx;
extern int colorAnimEnabled;
extern uint8 joyRepeatFlag;
extern char spriteToggle;
extern char animDone;
extern uint8 timerCounter2;
extern uint8 timerCounter3;
extern char enterPressed;
extern int colorStyleTable[];
extern struct SpriteParams *spriteAirBlink;
extern struct SpriteParams *spriteSamBlink;
extern struct SpriteParams *spriteGroundBlink;
extern struct SpriteParams *spriteWaypointBlink;
extern int curRecordIdx;
extern char slotInfoTable[]; /* slot info table, 16 bytes per slot */
extern char ejectedFlag;
extern const int popupSpriteY[];
extern const int popupSpriteX[];
extern int g_dbiconsBuf; /* sprite buffer holding the decoded dbicons.spr popup icon sheet */
extern struct SpriteParams *spriteAir;
extern struct SpriteParams *spriteSam;
extern struct SpriteParams *spriteGround;
extern struct SpriteParams *spriteWaypoint;
extern struct SpriteParams spriteMapAreaDef;
extern struct SpriteParams spriteStatusBarDef;
extern struct SpriteParams spriteAirDef;
extern struct SpriteParams spriteAirBlinkDef;
extern struct SpriteParams spriteSamDef;
extern struct SpriteParams spriteSamBlinkDef;
extern struct SpriteParams spriteGroundDef;
extern struct SpriteParams spriteGroundBlinkDef;
extern struct SpriteParams spriteBombDef;
extern struct SpriteParams spriteBombBlinkDef;
extern struct SpriteParams spriteWaypointDef;
extern struct SpriteParams spriteWaypointBlinkDef;
extern int16 *debriefPage;
extern int16 *debriefPage2;
extern struct SpriteParams *spriteMapArea;
extern struct SpriteParams *spriteStatusBar;
extern int16 *cursorBoundsPtr;
extern const char *theaterSprFiles[];
extern const char *debriefMenuStrings[];
extern MenuItem debriefMenuItems[];
extern char *worldStrings[];
extern char worldStringBuf[];
extern SDL_IOStream *worldBufHandle;
extern struct WorldObject worldObjects[];
extern int16 worldWaypointCount;
extern uint8 worldRouteTable[];
extern int16 worldRouteCount;
extern uint8 worldSamTable[];
extern uint8 worldUnitFlags[];
extern int16 worldGridSize;
extern uint8 worldMiscHeader[];
extern struct WeaponDataBlock weaponDataBlock;
extern uint16 worldObjectCount;
extern uint16 worldSamCount;
extern int totalFlightRecords;

/* The flight recording read back from the COMM block is one contiguous 0x600
 * block. The DOS build viewed it as a 2-byte time head plus a FlightRecord
 * array and relied on the linker placing two separate globals back to back;
 * that adjacency is not guaranteed, so a single buffer backs both views.
 * flightTimeTable is the +0 (timestamp) view, flightRecords the +2
 * (FlightRecord) view — note record i's timestamp word is the two bytes just
 * before record i, i.e. flightTimeTable[i*3] == (int*)&flightRecords[i] - 1
 * (see enbrief.c). The views are constant-address expressions into one fixed
 * buffer, so they compile to the same direct accesses as before. */
extern uint8 flightDataBuf[0x600];
#define flightTimeTable ((int16 *)flightDataBuf)
#define flightRecords ((FlightRecord *)(flightDataBuf + 2))
extern struct PageDesc awardPageDesc;
extern int16 *awardPage;
extern char textBuf[];
extern const char *rankNames[];
extern const long promoThresholds[];
extern const char *medalNames[];
extern const long medalThresholds[];
extern int secondaryHit;
extern int primaryHit;
extern int airMissed;
extern int airKilled;
extern int groundMissed;
extern int samMissed;
extern int groundKilled;
extern int samKilled;
extern char unitTypeTable[];
extern uint8 gridFlags[];
extern struct Game far *gameData;
extern char hercFlag;
extern uint8 joyAxisY;
extern uint8 joyAxisX;
extern int spriteBufSeg;
extern int missionResult;
extern int selectedMenuItem;

/* Named views into weaponDataBlock[] (offsets documented in endata.c):
 *   planeArray     +0x14E  SamDataEntry[] — 8-byte prefix then aircraft name
 *   samWeaponTable +0x3B6  Sam[]          — SAM/missile entries
 *   nightMission   +0x6DA  int16          — night-mission flag
 */
enum WeaponDataOffset {
    WEAPON_DATA_PLANE_ARRAY_OFFSET = 0x14E,
    WEAPON_DATA_SAM_WEAPON_TABLE_OFFSET = 0x3B6,
    WEAPON_DATA_NIGHT_MISSION_OFFSET = 0x6DA
};
#define planeArray ((struct SamDataEntry *)((unsigned char *)&weaponDataBlock + WEAPON_DATA_PLANE_ARRAY_OFFSET))
#define samWeaponTable ((struct Sam *)((unsigned char *)&weaponDataBlock + WEAPON_DATA_SAM_WEAPON_TABLE_OFFSET))
#define nightMission (*(int16 *)((unsigned char *)&weaponDataBlock + WEAPON_DATA_NIGHT_MISSION_OFFSET))

/* Award screen page descriptor views (awardPageDesc defined in endata.c) */
#define awardFont (awardPageDesc.font)
#define awardColor (awardPageDesc.color)

#endif /* F15_SE2_ENDATA */
