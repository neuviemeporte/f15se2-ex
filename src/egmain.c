// seg000 debug code (/Zi)
#include "eg3dview.h"
#include "egcode.h"
#include "egdata.h"
#include "egframe.h"
#include "egmath.h"
#include "worldxfer.h"
#include "egpic.h"
#include "egtypes.h"
#include "offsets.h"
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
void drawCockpit();
void runGameSession();

// ==== seg000:0x10 ====
int egame_main(void) {
    /* Per-mission state reset (persists across missions in the merged build).
     * g_initPhase=0 re-arms the init block; the indicator colour trackers (+7 of
     * the four rects at [3..22]) must match the cockpit base colour (3). The view
     * selector and director camera must start in the cockpit, not last mission's view. */
    g_initPhase = 0;
    g_missionEndedFlag[0] = g_missionEndedFlag[1] = 0;
    g_eventLogCount = 0;
    g_ejectState = 0;
    g_ejectPending = 0;
    keyValue = 0;
    g_directorMode = 0;
    g_directorEventDeadline = -1;
    g_tacmapIndicators[7] = g_tacmapIndicators[12] = g_tacmapIndicators[17] = g_tacmapIndicators[22] = 3;
    installCBreakHandler();
    if (commData->setupUseJoy == 1) {
        copyJoystickData(commData->joyData);
    } else {
        joyAxes[0] = joyAxes[1] = 0x80;
    }
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
    return exitCode;
}

// ==== seg000:0x147 ====
void drawCockpit() {
    initMissionStrings();
    load15Flt3d3();
    strcpy(regnStr, scenarioPlh[gameData->theater]);
    loadRegion3D();
    {
        /* Verify the mesh decoder against the just-loaded world models, once
         * per process. */
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
    /* Snapshot the clean lower cockpit into the save-under backing image. The
     * cockpit strip / scope panel / map-marker save-unders restore their regions
     * from here. */
    if (!g_eg2dBacking) g_eg2dBacking = gfx_allocImage(320, 200);
    gfx_captureToImage(g_eg2dBacking, 1, 0, 96, 0, 96, 320, 104);
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
    worldExportToEnd();
    if (commData->setupUseJoy == 0) {
        restoreInt9Handler();
    }
    gfx_setDacAnimCount(1);
    waitFrameSync(2);
    restoreTimerIrqHandler();
    setTimerTickHook(nullptr);
    audio_shutdown();
}
