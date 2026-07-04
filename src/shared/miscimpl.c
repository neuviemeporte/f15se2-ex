/*
 * miscimpl.c - C implementations of shared miscellaneous routines.
 * Only compiled for the NO_ASM build.
 */

#include "inttype.h"
#include <dos.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static void miscdbg(const char *msg) {
    FILE *f = fopen("NOASM.LOG", "a");
    if (f) {
        fputs(msg, f);
        fputs("\r\n", f);
        fclose(f);
    }
}

void installCBreakHandler(void) {
}

void restoreCbreakHandler(void) {
}

/* pollJoystick / copyJoystickData live in joystick.c (SDL gamepad/joystick). */

void mystrcat(char *dst, const char *src) {
    strcat(dst, src);
}

void intDispatch(int intnum, uint8 *inreg, uint8 *outreg) {
    union REGS r;
    /* inreg[0] = AL, inreg[1] = AH based on stinit.c usage */
    r.h.al = inreg[0];
    r.h.ah = inreg[1];
    int86(intnum, &r, &r);
    outreg[0] = r.h.al;
    outreg[1] = r.h.ah;
}

void doNothing2(const char *msg, int a, int b, int c) {
}

int getTimeOfDay(void) {
    return (int)((unsigned long)((double)time(NULL) * 18.2065) & 0xFFFF);
}

int mystrlen(const char *s) {
    return strlen(s);
}

void nearmemset(void *dst, char val, int count) {
    memset(dst, val, count);
}

int loadOverlay(const char *filename) {
    return 0;
}

int doFcbSearch(void) {
    return -1;
}
