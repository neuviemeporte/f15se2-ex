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
#include "debug.h"
#include "slot.h"
#include "const.h"
#include "comm.h"

#include <dos.h>
#include <conio.h>
#include <bios.h>
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
    TRACE(("egame main: entering"));
    TRACE(("egame main: setup overlays"));
    TRACE(("egame main: install cbreak"));
    installCBreakHandler();
    if (commData->setupUseJoy == 1) {
        copyJoystickData(commData->joyData);
    }
    else {
        joyAxes[0] = joyAxes[1] = 0x80;
    }
    TRACE(("egame main: gfxInit"));
    gfxInit();
    TRACE(("egame main: after gfxInit"));
    TRACE(("egame main: calling initOverlay"));
    gfx_initOverlay();
    TRACE(("egame main: calling setFadeSteps"));
    if (gameData->theater < 2) {
        gfx_setFadeSteps(12);
    }
    else {
        gfx_setFadeSteps(16);
    }
    gfxBufPtr = commData->gfxInitResult;
    setupInstrumentLayoutFar();
    TRACE(("egame main: drawCockpit"));
    drawCockpit();
    TRACE(("egame main: runGameSession"));
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
    TRACE_KEY(("egame main: EXITING with code %d, tick=%d", exitCode, frameTick));
    return exitCode;
}

// ==== seg000:0x147 ====
void drawCockpit() {
    TRACE_KEY(("drawCockpit: theater=%d regnStr=%s 38FDC=%d", gameData->theater, regnStr, g_detailLevel));
    initMissionStrings();
    load15Flt3d3();
    TRACE(("drawCockpit: after load15Flt3d3, scenPlh0=%04x, scenarioPlh@%04x", (unsigned)scenarioPlh[0], (unsigned)&scenarioPlh[0]));
    strcpy(regnStr, scenarioPlh[gameData->theater]);
    TRACE_KEY(("drawCockpit: regnStr=%s theater=%d", regnStr, gameData->theater));
    loadRegion3D();
    TRACE_KEY(("drawCockpit: after load3D, 38FDC=%d sizes3dt=%d/%d/%d/%d/%d", g_detailLevel, sizes3dt[0], sizes3dt[1], sizes3dt[2], sizes3dt[3], sizes3dt[4]));
    f15DgtlResult = loadF15DgtlBin();
    TRACE(("drawCockpit: f15DgtlResult=%d", f15DgtlResult));
    g_horizonGroundColor = g_world3dData[47];
    if ((g_dacSupported = gfx_getModeFlag()) != 0) {
        setupDac();
    }
     gfx_setDac(1);
     gfx_waitRetrace();
     TRACE(("drawCockpit: opening pic"));
     if (gfx_getModecode() == 3) {
        openBlitClosePic("256pit.PIC", 1);
     }
     else {
        openBlitClosePic("cockpit.PIC", 1);
     }
     TRACE(("drawCockpit: pic done"));
     gfx_copyRect(1, 0, 96, 0, 0, 96, 320, 104);
     gfx_copyRect(1, 0, 96, 2, 0, 96, 320, 104);
     TRACE(("drawCockpit: done"));
}

// ==== seg000:0x211 ====
void runGameSession() {
    TRACE(("runGameSession: enter"));
    FP_OFF(g_floppyMotorPtr) = OFF_BDA_FLOPPYMOTOR; // floppy motor runtime in bda???
    FP_SEG(g_floppyMotorPtr) = 0;
    if (*g_floppyMotorPtr > 1) {
        *g_floppyMotorPtr = 1;
    }
    TRACE(("runGameSession: audio_shutdown"));
    audio_shutdown();
    TRACE(("runGameSession: audio_setup"));
    audio_setup(*(int16 FAR*)(OFF_IACA_UNK), f15DgtlResult);
    TRACE(("runGameSession: setTimerIrqHandler"));
    setTimerTickHook(egAdvanceFrameTick);
    setTimerIrqHandler();
    if (commData->setupUseJoy == 0) {
        TRACE(("runGameSession: setInt9Handler"));
        setInt9Handler();
    }
    TRACE(("runGameSession: runGameLoop (game loop)"));
    runGameLoop();
    moveDataFar();
    if (commData->setupUseJoy == 0) {
        restoreInt9Handler();
    }
    gfx_setDacAnimCount(1);
    waitFrameSync(2);
    restoreTimerIrqHandler();
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
    TRACE(("gfxInit: allocPage(0)"));
    gfx_allocPage(0);
    TRACE(("gfxInit: allocPage(1)"));
    var_2 = gfx_allocPage(1);
    TRACE(("gfxInit: allocPage(1) returned %d", var_2));
    gfx_storeBufPtr(var_2, 1);
    gfx_storeBufPtr(commData->gfxInitResult, 2);
    TRACE(("gfxInit: done"));
}
