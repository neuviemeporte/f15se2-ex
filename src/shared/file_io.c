/*
 * file_io.c - shared file I/O, backed by SDL_IOStream.
 */

#include "inttype.h"
#include "pointers.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* file_alloc.inc: Allocate DOS memory. Callers pass BYTE count. */
uint16 dos_alloc(uint16 size)
{
    union REGS r;
    r.h.ah = 0x48;
    /* Convert bytes to paragraphs (divide by 16, round up) */
    r.x.bx = size >> 4;
    if (size & 0x0F) r.x.bx++;
    intdos(&r, &r);
    if (r.x.cflag) return 0;
    return r.x.ax;
}

/* file_close.inc: Close file handle */
void fileClose(int handle)
{
    union REGS r;
    r.h.ah = 0x3E;
    r.x.bx = handle;
    intdos(&r, &r);
}

/* file_open.inc: Open file, returns handle or -1 on error */
int openFile(const char *filename, int mode)
{
    union REGS r;
    struct SREGS s;
    r.h.ah = 0x3D;
    r.h.al = (unsigned char)mode;
    segread(&s);
    r.x.dx = 0; // (uint16)filename;
    intdosx(&r, &r, &s);
    if (r.x.cflag) return -1;
    return r.x.ax;
}


/* file_printstring.inc: Print '$'-terminated string via DOS */
void dos_printstring(const char *str)
{
    union REGS r;
    struct SREGS s;
    r.h.ah = 0x09;
    segread(&s);
    r.x.dx = 0; // (uint16)str;
    intdosx(&r, &r, &s);
}

/* file_read512.inc: Read 512 bytes from file - stub */
int read512FromFileIntoBuf(void)
{
    return 0;
}

/* file_error.inc: Print error and exit */
void errorAndExit(const char *msg)
{
    union REGS r;
    dos_printstring(msg);
    r.h.ah = 0x4C;
    r.h.al = 1;
    intdos(&r, &r);
}
