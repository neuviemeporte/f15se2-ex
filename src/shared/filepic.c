/*
 * filepic.c - file/picture open-close wrappers, loadPic, and mystrcpy
 * Compiled with /Gs (no /Zi). Shared between start.exe and end.exe.
 */

#include "common.h"
#include "../log.h"

int openFile(const char *name, int mode);
void fileClose(int handle);
void decodePic(int handle, uint16 segment);

void mystrcpy(char *dest, const char *source) {
    do {
    } while ((*dest++ = *source++) != '\0');
}

int openFileWrapper(const char *filename, int mode) /* Original: OpenFile(file, attrib). Open resident file service; returns a file handle. */
{
    return openFile(filename, mode);
}

void closeFileWrapper(int handle) /* Original: CloseFile(fh). Close a resident file-service handle. */
{
    /* Close through file_io.c so the handle bookkeeping stays centralized. */
    fileClose(handle);
}

void openShowPic(char *filename, int page) /* Original chain: OpenFile + show/decode + CloseFile. Open, draw PIC to page, then close. */
{
    int16 fileHandle;
    Log(("openShowPic: opening file %s, page %d",filename,page));
    fileHandle = openFileWrapper(filename, 0);
    Log(("openShowPic: showing pic, handle %d",fileHandle));
    showPicFile(fileHandle, page);
    closeFileWrapper(fileHandle);
    Log(("openShowPic: file closed, returning"));
}

void loadPic(const char *filename, uint16 segment) { /* Original chain: OpenFile + DecodePic(InSeg, OutSeg) + CloseFile. Load PIC into segment. */
    int handle;
    handle = openFileWrapper(filename, 0);
    Log(("loadPic(): opened %s, loading into segment 0x%x", filename, segment));
    decodePic(handle, segment);
    closeFileWrapper(handle);
}
