// seg000 optimized code (/Ot)
#include "eg3dload.h"
#include "eg3dmap.h"
#include "eg3dproj.h"
#include "eg3dview.h"
#include "egcode.h"
#include "egdata.h"
#include "egtarget.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "gfx_impl.h"
#include "const.h"
#include "r3d.h"

#include <dos.h>
#include <memory.h>

void loadRegion3D() {
    load3DAll();
}

// ==== seg000:0x21ca ====
//
void render3DView(int16 camX, int16 camY, int16 camZ, int32 worldX, int32 worldY, int32 worldZ, int16 clipLeft, int16 clipTop, int16 clipWidth, int16 clipHeight) {
    g_viewParams[7] = clipTop;
    g_viewParams[8] = clipTop + clipHeight - 1;
    g_viewParams[9] = clipLeft;
    g_viewParams[10] = clipLeft + clipWidth - 1;
    /* The view descriptor's page field is inert — all 3D composites into the
     * single hidden back buffer. */
    *g_viewParams = 0;
    g_viewParams[2] = (uint8)((char *)colorLut)[g_skyColorIndex & 0xFF];
    {
        R3DScene scene = {g_viewParams, camX, camY, camZ, 0, 0, (int16)worldZ, 1};
        r3d_beginScene(&scene);
    }
    projectObjects(camX, camY, worldX, worldY, worldZ);
    updateTargetLock();
    drawWorldEffects(); /* 3D tracers + explosion sparks: inside the scene so they sort/occlude/fog */
    r3d_endScene();
    drawHudWorldOverlay();
    g_renderPageToggle ^= 1;
}

// ==== seg000:0x2278 ====
void waitFrameSync(int16 frames) {
    uint8 targetTick;
    if (frames > 0) {
        targetTick = (uint8)frames + g_timerTickByte[0];
        while (targetTick != g_timerTickByte[0]) timerYield();
    }
}

// ==== seg000:0x229a loadColorPalette ====
void loadColorPalette(int16 idx) {
    memcpy(colorLut, g_colorPalettes + idx * 16, 16);
}

// ==== seg000:0x3224 ====
