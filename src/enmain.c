/* enmain.c — main/init, compiled with /Gs */
#include "gfx.h"
#include "slot.h"
#include <dos.h>
#include "offsets.h"
#include "log.h"
#include "shared/common.h"
#include <stdlib.h>
#include "endtypes.h"
#include "endata.h"
#include "endcode.h"
#include "enaward.h"
#include "enbrief.h"
#include "endbrf.h"
#include "eninput.h"
#include "enmisc.h"
#include "enrand.h"

/* Private helpers for this translation unit. */
void enInitGraphics(void);
void checkQuitFlag(void);

int end_main(void) {
    misc_clearKeyFlags();
    clearKeybuf();
    installCBreakHandler();
    enInitGraphics();
    /* Centre the debrief's nav axes regardless of setupUseJoy: nothing feeds
     * joyAxisX/Y natively (the pad is read through the event pump), so leaving
     * them at the init 0 would sit below the deadzone and spam LEFTARROW. */
    joyAxisX = joyAxisY = JOY_CENTER;
    if (commData->setupUseJoy == 1) {
        copyJoystickData(commData->joyData);
    }
    loadWorldStrings();
    missionResult = 3;
    if (commData->landingType == 2) {
        computeMissionResult();
    }
    clearKeybuf();
    /* HD debrief maps and their path/event graphics are native overlays rather
     * than pixels retained in page 0. Recompose them after expose, focus, or
     * resize events instead of repainting only the static debrief backdrop. */
    gfx_setRepaintHook(debriefPresent);
    debriefMainLoop();
    gfx_setRepaintHook(NULL);
    checkQuitFlag();
    clearKeybuf();
    showPostMissionAwards();
    restoreCbreakHandler();
    return EXIT_DEBRIEF;
}

void checkQuitFlag(void) {
    if (quitFlag != 0) {
        cleanup();
        restoreCbreakHandler();
        exit(0);
    }
}

void enInitGraphics(void) {
    int a, b, c, d, e, f, g, h;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    (void)f;
    (void)g;
    (void)h;
    enSeedRandom();
    gfx_setDac(1);
}
