/*
 * This is the main game executable for F15 SE II.
 *
 * The original was apparently coded in assembly (and included a convoluted anti-debugging & copy protection scheme);
 * this is a purposefully divergent implementation that does not try to be faithful to the original binary.
 *
 * It also subsumes the functionality of SU.EXE (SetUp), whose purpose was to ask the user which graphics adapter and
 * sound device they wanted. We only support VGA (MCGA) and no sound, so there are no queries: the COMM values the
 * sub-programs read are simply populated with the VGA / no-sound configuration.
 *
 * The purpose of this executable is to do some initial setup and then operate the game main loop, running
 * START, EGAME and END in sequence until the user decides to exit.
 */

#include "inttype.h"
#include "pointers.h"
#include "dosfunc.h"
#include "log.h"
#include "comm.h"
#include "offsets.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "joystick.h"
#include "r3d.h"

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <SDL3/SDL.h>

const uint16 GFX_INIT_ARG = 2;
const int RET_MENU = 0xc;
const int RET_DEBRIEFING = 0x23;

struct GameComm commBuffer;
struct Game gameBuffer;

void game_init(void) {
    uint16 gfxBufAddress;

    commData = &commBuffer;
    gameData = &gameBuffer;

    commData->needSplash = 1;
    commData->setupUseJoy = 0;
    commData->setupDetail = 4; /* 4 = extended detail: full LOD + long-range draw distance */

    gfx_initState();
    gfx_setMode13();
    r3d_init();

    gfxBufAddress = (uint16)gfx_allocPage((int)GFX_INIT_ARG);
    commData->gfxInitResult = gfxBufAddress;
}

/* In-process entry points for the former START.EXE / EGAME.EXE / END.EXE.
 * These used to be separate programs launched via dos_runProgram(); they are
 * now linked into this single executable and called directly. */
int start_main(void);
int egame_main(void);
int end_main(void);

int main(int argc, char *argv[]) {
    /* process cmdline args */
    int argIdx, charIdx;
    bool debugMenu = false, debugFlight = false, debugDebrief = false;

    log_set_app("f15");
    gfx_videoInit();

    for (argIdx = 1; argIdx < argc; ++argIdx) {
        const char *arg = argv[argIdx];
        const size_t len = strlen(arg);
        if (len < 3 || arg[0] != '/' || tolower(arg[1]) != 'd')
            LogCritical(("Unrecognized argument: '%s'", arg));
        for (charIdx = 2; (size_t)charIdx < len; ++charIdx) {
            switch (arg[charIdx]) {
            case '1':
                debugMenu = true;
                break;
            case '2':
                debugFlight = true;
                break;
            case '3':
                debugDebrief = true;
                break;
            default:
                LogCritical(("Unrecognized argument: '%s'", arg));
            }
        }
    }

    game_init();
    joy_init();

    while (true) {
        int err;

        log_set_app("f15");
        log_set_app("start");
        err = start_main();
        log_set_app("f15");
        if (!debugMenu && err != RET_MENU) break;

        log_set_app("egame");
        err = egame_main();
        log_set_app("f15");
        if (!debugFlight && err == 0) break;

        log_set_app("end");
        err = end_main();
        log_set_app("f15");
        if (!debugDebrief && err != RET_DEBRIEFING) break;
    }

    joy_shutdown();
    gfx_videoShutdown();
    return 0;
}
