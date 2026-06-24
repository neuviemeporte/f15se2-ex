#ifndef COMM_H
#define COMM_H

#include "const.h"
#include "inttype.h"
#include "sassert.h"

/* the communication structure used for exchanging data between the parts of the game has 320 paragraphs, 5120 bytes, 0x1400 */
#define COMM_SIZE_PARA 0x140

/* Size of the world-export scratch area that START fills and END reads back */
#define COMM_WORLDBUF_SIZE 0x1194

#pragma pack(1)
struct GameComm {
    char gfxOvlName[FILENAME_LENGTH];
    char sndOvlName[FILENAME_LENGTH];
    uint16 gfxOvlAddr;
    uint16 sndOvlAddr;
    uint16 miscOvlAddr;
    int16 gfxInitResult;
    int16 startDone;
    int16 landingType;     /* 1=crashed, 2=ejected, 3=landed */
    int16 bailoutSurvived; /* 0=survived */
    int16 setup2;
    int16 restartFlag;
    int16 unk4;           /* nonzero enables crash exit in egame (set from theater table in start.exe) */
    int16 trainingFlag;   /* nonzero if the last mission was a training mission */
    int16 setupDetail;
    uint8 pad0[4];
    uint16 weaponType[4]; /* weapon type indices into weaponLoadouts[] (0=Sidewinder,1=AMRAAM,etc) */
    int16 weaponCount[4]; /* weapon quantities per slot */
    uint8 joyData[20];
    uint8 pad2[20];
    int16 setupT;
    int16 setupUseJoy;
    uint16 worldX;
    uint16 worldY;
    int16 needSplash;   /* show splash/intro on the first START pass */
    /* world-export scratch: exportWorldToComm() fills this, END reads it back */
    uint8 worldBuf[COMM_WORLDBUF_SIZE];
};
#pragma pack()

/* The shared communication record. */
extern struct GameComm *commData;

/* the communication structure contains a buffer at offset 0x120, whose contents seem to have some different purpose than config values */
#define COMM_BUFFER_OFFSET 0x120
#define COMM_GAMEDATA_OFFSET 0x120e

#pragma pack(1)
struct Game {
    uint16 pilotIdx;
    char pilotName[22];
    uint8 pad1[8];
    uint16 rank;
    uint16 medals;
    uint8 pad5[10];
    uint16 lastScore;
    uint16 rankHigh;
    int32 totalScore;
    uint8 pad2[2];
    uint16 theater;
    int16 isCampaignMission;
    uint16 missionReady;
    int16 difficulty;
    uint16 unk4; // checked in egame
    int16 rand;
    int16 hallOfFameEligible; // desk job?
    uint8 pad4[8];
    int16 campaignProgress;
};
#pragma pack()
STATIC_ASSERT(sizeof(struct Game)==80);

/* The shared Game record. */
extern struct Game *gameData;

#define GAME_PILOTIDX_OFFSET 0x0
#define GAME_PILOTNAME_OFFSET 0x2
#define GAME_RANK_OFFSET 0x20
#define GAME_MEDALS_OFFSET 0x22
#define GAME_LASTSCORE_OFFSET 0x2e
#define GAME_RANK_HIGH_OFFSET 0x30
#define GAME_TOTALSCORE_OFFSET 0x32
#define GAME_START_THEATER 0x38
#define GAME_IS_CAMPAIGN_MISSION_OFFSET 0x3a
#define GAME_MISSION_READY_OFFSET 0x3c
#define GAME_START_DIFFICULTY 0x3e
#define GAME_UNK4_OFFSET 0x40
#define GAME_RAND_OFFSET 0x42
#define GAME_RAND_OFFSET 0x42
#define GAME_HOF_ELIGIBLE_OFFSET 0x44
#define GAME_CAMPAIGN_PROGRESS_OFFSET 0x4e

#endif /* COMM_H */
