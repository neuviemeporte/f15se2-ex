#ifndef F15_SE2_EGTYPES
#define F15_SE2_EGTYPES
/* egame.exe hardware/comm/overlay constants. */

/* DOS calling-convention / pointer-size decorations (far/near/pascal/__cdecl/
 * __far/FAR/…) come from the single source, compat64/dos.h; many egame TUs
 * pick them up transitively through this header. */
#include "dos_compat.h"

#define AIRCRAFT_MODELS_OFFSET 0xADD4
/* Total size of the g_world3dData region (main + photo models + appended
 * aircraft models); see the definition in egfarbuf.c. */
#define WORLD3D_DATA_SIZE (AIRCRAFT_MODELS_OFFSET + 0x520C)
#define IRQ_VIDEO 0x10
#define UNIT_STATE_COUNT 0x64

#endif /* F15_SE2_EGTYPES */
