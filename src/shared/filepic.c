/*
 * filepic.c - file/picture open-close wrappers, loadPic, and mystrcpy
 * Compiled with /Gs (no /Zi). Shared between start.exe and end.exe.
 */

#include "common.h"
#include "../debug.h"
#include <SDL3/SDL.h>

SDL_IOStream *openFile(const char *name, int mode);
void fileClose(SDL_IOStream *handle);
void decodePic(SDL_IOStream *handle, int segment);

void mystrcpy(char *dest, const char *source) {
    do {
    } while ((*dest++ = *source++) != '\0');
}

SDL_IOStream *openFileWrapper(const char *filename, int mode) /* Original: OpenFile(file, attrib). */
{
    return openFile(filename, mode);
}

void closeFileWrapper(SDL_IOStream *handle) /* Original: CloseFile(fh). */
{
    TRACE(("closeFileWrapper"));
    fileClose(handle);
}

void openShowPic(const char *filename, int page) /* Original chain: OpenFile + show/decode + CloseFile. Open, draw PIC to page, then close. */
{
    SDL_IOStream *fileHandle;
    TRACE(("openShowPic: opening file %s, page %d",filename,page));
    fileHandle = openFileWrapper(filename, 0);
    TRACE(("openShowPic: showing pic, handle %p",(void *)fileHandle));
    showPicFile(fileHandle, page);
    closeFileWrapper(fileHandle);
    TRACE(("openShowPic: file closed, returning"));
}

void loadPic(const char *filename, int segment) { /* Original chain: OpenFile + DecodePic(InSeg, OutSeg) + CloseFile. Load PIC into segment. */
    SDL_IOStream *handle;
    handle = openFileWrapper(filename, 0);
    TRACE(("loadPic(): opened %s, loading into segment 0x%x", filename, segment));
    decodePic(handle, segment);
    closeFileWrapper(handle);
}
