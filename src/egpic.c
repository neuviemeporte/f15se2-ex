// seg000 optimization disabled (/Od) code
#include "egfileio.h"
#include "egcode.h"
#include "egpic.h"
#include "egtypes.h"
#include "offsets.h"
#include "pointers.h"
#include "log.h"
#include "slot.h"
#include "const.h"

#include <dos.h>
#include <memory.h>

void showPicFile(int handle, int pageNum);
void openBlitClosePic(const char* filename, int page) { /* Original chain: OpenFile + blit/decode + CloseFile. Open, blit PIC to page, then close. */
    int handle = openFileWrapper(filename, 0);
    /* The PIC decoder/blitter consumes the already-open file handle. */
    showPicFile(handle, page);
    /* Always close immediately after the synchronous blit/decode call. */
    closeFileWrapper(handle);
}
