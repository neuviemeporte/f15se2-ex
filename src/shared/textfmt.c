/*
 * textfmt.c - centered text, string width, and integer-to-string (my_ltoa/my_itoa)
 */

#include "common.h"

#include <dos.h>

extern int FAR gfx_setFont(uint16 ch, uint16 font);
extern int gfx_getGlyphAdvance(uint32 codepoint, uint16 fontIdx);

/* Match gfx_impl.c text rendering semantics: valid UTF-8 sequences are Unicode
 * glyphs, malformed high bytes remain the original inline colour escapes. */
static int decodeUtf8Codepoint(const char *text, uint32 *codepointOut, int16 *byteCountOut) {
    const unsigned char *s = (const unsigned char *)text;
    unsigned int cp;
    int needed;
    int i;

    if (s[0] < 0x80) {
        *codepointOut = s[0];
        *byteCountOut = 1;
        return 1;
    }
    if ((s[0] & 0xe0) == 0xc0) {
        cp = (unsigned int)(s[0] & 0x1f);
        needed = 2;
        if (cp == 0) return 0; /* overlong ASCII */
    } else if ((s[0] & 0xf0) == 0xe0) {
        cp = (unsigned int)(s[0] & 0x0f);
        needed = 3;
    } else if ((s[0] & 0xf8) == 0xf0) {
        cp = (unsigned int)(s[0] & 0x07);
        needed = 4;
    } else {
        return 0;
    }
    for (i = 1; i < needed; i++) {
        if ((s[i] & 0xc0) != 0x80) return 0;
        cp = (cp << 6) | (unsigned int)(s[i] & 0x3f);
    }
    if ((needed == 2 && cp < 0x80) ||
        (needed == 3 && cp < 0x800) ||
        (needed == 4 && (cp < 0x10000 || cp > 0x10ffff)) ||
        (cp >= 0xd800 && cp <= 0xdfff)) {
        return 0;
    }
    *codepointOut = (uint32)cp;
    *byteCountOut = (int16)needed;
    return 1;
}

void drawStringCentered(int16 *page, const char *str, int16 startx, int16 y, int16 endx) {
    int16 width;
    width = stringWidth(page, str);
    drawStringAt(page, str, (endx - width) / 2 + startx, y);
}

int16 stringWidth(int16 *page, const char *str) {
    int16 n;
    const char *l;
    int16 j;
    l = str;
    j = page[6];
    n = 0;
    while (*l != '\0') {
        uint32 codepoint;
        int16 byteCount;
        unsigned char ch = (unsigned char)*l;
        if (!decodeUtf8Codepoint(l, &codepoint, &byteCount)) {
            codepoint = ch;
            byteCount = 1;
        }
        if (byteCount == 1 && (ch & 0x80)) {
            l++;
            continue;
        }
        n += gfx_getGlyphAdvance(codepoint, j);
        l += byteCount;
    }
    return n;
}

void my_ltoa(int32 value, char *buf) {
    int8 i, k;
    char *p;
    int8 n[6];
    p = buf;
    if (value < 0) {
        value = -value;
        *p = '-';
        p++;
    }
    n[0] = value % 0xa;
    value /= 0xa;
    n[1] = value % 0xa;
    value /= 0xa;
    n[2] = value % 0xa;
    value /= 0xa;
    n[3] = value % 0xa;
    value /= 0xa;
    n[4] = value % 0xa;
    value /= 0xa;
    n[5] = value % 0xa;
    i = 0;
    for (k = 5; k > 0; k--) {
        if (n[k] != 0) break;
    }
    do {
        if (k == 2 && i == 1) {
            *p = ',';
            p++;
        }
        *p = n[k] + '0';
        i = 1;
        p++;
    } while (--k >= 0);
    *p = '\0';
}

void my_itoa(int16 value, char *buf) {
    int8 n[6];
    int8 i, k;
    char *p;
    p = buf;
    if (value < 0) {
        value = -value;
        *p = 0x2d;
        p++;
    }
    n[0] = value % 0xa;
    value /= 0xa;
    n[1] = value % 0xa;
    value /= 0xa;
    n[2] = value % 0xa;
    value /= 0xa;
    n[3] = value % 0xa;
    value /= 0xa;
    n[4] = value % 0xa;
    value /= 0xa;
    n[5] = value % 0xa;
    i = 0;
    for (k = 5; k > 0; k--) {
        if (n[k] != 0) break;
    }
    do {
        if (k == 2 && i == 1) {
            *p = 0x2c;
            p++;
        }
        *p = n[k] + 0x30;
        i = 1;
        p++;
    } while (--k >= 0);
    *p = 0;
}
