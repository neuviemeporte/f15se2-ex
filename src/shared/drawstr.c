/*
 * drawstr.c - drawStringAt(); shared string drawing (split for link order)
 */

#include "common.h"

#include <dos.h>

extern void far gfx_drawString(int16 *pageNum, const char *string);

void (*g_textRecorder)(const char *str, int x, int y, int color, int font) = 0;

void drawStringAt(int16 *pageNum, const char *string, int x, int y) {
    pageNum[4] = x;
    pageNum[5] = y;
    /* pageNum[2] = draw colour, pageNum[6] = font index (PageDesc word view). */
    if (g_textRecorder) g_textRecorder(string, x, y, pageNum[2], pageNum[6]);
    gfx_drawString(pageNum, string);
}
