#include "inttype.h"
#include "pointers.h"
#include "input.h"
#include "r3d.h"
#include "slot.h"
#include "gfx.h"

void pollJoystick(void) {}
void gfx_setPageN(unsigned short pageNum) { (void)pageNum; }
void copyJoystickData(uint8 FAR *ptr) { (void)ptr; }
void gfx_drawSpriteOpaque(int handle, int srcX, int srcY, int dstPage,
                         int dstX, int dstY, int width, int height) {
    (void)handle;
    (void)srcX;
    (void)srcY;
    (void)dstPage;
    (void)dstX;
    (void)dstY;
    (void)width;
    (void)height;
}
