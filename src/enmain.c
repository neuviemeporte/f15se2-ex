/* enmain.c — main/init, compiled with /Gs */
#include "gfx.h"
#include "slot.h"
#include <dos.h>
#include "offsets.h"
#include "pointers.h"
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
    int auxBufSize;

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
    auxBufSize = gfx_getAuxBufSize();
    gfxBufSeg = allocBuffer(auxBufSize);
    if (hasVgaMode == 1) {
        vgaBufSeg = allocBuffer(VGA_BUF_SIZE);
        vgaBufSeg2 = vgaBufSeg;
        vgaBufOffset = 0;
    }
    missionResult = 3;
    if (commData->landingType == 2) {
        computeMissionResult();
    }
    clearKeybuf();
    debriefMainLoop();
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
    gfx_setPageN(0);
    gfx_allocPage(0);
    gfx_getCurPage(0);
    gfx_setDac(1);
    gfx_storeBufPtr(commData->gfxInitResult, 1);
}
