#pragma once
// Minimal dos.h stub for 64-bit native builds

#ifndef _DOS_H_COMPAT64
#define _DOS_H_COMPAT64

// 16-bit calling-convention / pointer-size decorations. All no-ops natively;
// kept only so the ported DOS signatures still parse. Single source for the
// whole tree (uppercase FAR/NEAR/CDECL were formerly duplicated in pointers.h,
// the __far/__cdecl forms in egtypes.h).
#include <stdint.h>
typedef uint32_t uint32;
typedef uint16_t uint16;
typedef uint8_t uint8;
typedef int32_t int32;
typedef int16_t int16;
typedef int8_t int8;
#define cdecl
#define far
#define near
#define FAR
#define NEAR
#define CDECL

struct WORDREGS {
    uint16 ax;
    uint16 bx;
    uint16 cx;
    uint16 dx;
    uint16 si;
    uint16 di;
    uint16 cflag;
};

struct BYTEREGS {
    uint8 al, ah;
    uint8 bl, bh;
    uint8 cl, ch;
    uint8 dl, dh;
};

union REGS {
    struct WORDREGS x;
    struct BYTEREGS h;
};

struct SREGS {
    uint16 es;
    uint16 cs;
    uint16 ss;
    uint16 ds;
};

inline int int86(int16 intno, union REGS *inregs, union REGS *outregs) {
    (void)intno;
    (void)inregs;
    (void)outregs;
    return 0;
}
inline int16 int86x(int16 intno, union REGS *inregs, union REGS *outregs, struct SREGS *segregs) {
    (void)intno;
    (void)inregs;
    (void)outregs;
    (void)segregs;
    return 0;
}
inline void movedata(uint16 srcseg, uint16 srcoff, uint16 dstseg, uint16 dstoff, uint16 nbytes) {
    (void)srcseg;
    (void)srcoff;
    (void)dstseg;
    (void)dstoff;
    (void)nbytes;
}

// Non-standard C functions used by the codebase
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#if !defined(MSDOS) && !defined(_WIN32)
#include <unistd.h>
#else
#include <io.h>
#endif

inline int16 putch(int16 c) {
    (void)c;
    return 0;
}
/* Backed by the SDL keyboard layer in eginput.c (egame flight loop). */
int16 kbhit(void);

#if 0
inline char *itoa(int16 value, char *str, int16 base) {
    if (base == 10) {
        sprintf(str, "%d", value);
    } else if (base == 16) {
        sprintf(str, "%x", value);
    } else {
        str[0] = '\0';
    }
    return str;
}
#endif

inline char *strupr(char *s) {
    for (char *p = s; *p; p++) *p = toupper((uint8)*p);
    return s;
}

#endif // _DOS_H_COMPAT64
