/* enmisc.c — split from enmain.c (loadWorldStrings + helpers), compiled with /Gs */
#include "gfx.h"
#include <dos.h>
#include "offsets.h"
#include "pointers.h"
#include "log.h"
#include "shared/common.h"
#include <stdlib.h>
#include "endata.h"
#include "enmisc.h"

/* Private helpers for this translation unit. */
void drawStringAtPos(int16 *s, const char far *str, int x, int y);
void drawFarString(int16 *s, const char far *str);
void farStrcpy(char *dst, const char far *src);
void restoreVideoMode(void);
void restoreInterrupts(void);

void loadWorldStrings(void) {
    int strIdx;
    int pos;
    /* World data is already in the END globals (worldExportToEnd, at mission end);
     * just (re)build the place-name pointer table over worldStringBuf. */
    worldStrings[0] = worldStringBuf;
    strIdx = 1;
    pos = 0;
    while (pos < 750) {
        if (worldStringBuf[pos] == '\0' && strIdx < 100) {
            worldStrings[strIdx++] = &worldStringBuf[pos + 1];
        }
        pos++;
    }
}

void drawStringAtPos(int16 *s, const char far *str, int x, int y) {
    s[4] = x;
    s[5] = y;
    drawFarString(s, str);
}

void farStrcpy(char *dst, const char far *src) {
    while ((*dst++ = *src++) != '\0');
}

void drawFarString(int16 *s, const char far *str) {
    char buf[200];
    farStrcpy(buf, str);
    gfx_drawString(s, buf);
}

void restoreVideoMode(void) {
}

void restoreInterrupts(void) {
}
