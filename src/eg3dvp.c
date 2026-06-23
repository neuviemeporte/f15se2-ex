#include "eg3dvp.h"
#include "egdata.h"
#include "egtypes.h"
#include "gfx_impl.h"

// ==== seg000:0x39c0 ====
void setupViewport(const int16 *rect) {
    int height, width;
    /* The clip-rect fields are 16-bit words (descriptor indices 7..10). */
    width = rect[10] - rect[9] + 1;
    height = rect[8] - rect[7] + 1;
    g_viewCenterX = ((width + 1) >> 1) - 1;
    g_viewCenterY = ((height + 1) >> 1) - 1;
    if (rect[7] == 0) {
        g_viewCenterY = rect[8] == 199 ? 100 : 56;
    }
    gfx_setOvlVal2(width - 1);
    gfx_setBlitOffset(gfx_calcRowAddr(rect[9], rect[7]));
    g_clipMaxX = width - 1;
    g_clipMaxY = height - 1;
    g_overlayCenterX = g_overlayBaseX;
    g_overlayCenterY = g_overlayBaseY;
    if (g_halfScaleRender != 0) {
        g_overlayCenterX += 8;
        g_overlayCenterY += 8;
    }
    if ((char)g_hudVisible != 0) {
        g_overlayCenterY += 16;
    }
}
