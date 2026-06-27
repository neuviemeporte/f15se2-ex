// seg000 debug code (/Zi)
#include "eg3dview.h"
#include "egcode.h"
#include "egdata.h"
#include "egframe.h"
#include "egmath.h"
#include "egpic.h"
#include "egtypes.h"
#include "offsets.h"
#include "pointers.h"
#include "log.h"
#include "gfx.h"
#include "slot.h"
#include "const.h"
#include "comm.h"
#include "r3dmesh.h"

#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private helpers for this translation unit. */
void __cdecl drawCockpit();
void runGameSession();
void doNothing3();
void doNothing4();
void __cdecl gfxInit();

// ==== seg000:0x10 ====
int egame_main(void) {
    /* Per-mission state reset (persists across missions in the merged build).
     * g_initPhase=0 re-arms the init block; the indicator colour trackers (+7 of
     * the four rects at [3..22]) must match the cockpit base colour (3). */
    g_initPhase = 0;
    g_missionEndedFlag[0] = g_missionEndedFlag[1] = 0;
    g_eventLogCount = 0;
    g_ejectState = 0;
    g_tacmapIndicators[7] = g_tacmapIndicators[12] = g_tacmapIndicators[17] = g_tacmapIndicators[22] = 3;
    installCBreakHandler();
    if (commData->setupUseJoy == 1) {
        copyJoystickData(commData->joyData);
    } else {
        joyAxes[0] = joyAxes[1] = 0x80;
    }
    gfxInit();
    gfx_initOverlay();
    if (gameData->theater < 2) {
        gfx_setFadeSteps(12);
    } else {
        gfx_setFadeSteps(16);
    }
    gfxBufPtr = commData->gfxInitResult;
    setupInstrumentLayoutFar();
    drawCockpit();
    runGameSession();
    if (commData->setupUseJoy == 1) {
        restoreJoystickData(commData->joyData);
    }
    restoreCbreakHandler();
    if (exitCode == 0) {
        regs.h.ah = 0;
        regs.h.al = 3;
        int86(IRQ_VIDEO, &regs, &regs);
    }
    return exitCode;
}

// ==== seg000:0x147 ====
void drawCockpit() {
    initMissionStrings();
    load15Flt3d3();
    strcpy(regnStr, scenarioPlh[gameData->theater]);
    loadRegion3D();
    {
        /* Verify the Step-2 mesh decoder against the just-loaded world models,
         * once per process (see docs/render-3d-backend.md). */
        static int meshSelfTestDone = 0;
        if (!meshSelfTestDone) {
            meshSelfTestDone = 1;
            r3dmesh_selfTest();
        }
    }
    f15DgtlResult = loadF15DgtlBin();
    g_horizonGroundColor = g_world3dData[47];
    if ((g_dacSupported = gfx_getModeFlag()) != 0) {
        setupDac();
    }
    gfx_setDac(1);
    gfx_waitRetrace();
    if (gfx_getModecode() == 3) {
        openBlitClosePic("256pit.PIC", 1);
    } else {
        openBlitClosePic("cockpit.PIC", 1);
    }
    gfx_copyRect(1, 0, 96, 0, 0, 96, 320, 104);
    gfx_copyRect(1, 0, 96, 2, 0, 96, 320, 104);
}

// ==== seg000:0x211 ====
void runGameSession() {
    /* The original capped the BIOS floppy motor-off countdown in the BDA;
       there is no floppy access natively, so that poke is dropped. */
    audio_shutdown();
    audio_setup(0, f15DgtlResult);
    setTimerTickHook(egAdvanceFrameTick);
    setTimerIrqHandler();
    if (commData->setupUseJoy == 0) {
        setInt9Handler();
    }
    runGameLoop();
    moveDataFar();
    if (commData->setupUseJoy == 0) {
        restoreInt9Handler();
    }
    gfx_setDacAnimCount(1);
    waitFrameSync(2);
    restoreTimerIrqHandler();
    setTimerTickHook(nullptr);
    audio_shutdown();
}

// ==== seg000:0x0294 routine_6 ====
void doNothing3() {
}

// ==== seg000:0x0297 routine_5 ====
void doNothing4() {
}

// ==== seg000:0x29a ====
void gfxInit() {
    int var_2;
    gfx_allocPage(0);
    var_2 = gfx_allocPage(1);
    gfx_storeBufPtr(var_2, 1);
    gfx_storeBufPtr(commData->gfxInitResult, 2);
}
