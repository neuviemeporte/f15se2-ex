/*
 * filepic.c - file/picture open-close wrappers, loadPic, and mystrcpy
 * Compiled with /Gs (no /Zi). Shared between start.exe and end.exe.
 */

#include "common.h"
#include "../log.h"
#include <SDL3/SDL.h>

SDL_IOStream *openFile(const char *name, int mode);
void fileClose(SDL_IOStream *handle);
void decodePic(SDL_IOStream *handle, int segment);
int loadReplacementPngToPage(const char *filename, int page);
int loadReplacementPngToSprite(const char *filename, int segment);

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
    fileClose(handle);
}

void openShowPic(const char *filename, int page) /* Original chain: OpenFile + show/decode + CloseFile. Open, draw PIC to page, then close. */
{
    SDL_IOStream *fileHandle;
    if (loadReplacementPngToPage(filename, page)) {
        return;
    }
    fileHandle = openFileWrapper(filename, 0);
    showPicFile(fileHandle, page);
    closeFileWrapper(fileHandle);
}

void loadPic(const char *filename, int segment) { /* Original chain: OpenFile + DecodePic(InSeg, OutSeg) + CloseFile. Load PIC into segment. */
    SDL_IOStream *handle;
    if (loadReplacementPngToSprite(filename, segment)) {
        return;
    }
    handle = openFileWrapper(filename, 0);
    decodePic(handle, segment);
    closeFileWrapper(handle);
}
