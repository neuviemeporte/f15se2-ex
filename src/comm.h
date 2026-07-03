#ifndef COMM_H
#define COMM_H

#include "const.h"
#include "inttype.h"
#include "sassert.h"

/* In-memory only (never serialized; the three former DOS EXEs are one process
 * now) and accessed solely by named field, so no packing / size contract. */
struct GameComm {
    int16 gfxInitResult;   /* f15.spr sprite-sheet buffer handle (radar/HUD sprites) */
    int16 landingType;     /* 1=crashed, 2=ejected, 3=landed */
    int16 bailoutSurvived; /* 0=survived */
    int16 unk4;            /* nonzero enables crash exit in egame (set from theater table in start.exe) */
    int16 trainingFlag;    /* nonzero if the last mission was a training mission */
    int16 setupDetail;
    int16 gunHits;        /* egame writes mission gun-hit count here for the debrief */
    uint16 weaponType[4]; /* weapon type indices into weaponLoadouts[] (0=Sidewinder,1=AMRAAM,etc) */
    int16 weaponCount[4]; /* weapon quantities per slot */
    uint8 joyData[20];
    int16 setupUseJoy;
    uint16 worldX;
    uint16 worldY;
    int16 needSplash; /* show splash/intro on the first START pass */
};

/* The shared communication record. */
extern struct GameComm *commData;

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
STATIC_ASSERT(sizeof(struct Game) == 80);

/* The shared Game record. */
extern struct Game *gameData;

#endif /* COMM_H */
