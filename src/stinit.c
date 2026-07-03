#include "inttype.h"
#include "offsets.h"
#include "pointers.h"
#include "comm.h"
#include "shared/common.h"
#include "gfx.h"
#include "slot.h"
#include "const.h"

#include "log.h"
#include "stdata.h"
#include "stinit.h"
#include "strand.h"

#include <stdio.h>
#include <dos.h>

void initGraphics() {
    /* unused stack data eliminated by compiler, but original binary has sub sp,0xe in preamble - ??? */
    uint8 unused[14];
    seedRandom();
    misc_clearKeyFlags();
}
