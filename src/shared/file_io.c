/*
 * file_io.c - shared file I/O, backed by SDL_IOStream.
 */

#include "inttype.h"
#include "pointers.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* The game ships its assets with UPPERCASE 8.3 names (LABS.PIC), but the code
 * passes lowercase or mixed-case literals ("labs.pic", "HallFame"). DOS
 * filesystems were case-insensitive; a Linux filesystem is not.
 *
 * Resolve `filename` to the spelling that actually exists on disk: try the name
 * as given, then an all-uppercase form, then an all-lowercase form, and return
 * whichever exists (copied into `buf`). If none exists, return the original name. */
static const char *resolveCasePath(const char *filename, char *buf, size_t bufsz)
{
    SDL_PathInfo info;
    size_t len, i;

    if (SDL_GetPathInfo(filename, &info)) return filename;

    len = SDL_strlen(filename);
    if (len >= bufsz) return filename;

    for (i = 0; i <= len; i++) buf[i] = (char)toupper((unsigned char)filename[i]);
    if (SDL_GetPathInfo(buf, &info)) return buf;

    for (i = 0; i <= len; i++) buf[i] = (char)tolower((unsigned char)filename[i]);
    if (SDL_GetPathInfo(buf, &info)) return buf;

    return filename;
}

/* Open an asset for reading. Returns the stream, or NULL on failure. */
SDL_IOStream *openFile(const char *filename, int mode)
{
    char buf[260];
    (void)mode;   /* the resident open service only distinguished read vs. write;
                   * every openFile caller in the game opens an asset for reading */
    return SDL_IOFromFile(resolveCasePath(filename, buf, sizeof(buf)), "rb");
}

/* Create or truncate a file for writing. Returns the stream, or NULL on failure. */
SDL_IOStream *createFile(const char *filename, int attr)
{
    char buf[260];
    (void)attr;
    return SDL_IOFromFile(resolveCasePath(filename, buf, sizeof(buf)), "wb");
}

void fileClose(SDL_IOStream *io)
{
    if (io) SDL_CloseIO(io);
}

/* fread/fwrite work-alikes over SDL_IOStream: transfer `count` items of `size`
 * bytes and return the number of whole items moved, matching the stdio
 * semantics the game's call sites were written against. */
size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *io)
{
    if (!io || size == 0) return 0;
    return SDL_ReadIO(io, ptr, size * count) / size;
}

size_t fileWrite(const void *ptr, size_t size, size_t count, SDL_IOStream *io)
{
    if (!io || size == 0) return 0;
    return SDL_WriteIO(io, ptr, size * count) / size;
}

/* Raw read into a host buffer. Shared with the PIC decoder (picimpl.c), which
 * streams the file 512 bytes at a time. Returns bytes read, or -1 on a null
 * stream. A negative count means "as much as fits" (DOS used cx=0xFFFF). */
int fileReadRaw(SDL_IOStream *io, void *dst, int count)
{
    if (!io) return -1;
    if (count < 0) count = 0xFFFF;
    return (int)SDL_ReadIO(io, dst, (size_t)count);
}

/* readFileAt/readFile/writeFile still resolve their buffer through MK_FP: the
 * segment:offset addressing is the next thing to be ported away, but it is not
 * on the PIC/title path and is left intact for now. */
int readFile(SDL_IOStream *io, int count, int bufOffset)
{
    return fileReadRaw(io, (void *)MK_FP(0, bufOffset), count);
}

int readFileAt(SDL_IOStream *io, int count, int offset, int segment)
{
    return fileReadRaw(io, (void *)MK_FP(segment, offset), count);
}

int writeFile(SDL_IOStream *io, int count, int offset, int segment, int unused)
{
    (void)unused;
    if (!io) return -1;
    if (count < 0) count = 0xFFFF;
    return (int)SDL_WriteIO(io, (const void *)MK_FP(segment, offset), (size_t)count);
}

int writeFileAtRaw(SDL_IOStream *io, void *buf, uint16 count)
{
    if (!io) return -1;
    return (int)SDL_WriteIO(io, buf, count);
}

/* Print a '$'-terminated DOS string (INT 21h/09h) to the log. */
void dos_printstring(const char *str)
{
    size_t len = 0;
    while (str[len] && str[len] != '$') len++;
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO,
                   "%.*s", (int)len, str);
}

/* file_error.inc: Print error and exit */
void errorAndExit(const char *msg)
{
    dos_printstring(msg);
    SDL_Quit();
    exit(1);
}
