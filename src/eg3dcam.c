// seg000 optimized code (/Ot)
#include "eg3dcam.h"
#include "egcode.h"
#include "egdata.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "gfx_impl.h"
#include "const.h"

#include "dos_compat.h"
#include <memory.h>

void setViewRotation(int rotX, int rotY, int rotZ) {
    buildRotationMatrixFar(g_viewRotMatrix, -rotX, -rotY, -rotZ);
}

// ==== seg000:0x3a90 ====
void setViewPosition(int16 viewX, int16 viewY, int16 viewZ) { /* Original: SetViewPos(X,Y,Z). Store viewer coordinates for 3D transforms. */
    g_viewPosX = viewX;
    g_viewPosY = viewY;
    g_viewPosZ = viewZ;
    g_viewPosFracX = g_viewPosFracY = g_viewPosFracZ = 0;
}

/* Q8 sub-unit remainder of the viewer position (true = g_viewPos + frac/256).
 * Cleared by setViewPosition, so callers with sub-unit knowledge opt in after it. */
void setViewPositionFrac(int fracX, int fracY, int fracZ) {
    g_viewPosFracX = (int16)fracX;
    g_viewPosFracY = (int16)fracY;
    g_viewPosFracZ = (int16)fracZ;
}
