#include "inttype.h"
#include "offsets.h"
#include "pointers.h"
#include "comm.h"
#include "shared/common.h"
#include "gfx.h"
#include "slot.h"
#include "const.h"

#include "debug.h"
#include "stdata.h"
#include "stinit.h"
#include "strand.h"

#include <stdio.h>
#include <dos.h>


void initGraphics()
{
    /* unused stack data eliminated by compiler, but original binary has sub sp,0xe in preamble - ??? */
    uint8 unused[14];
    seedRandom();
    gfx_setPageN(0);
    gfx_allocPage(0);
    /* 0x4c4 - see f14 gmain.c InitGraphicPages() */
    gfx_storeBufPtr(page1Ptr = gfx_allocPage(1), 1); // 64k framebuffer @ 2cc0:0
    misc_clearKeyFlags();
}
