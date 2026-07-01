#include "dos.h"
#include "inttype.h"

int far drawClipLineGlobal(void) { return 0; }
void far gfx_drawLine(uint16, uint16, uint16, uint16) {}
void far gfx_setColor(int) {}
void far gfx_nop22(void) {}
void far gfx_dirtyRect(int16 *, int, int) {}
