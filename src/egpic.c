// seg000 optimization disabled (/Od) code
#include "shared/common.h"
#include "egcode.h"
#include "egpic.h"
#include "egtypes.h"
#include "offsets.h"
#include "log.h"
#include "const.h"

#include <dos.h>
#include <memory.h>

void openBlitClosePic(const char *filename, int16 page) { /* Original chain: OpenFile + blit/decode + CloseFile. Open, blit PIC to page, then close. */
    SDL_IOStream *handle = openFileWrapper(filename, 0);
    /* The PIC decoder/blitter consumes the already-open file handle. */
    showPicFile(handle, page);
    /* Always close immediately after the synchronous blit/decode call. */
    closeFileWrapper(handle);
}
