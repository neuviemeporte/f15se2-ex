#include "SDL3/SDL_oldnames.h"
#include "inttype.h"
#include "offsets.h"
#include "pointers.h"
#include "comm.h"
#include "gfx.h"
#include "slot.h"
#include "const.h"
#include "debug.h"
#include "stalloc.h"
#include "stcode.h"
#include "stdata.h"
#include "stgen.h"
#include "stinit.h"
#include "stmissn.h"
#include "stpilot.h"
#include "sttypes.h"
#include "shared/common.h"

#include <dos.h>

int start_main(void)
{
    uint8 introStage;
    uint16 difficulty;
    int16 theater;
    int16 titleFadeDac;

    SDL_Log("start: entering");
    SDL_Log("start: installing cbreak handler");
    installCBreakHandler();
    /* gfx/misc/sound are called directly in the merged build — no overlay
     * slot trampolines to populate. */
    SDL_Log("start: gfx_storeBufPtr");
    gfx_storeBufPtr(commData->gfxInitResult, 2);
    SDL_Log("start: init graphics");
    initGraphics();
    SDL_Log("start: init audio");
    audio_shutdown();
    audio_setup(0, 0);
#ifndef DEBUG_AUTOSTART
    if (commData->needSplash == 1) {
        SDL_Log("start: doing splash");
        gameData->campaignProgress = 1;
        gameData->difficulty = 0xffff;
        gameData->theater = 0xffff;
        gfx_setFadeSteps(5);
        SDL_Log("start: showing labs");
        openShowPic("Labs.pic", 0);
        gfx_commitPage();
        SDL_Log("start: setting timer irq handler");
        setTimerIrqHandler();
        for (timerCounter = 0; timerCounter < MPS_TIMEOUT;) {
            if (misc_checkKeyBuf() == 0) {
                misc_getKey();
                break;
            }
        }
        if (timerCounter >= MPS_TIMEOUT) { // key was not pressed, show adv.pic
            gfx_waitRetrace();
            gfx_setFadeSteps(15);
            SDL_Log("start: showing adv");
            openShowPic("adv.pic", 0);
            gfx_commitPage();
            gfx_flipPage();
            for (introStage = 0; introStage < 2; introStage++) {
                for (timerCounter = 0; timerCounter < ADV_TIMEOUT;) {
                    if (misc_checkKeyBuf() == 0) {
                        misc_getKey();
                        goto checkEga;
                    }
                }
            }
        }

checkEga:
        /* Ask SDL for the hi-res title resolution; if it takes, show the 640x350
         * title, otherwise fall back to the 320x200 one. Either way we restore
         * the 320x200 game resolution afterwards. */
        if (video_setHiRes()) {
            SDL_Log("start: hi-res title");
            showPic640("Title640.Pic");
            titleFadeDac = 2;
        }
        else {
            SDL_Log("start: 16-color title");
            gfx_setFadeSteps(1);
            openShowPic("title16.pic", 0);
            gfx_commitPage();
            gfx_setDac(4);
            titleFadeDac = 0;
        }
        TRACE(("main: waiting for mda/cga"));
        waitMdaCgaStatus(4);
        SDL_Log("start: doing audio thing");
        audio_playIntro();
        restoreTimerIrqHandler();
        gfx_setDac(titleFadeDac);
        misc_getKey();
        gfx_waitRetrace();
        gfx_setMode13();
    }
#endif /* !DEBUG_AUTOSTART */

    SDL_Log("start: setting difficulty and theater");
#ifdef DEBUG_AUTOSTART
    /* Auto-start: skip UI, set hardcoded difficulty/theater, go straight to egame */
    SDL_Log("start: DEBUG_AUTOSTART - skipping UI");
    gameData->difficulty = 0;  /* 0=green (airborne start), 1=veteran, 2=ace, 3=max, 4=demo */
    gameData->theater = 0;     /* 0=Libya, 1=Desert, 2=Europe, 3=Kuril */
    gameData->missionReady = 1;
    gameData->isCampaignMission = 0;
    gameData->campaignProgress = 0;
    gameData->rand = 12345;
    commData->startDone = 1;
    joyAxes[0] = joyAxes[1] = JOY_CENTER;
    srand(gameData->rand);
    missionGenerate();
#else
    difficulty = gameData->difficulty;
    theater = gameData->theater;
    if (commData->trainingFlag == 0 && gameData->campaignProgress == 0 && gameData->theater < NUM_THEATERS &&
            ++(gameData->theater) == NUM_THEATERS) {
        gameData->theater = 0;
        if (gameData->difficulty < MAX_DIFFICULTY) {
            gameData->difficulty++;
        }
    }

    SDL_Log("start: setting up kbd/joy");
    clearKeybuf();
    if (commData->setupUseJoy == 1) {
        /*
            This data of length 0x14 is copied in su.exe at seg0001(683):d1 from dseg(692):eb4 to COMM:48, then later this copies it from there onto the stack (???)
            1CC2:0CDE     6D 01 6D 01 00 00 00 00 73 01 73 01 00 00 00 00  m.m.....s.s.....\n            1CC2:0CEE     6E 01 6E 01 00 00 00 00 01 00 01 00 00 00 00 00  n.n.............\n        */
       copyJoystickData(commData->joyData);
    }
    else {
        joyAxes[0] = joyAxes[1] = JOY_CENTER;
    }
    SDL_Log("start: init pilot/mission");
    joyReady[0] = 1;
    menuSprites = gfx_allocSpriteBuf();
    pilotSelect(commData->needSplash);
    SDL_Log("start: pilot selected");
    missionSelect();
    SDL_Log("start: mission selected");
    gameData->missionReady = 1;
    gameData->isCampaignMission = 1;
    gameData->campaignProgress = 0;
    commData->startDone = 1;
    /* 0x365, check if same diff and thea picked as last time */
    if (gameData->difficulty == difficulty && gameData->theater == theater && missionPick == -1 && askRepeatMission() != 0)
        goto doSrand;
    gameData->rand = rand();
doSrand:
    SDL_Log("start: mission decoding");
    srand(gameData->rand);
    missionDecode();
    SDL_Log("start: mission generation");
    missionGenerate();
#endif
#ifdef DEBUG_AUTOSTART
    /* Auto-start: skip ONLY the interactive screens (printMission briefing and
       the checkDiskA disk prompt). Everything else must run exactly as the
       normal path below -- in particular the f15.spr sprite-sheet load into
       commData->gfxInitResult, which egame reads as gfxBufPtr for the radar /
       tactical-map / HUD sprites. */
    exitCode[0] = 12;
    restoreCbreakHandler();
    commData->needSplash = 0;
    gfx_setFadeSteps(8);
    TRACE(("start: DEBUG_AUTOSTART - loading sprites"));
    if (gfx_getVal() == 0) {
        openShowPic("f15.spr", 2);
    }
    else {
        loadPic("f15.spr", commData->gfxInitResult);
    }
    TRACE(("start: DEBUG_AUTOSTART - write world"));
    exportWorldToComm("temp.wld");
    commData->restartFlag = 0;
    if (gameData->missionReady > 1) {
        commData->trainingFlag = 1;
    }
    else {
        commData->trainingFlag = 0;
    }
#else
    if (gameData->difficulty != DIFFICULTY_DEMO) {
        TRACE(("start: printing mission"));
        printMission();
    }
    TRACE(("start: checking disk"));
    checkDiskA();
    exitCode[0] = 12;
    TRACE(("start: restoring cbreak handler and clearing splash"));
    restoreCbreakHandler();
    commData->needSplash = 0;
    gfx_setFadeSteps(8);
    TRACE(("start: loading sprites"));
    if (gfx_getVal() == 0) {
        openShowPic("f15.spr", 2);
    }
    else {
        loadPic("f15.spr", commData->gfxInitResult);
    }
    // 403
    TRACE(("start: write world"));
    exportWorldToComm("temp.wld");
    commData->restartFlag = 0;
    if (gameData->missionReady > 1) {
        commData->trainingFlag = 1;
    }
    else {
        commData->trainingFlag = 0;
    }
    TRACE(("start: clearing keyflags and screen"));
#endif /* !DEBUG_AUTOSTART */
    misc_clearKeyFlags();
    clearRect(bufPtr, 0, 0, SCREEN_MAXX, SCREEN_MAXY);
    TRACE(("start: exiting with code %hd", exitCode[0]));
    return exitCode[0];
}
