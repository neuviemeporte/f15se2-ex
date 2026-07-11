/*
 * picimpl.c - PIC image LZW decoder.
 * Replaces: pic_decodepic.inc, pic_lzw.inc, pic_showpicfile.inc
 *
 * Two-phase decode: LZW produces raw bytes, then RLE (0x90 escape) is applied.
 */

#include "inttype.h"
#include "asset_compare.h"
#include "common.h"
#include "../gfx.h"
#include "../log.h"
#include <SDL3/SDL.h>
#include <stdlib.h>

/* Page backbuffers (gfx_impl.c): the decoder writes palette indices into these
 * 320x200 8-bit surfaces instead of the old fake DOS page segments. */
extern SDL_Surface *gfx_getCurPageSurface(void);
extern SDL_Surface *gfx_getPageSurface(int page);
extern SDL_Surface *gfx_getSpriteSurface(int handle);
extern SDL_Palette *gfx_getPalette(void);

/* SDL-backed raw read from a file handle (file_io.c). */
extern int fileReadRaw(SDL_IOStream *handle, void *dst, int count);
extern SDL_IOStream *openFile(const char *filename, int mode);
extern void fileClose(SDL_IOStream *io);

/* Hi-res title surface + present (gfx_impl.c). */
extern SDL_Surface *gfx_getHiResSurface(void);
extern void gfx_presentHiRes(void);

/* Pic decode work data */
extern uint8 picDecodedRowBuf[320];

/* File I/O state */
static uint8 picFileReadBuf[512];
static SDL_IOStream *picFileHandle;
static uint16 picBufPos;

/* Bit reader state - matches ASM exactly */
static uint16 picFileWord;         /* current 16-bit word being decoded */
static uint8 picRemainingBitCount; /* bits remaining in picFileWord */

/* LZW state */
static uint16 picCodeWidth;
static uint16 picMaxWidth;
static uint16 picNextCode;
static uint16 picMaxCodeAtWidth;
static uint16 picPrevCode;
static uint8 picFirstChar;
static uint16 picSignedFlag;

/* RLE state - persists across row calls (matches ASM picProcessFlag/picLookupResult) */
static uint8 rlePrevByte;
static uint8 rleProcessFlag; /* remaining RLE repeats */

/* Dictionary - 2048 entries max */
static uint16 dictParent[2048];
static uint8 dictChar[2048];

/* LZW output buffer (coroutine simulation) */
static uint8 lzwOutBuf[4096]; /* must cover the stackTop<4096 traversal guard */
static uint16 lzwOutPos;
static uint16 lzwOutLen;
static int16 lzwFirstCode; /* flag: first code after init/reset */

static void read512FromFile(void) {
    fileReadRaw(picFileHandle, picFileReadBuf, 512);
    picBufPos = 0;
}

static uint16 readCode(void) {
    uint16 bits;
    uint8 cl;
    uint16 word;
    uint16 mask;

    /* Shift out consumed bits to get remaining at bottom */
    bits = picFileWord >> (16 - picRemainingBitCount);
    cl = picRemainingBitCount;

    /* Need more bits? (always at most one new word since max code_width=12, max remaining=15) */
    if (cl < picCodeWidth) {
        if (picBufPos >= 512) {
            read512FromFile();
        }
        word = picFileReadBuf[picBufPos] | ((uint16)picFileReadBuf[picBufPos + 1] << 8);
        picBufPos += 2;
        picFileWord = word;
        bits |= (word << cl);
        cl += 16;
    }

    cl -= (uint8)picCodeWidth;
    picRemainingBitCount = cl;

    mask = (1u << picCodeWidth) - 1;
    return bits & mask;
}

static void resetDictionary(void) {
    uint16 i;
    for (i = 0; i < 256; i++) {
        dictParent[i] = 0xFFFF;
        dictChar[i] = (uint8)i;
    }
    picNextCode = 256;
    picCodeWidth = 9;
    picMaxCodeAtWidth = 511;
    /* NOTE: lzwFirstCode is NOT set here - only set on initial decode start.
       After a mid-stream reset, the next code still adds a dict entry
       using picPrevCode as parent. */
}

/*
 * Decode one LZW code into lzwOutBuf[].
 * Returns number of bytes produced.
 */
static uint16 decodeLZWStep(void) {
    uint16 code;
    uint16 origCode;
    uint16 stackTop;
    uint16 i;
    uint8 tmp;

    code = readCode();
    origCode = code;
    stackTop = 0;

    /* KwKwK case: code not yet in dictionary */
    if (code >= picNextCode) {
        lzwOutBuf[stackTop++] = picFirstChar;
        code = picPrevCode;
    }

    /* Traverse dictionary chain */
    while (dictParent[code] != 0xFFFF && code < 2048 && stackTop < 4096) {
        lzwOutBuf[stackTop++] = dictChar[code];
        code = dictParent[code];
    }
    if (code < 2048) {
        lzwOutBuf[stackTop++] = dictChar[code];
    }

    /* Root character = first char of this string */
    picFirstChar = dictChar[code];

    /* Add new dictionary entry (always, even on first code - matches ASM) */
    if (picNextCode < 2048) {
        dictParent[picNextCode] = picPrevCode;
        dictChar[picNextCode] = picFirstChar;
        picNextCode++;
    }

    /* Check if code width needs to grow */
    if (picNextCode > picMaxCodeAtWidth) {
        picCodeWidth++;
        picMaxCodeAtWidth = (uint16)((1UL << picCodeWidth) - 1);
        if (picCodeWidth > picMaxWidth) {
            resetDictionary();
        }
    }

    picPrevCode = origCode;

    /* Reverse the stack to get output order */
    for (i = 0; i < stackTop / 2; i++) {
        tmp = lzwOutBuf[i];
        lzwOutBuf[i] = lzwOutBuf[stackTop - 1 - i];
        lzwOutBuf[stackTop - 1 - i] = tmp;
    }

    return stackTop;
}

/*
 * Get next raw byte from LZW stream.
 * Uses buffered output from decodeLZWStep.
 */
static uint8 getNextLZWByte(void) {
    if (lzwOutPos >= lzwOutLen) {
        lzwOutLen = decodeLZWStep();
        lzwOutPos = 0;
    }
    return lzwOutBuf[lzwOutPos++];
}

/*
 * Decode one row of pixels with LZW + RLE (0x90 escape).
 * count = number of output bytes needed.
 */
static void decodeRow(uint8 *outBuf, uint16 count) {
    uint16 outPos;
    uint8 ch;
    uint8 rleCount;
    uint16 j;
    uint32 safetyLimit;

    outPos = 0;
    safetyLimit = 0;

    while (outPos < count) {
        if (++safetyLimit > 50000UL) {
            break;
        }

        /* First: handle any remaining RLE repeats from previous call */
        if (rleProcessFlag > 0) {
            outBuf[outPos++] = rlePrevByte;
            rleProcessFlag--;
            continue;
        }

        ch = getNextLZWByte();

        if (ch == 0x90) {
            /* RLE escape */
            rleCount = getNextLZWByte();
            if (rleCount == 0) {
                /* Literal 0x90 */
                outBuf[outPos++] = 0x90;
                rlePrevByte = 0x90;
            } else {
                /* Repeat prevByte (rleCount-1) more times */
                rleProcessFlag = rleCount - 1;
            }
        } else {
            outBuf[outPos++] = ch;
            rlePrevByte = ch;
        }
    }
}

/* Decode a PIC image into an 8-bit SDL_Surface. The decoder always produces
 * 320 palette indices per row; they are copied into the surface row by row
 * (clamped to the surface width), for as many rows as the surface is tall.
 *
 * Surfaces are linear 8bpp, so the decoded rows are written straight to
 * dst->pixels — no DOS page-segment addressing (MK_FP) or EGA bit-plane packing
 * through the Sequencer ports (the planar title path) is needed. */
/* Read the PIC header word and (re)initialise the LZW/RLE decode state.
 * Matches ASM picReadDataAndMakeDict + picInitRoutine. */
static void picDecodeInit(SDL_IOStream *handle) {
    picFileHandle = handle;
    read512FromFile();

    /* Read header word from file buffer - matches ASM picReadDataAndMakeDict */
    picFileWord = picFileReadBuf[picBufPos] | ((uint16)picFileReadBuf[picBufPos + 1] << 8);
    picBufPos += 2;

    /* ASM: AL = low byte. If bit 7 set: byte mode, NEG al to get max bits.
       If bit 7 clear: nibble mode, al is max bits directly. */
    {
        uint8 al = (uint8)(picFileWord & 0xFF);
        picSignedFlag = 0;
        if (al & 0x80) {
            /* Byte mode: negate to get true max bits (two's complement) */
            picSignedFlag = 1;
            al = (uint8)(-(int8)al);
        }
        picMaxWidth = al;
    }
    /* ASM sets picRemainingBitCount = 8 (high byte of header word is first data) */
    picRemainingBitCount = 8;

    /* Init LZW */
    resetDictionary();
    picPrevCode = 0; /* ASM: picSlotCounter is BSS-zeroed on first use */
    picFirstChar = 0;
    lzwOutPos = 0;
    lzwOutLen = 0;
    rlePrevByte = 0;
    rleProcessFlag = 0;
}

/* Decode one 320-pixel row into picDecodedRowBuf, handling nibble/byte mode. */
static void picDecodeNextRow(void) {
    static uint8 tempBuf[160];
    uint16 i;
    if (!picSignedFlag) {
        /* picByteUnsignedFlag=1 (bit7 clear): NIBBLE mode */
        decodeRow(tempBuf, 160);
        for (i = 0; i < 160; i++) {
            picDecodedRowBuf[i * 2] = tempBuf[i] & 0x0F;
            picDecodedRowBuf[i * 2 + 1] = (tempBuf[i] >> 4) & 0x0F;
        }
    } else {
        /* picByteUnsignedFlag=0 (bit7 set): BYTE mode */
        decodeRow(picDecodedRowBuf, 320);
    }
}

static void picDecodeToSurface(SDL_IOStream *handle, SDL_Surface *dst) {
    uint16 rowCount;
    uint16 rowWidth;
    uint16 row;
    uint8 *dstBase;
    int dstPitch;

    if (!dst) return;
    dstBase = (uint8 *)dst->pixels;
    dstPitch = dst->pitch;
    rowCount = (uint16)dst->h;
    rowWidth = (dst->w < 320) ? (uint16)dst->w : 320;

    picDecodeInit(handle);

    for (row = 0; row < rowCount; row++) {
        picDecodeNextRow();
        SDL_memcpy(dstBase + (size_t)row * dstPitch, picDecodedRowBuf, rowWidth);
    }
}

static void picDecodeTitle640ToSurface(SDL_IOStream *handle, SDL_Surface *dst) {
    uint8 *dstBase;
    int dstPitch;
    int row, col;

    if (!dst) return;
    dstBase = (uint8 *)dst->pixels;
    dstPitch = dst->pitch;
    SDL_memset(dstBase, 0, (size_t)dstPitch * dst->h);

    picDecodeInit(handle);
    for (row = 0; row < 700; row++) {
        int y = row >> 1;
        int xoff = (row & 1) ? 320 : 0;
        uint8 *rowp;
        picDecodeNextRow();
        if (y >= dst->h || xoff >= dst->w) continue;
        rowp = dstBase + (size_t)y * dstPitch + xoff;
        for (col = 0; col < 320 && xoff + col < dst->w; col++) {
            rowp[col] = picDecodedRowBuf[col] & 0x0F;
        }
    }
}

/* Defensive scratch target for a decode whose segment has no registered sprite
 * buffer. Live loads (loadPic / loadPicFromFile) always pass an allocated handle
 * that resolves to a real surface, so this only guards an unregistered-segment
 * decode from writing through a null pointer. */
static SDL_Surface *picScratchSurface(void) {
    static SDL_Surface *scratch;
    if (!scratch)
        scratch = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_INDEX8);
    return scratch;
}

static uint8 pngRgb8ToDac6(uint8 value) {
    return (uint8)((value * 63 + 127) / 255);
}

static void pngApplyEmbeddedPalette(SDL_Surface *src) {
    SDL_Palette *pal = SDL_GetSurfacePalette(src);
    uint8 dac[256 * 3];
    int i;

    if (!pal || pal->ncolors <= 0) return;
    for (i = 0; i < pal->ncolors && i < 256; i++) {
        dac[i * 3] = pngRgb8ToDac6(pal->colors[i].r);
        dac[i * 3 + 1] = pngRgb8ToDac6(pal->colors[i].g);
        dac[i * 3 + 2] = pngRgb8ToDac6(pal->colors[i].b);
    }
    gfx_setDacRange(0, (uint16)((pal->ncolors < 256) ? pal->ncolors : 256), dac);
}

static int pngNearestPaletteIndex(uint8 r, uint8 g, uint8 b) {
    SDL_Palette *pal = gfx_getPalette();
    int best = 0;
    int bestDist = 0x7fffffff;
    int i;

    if (!pal || pal->ncolors <= 0) return 0;
    for (i = 0; i < pal->ncolors && i < 256; i++) {
        int dr = (int)r - (int)pal->colors[i].r;
        int dg = (int)g - (int)pal->colors[i].g;
        int db = (int)b - (int)pal->colors[i].b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
            if (dist == 0) break;
        }
    }
    return best;
}

static void pngCopyIndexed(SDL_Surface *src, SDL_Surface *dst) {
    int x, y;

    if (src->w <= 0 || src->h <= 0 || dst->w <= 0 || dst->h <= 0) return;
    SDL_memset(dst->pixels, 0, (size_t)dst->pitch * dst->h);
    for (y = 0; y < dst->h; y++) {
        const int sy = (int)(((int64)y * src->h) / dst->h);
        const uint8 *srcRow = (const uint8 *)src->pixels + (size_t)sy * src->pitch;
        uint8 *dstRow = (uint8 *)dst->pixels + (size_t)y * dst->pitch;
        for (x = 0; x < dst->w; x++) {
            const int sx = (int)(((int64)x * src->w) / dst->w);
            dstRow[x] = srcRow[sx];
        }
    }
}

static int pngCopyTruecolor(SDL_Surface *src, SDL_Surface *dst) {
    SDL_Surface *rgba = SDL_ConvertSurface(src, SDL_PIXELFORMAT_RGBA32);
    int x, y;

    if (!rgba) return 0;
    if (rgba->w <= 0 || rgba->h <= 0 || dst->w <= 0 || dst->h <= 0) {
        SDL_DestroySurface(rgba);
        return 0;
    }
    if (SDL_MUSTLOCK(rgba)) SDL_LockSurface(rgba);
    if (SDL_MUSTLOCK(dst)) SDL_LockSurface(dst);
    SDL_memset(dst->pixels, 0, (size_t)dst->pitch * dst->h);
    for (y = 0; y < dst->h; y++) {
        const int sy = (int)(((int64)y * rgba->h) / dst->h);
        const uint8 *srcRow = (const uint8 *)rgba->pixels + (size_t)sy * rgba->pitch;
        uint8 *dstRow = (uint8 *)dst->pixels + (size_t)y * dst->pitch;
        for (x = 0; x < dst->w; x++) {
            const int sx = (int)(((int64)x * rgba->w) / dst->w);
            const uint8 *px = srcRow + sx * 4;
            dstRow[x] = (uint8)pngNearestPaletteIndex(px[0], px[1], px[2]);
        }
    }
    if (SDL_MUSTLOCK(dst)) SDL_UnlockSurface(dst);
    if (SDL_MUSTLOCK(rgba)) SDL_UnlockSurface(rgba);
    SDL_DestroySurface(rgba);
    return 1;
}

static uint8 dac6ToRgb8(uint8 v) {
    v = (uint8)((v & 0x3f) << 2);
    return (uint8)(v | (v >> 6));
}

static void comparePngPaletteWithLegacyDac(const char *filename,
                                           SDL_Surface *pngSurface,
                                           const SDL_Color *legacyColors,
                                           int legacyColorCount,
                                           const char *replacementPath) {
    SDL_Palette *pngPalette;
    uint8 legacyRgb[256 * 3];
    uint8 pngRgb[256 * 3];
    int i;

    if (!assetCompareEnabled() || !filename || !pngSurface || !legacyColors) return;
    if (pngSurface->format != SDL_PIXELFORMAT_INDEX8) return;
    pngPalette = SDL_GetSurfacePalette(pngSurface);
    if (!pngPalette || pngPalette->ncolors <= 0) {
        LogWarn(("asset replacement compare: %s indexed PNG has no embedded palette (%s)", filename, replacementPath));
        return;
    }

    for (i = 0; i < legacyColorCount && i < 256; i++) {
        legacyRgb[i * 3] = legacyColors[i].r;
        legacyRgb[i * 3 + 1] = legacyColors[i].g;
        legacyRgb[i * 3 + 2] = legacyColors[i].b;
    }
    for (i = 0; i < pngPalette->ncolors && i < 256; i++) {
        pngRgb[i * 3] = dac6ToRgb8(pngRgb8ToDac6(pngPalette->colors[i].r));
        pngRgb[i * 3 + 1] = dac6ToRgb8(pngRgb8ToDac6(pngPalette->colors[i].g));
        pngRgb[i * 3 + 2] = dac6ToRgb8(pngRgb8ToDac6(pngPalette->colors[i].b));
    }
    assetCompareRgbPalettes(filename, legacyRgb, legacyColorCount, pngRgb, pngPalette->ncolors, replacementPath);
}

static void compareReplacementPngWithLegacy(const char *filename,
                                            SDL_Surface *replacementDst,
                                            SDL_Surface *pngSurface,
                                            const SDL_Color *legacyColors,
                                            int legacyColorCount,
                                            const char *replacementPath,
                                            int pngW,
                                            int pngH) {
    SDL_IOStream *legacyIo;
    SDL_Surface *legacySurface;

    if (!assetCompareEnabled() || !filename || !replacementDst) return;

    legacySurface = SDL_CreateSurface(replacementDst->w, replacementDst->h, SDL_PIXELFORMAT_INDEX8);
    if (!legacySurface) {
        LogWarn(("asset replacement compare: failed to allocate PIC compare surface for %s", filename));
        return;
    }
    legacyIo = openFile(filename, 0);
    if (!legacyIo) {
        LogWarn(("asset replacement compare: failed to open legacy PIC/SPR %s for PNG %s", filename, replacementPath));
        SDL_DestroySurface(legacySurface);
        return;
    }
    picDecodeToSurface(legacyIo, legacySurface);
    fileClose(legacyIo);
    comparePngPaletteWithLegacyDac(filename, pngSurface, legacyColors, legacyColorCount, replacementPath);

    /* The loader copies into fixed-size page/sprite surfaces. Report source PNG
     * dimensions separately because a smaller PNG is zero-filled into the target
     * and may otherwise look like a generic pixel mismatch. */
    if (pngW != replacementDst->w || pngH != replacementDst->h) {
        LogWarn((
            "asset replacement compare: %s PNG source size differs from target surface "
            "(target=%dx%d png=%dx%d, png=%s)",
            filename,
            replacementDst->w,
            replacementDst->h,
            pngW,
            pngH,
            replacementPath
        ));
    } else {
        if (SDL_MUSTLOCK(legacySurface)) SDL_LockSurface(legacySurface);
        if (SDL_MUSTLOCK(replacementDst)) SDL_LockSurface(replacementDst);
        assetCompareIndexedPixels2D(
            filename,
            (const uint8 *)legacySurface->pixels,
            legacySurface->pitch,
            (const uint8 *)replacementDst->pixels,
            replacementDst->pitch,
            legacySurface->w < replacementDst->w ? legacySurface->w : replacementDst->w,
            legacySurface->h < replacementDst->h ? legacySurface->h : replacementDst->h,
            replacementPath
        );
        if (SDL_MUSTLOCK(replacementDst)) SDL_UnlockSurface(replacementDst);
        if (SDL_MUSTLOCK(legacySurface)) SDL_UnlockSurface(legacySurface);
    }
    SDL_DestroySurface(legacySurface);
}

static void compareReplacementTitle640PngWithLegacy(const char *filename,
                                                    SDL_Surface *replacementDst,
                                                    SDL_Surface *pngSurface,
                                                    const SDL_Color *legacyColors,
                                                    int legacyColorCount,
                                                    const char *replacementPath,
                                                    int pngW,
                                                    int pngH) {
    SDL_IOStream *legacyIo;
    SDL_Surface *legacySurface;

    if (!assetCompareEnabled() || !filename || !replacementDst) return;

    legacySurface = SDL_CreateSurface(replacementDst->w, replacementDst->h, SDL_PIXELFORMAT_INDEX8);
    if (!legacySurface) {
        LogWarn(("asset replacement compare: failed to allocate TITLE640 compare surface for %s", filename));
        return;
    }
    legacyIo = openFile(filename, 0);
    if (!legacyIo) {
        LogWarn(("asset replacement compare: failed to open legacy TITLE640 %s for PNG %s", filename, replacementPath));
        SDL_DestroySurface(legacySurface);
        return;
    }
    picDecodeTitle640ToSurface(legacyIo, legacySurface);
    fileClose(legacyIo);

    comparePngPaletteWithLegacyDac(filename, pngSurface, legacyColors, legacyColorCount, replacementPath);
    if (pngW != replacementDst->w || pngH != replacementDst->h) {
        LogWarn((
            "asset replacement compare: %s PNG source size differs from hi-res title surface "
            "(target=%dx%d png=%dx%d, png=%s)",
            filename,
            replacementDst->w,
            replacementDst->h,
            pngW,
            pngH,
            replacementPath
        ));
    }
    if (SDL_MUSTLOCK(legacySurface)) SDL_LockSurface(legacySurface);
    if (SDL_MUSTLOCK(replacementDst)) SDL_LockSurface(replacementDst);
    assetCompareIndexedPixels2D(
        filename,
        (const uint8 *)legacySurface->pixels,
        legacySurface->pitch,
        (const uint8 *)replacementDst->pixels,
        replacementDst->pitch,
        legacySurface->w < replacementDst->w ? legacySurface->w : replacementDst->w,
        legacySurface->h < replacementDst->h ? legacySurface->h : replacementDst->h,
        replacementPath
    );
    if (SDL_MUSTLOCK(replacementDst)) SDL_UnlockSurface(replacementDst);
    if (SDL_MUSTLOCK(legacySurface)) SDL_UnlockSurface(legacySurface);
    SDL_DestroySurface(legacySurface);
}

static int loadReplacementPngToSurface(const char *filename, SDL_Surface *dst) {
    char replacementPath[512];
    SDL_Surface *src;
    SDL_Color legacyColors[256];
    SDL_Palette *activePalette;
    int legacyColorCount = 0;
    int ok = 0;

    /* PIC/SPR replacements are media-first: the PNG itself supplies pixels and,
     * for indexed PNGs, the palette. JSON sidecars are converter metadata and
     * are not needed by the runtime loader. */
    if (!filename || !dst) return 0;
    if (!findReplacementAssetPath(filename, ".png", replacementPath, sizeof(replacementPath))) {
        return 0;
    }

    src = SDL_LoadPNG(replacementPath);
    if (!src) {
        LogWarn(("asset replacement: failed to load PNG %s (%s); using legacy PIC/SPR", replacementPath, SDL_GetError()));
        return 0;
    }

    activePalette = gfx_getPalette();
    if (activePalette && activePalette->ncolors > 0) {
        legacyColorCount = activePalette->ncolors < 256 ? activePalette->ncolors : 256;
        SDL_memcpy(legacyColors, activePalette->colors, (size_t)legacyColorCount * sizeof(legacyColors[0]));
    }

    if (src->format == SDL_PIXELFORMAT_INDEX8) {
        SDL_Palette *srcPalette = SDL_GetSurfacePalette(src);
        if (!srcPalette || srcPalette->ncolors <= 0) {
            LogWarn(("asset replacement: rejected indexed PNG %s without embedded palette; using legacy PIC/SPR", replacementPath));
            SDL_DestroySurface(src);
            return 0;
        }
        if (SDL_MUSTLOCK(src)) SDL_LockSurface(src);
        if (SDL_MUSTLOCK(dst)) SDL_LockSurface(dst);
        pngApplyEmbeddedPalette(src);
        pngCopyIndexed(src, dst);
        if (SDL_MUSTLOCK(dst)) SDL_UnlockSurface(dst);
        if (SDL_MUSTLOCK(src)) SDL_UnlockSurface(src);
        ok = 1;
    } else {
        ok = pngCopyTruecolor(src, dst);
    }

    if (ok) {
        compareReplacementPngWithLegacy(filename, dst, src, legacyColors, legacyColorCount, replacementPath, src->w, src->h);
        LogInfo(("asset replacement: loaded PNG %s for %s (%dx%d)", replacementPath, filename, src->w, src->h));
    } else {
        LogWarn(("asset replacement: could not convert PNG %s; using legacy PIC/SPR", replacementPath));
    }
    SDL_DestroySurface(src);
    return ok;
}

int loadReplacementPngToPage(const char *filename, int page) {
    (void)page;
    return loadReplacementPngToSurface(filename, gfx_getCurPageSurface());
}

int loadReplacementPngToHiResTitle(const char *filename) {
    char replacementPath[512];
    SDL_Surface *dst = gfx_getHiResSurface();
    SDL_Surface *src;
    SDL_Color legacyColors[256];
    SDL_Palette *activePalette;
    int legacyColorCount = 0;
    int ok = 0;

    if (!filename || !dst) return 0;
    if (!findReplacementAssetPath(filename, ".png", replacementPath, sizeof(replacementPath))) {
        return 0;
    }

    src = SDL_LoadPNG(replacementPath);
    if (!src) {
        LogWarn(("asset replacement: failed to load hi-res title PNG %s (%s); using legacy PIC", replacementPath, SDL_GetError()));
        return 0;
    }

    activePalette = gfx_getPalette();
    if (activePalette && activePalette->ncolors > 0) {
        legacyColorCount = activePalette->ncolors < 256 ? activePalette->ncolors : 256;
        SDL_memcpy(legacyColors, activePalette->colors, (size_t)legacyColorCount * sizeof(legacyColors[0]));
    }

    if (src->format == SDL_PIXELFORMAT_INDEX8) {
        SDL_Palette *srcPalette = SDL_GetSurfacePalette(src);
        if (!srcPalette || srcPalette->ncolors <= 0) {
            LogWarn(("asset replacement: rejected indexed hi-res title PNG %s without embedded palette; using legacy PIC", replacementPath));
            SDL_DestroySurface(src);
            return 0;
        }
        if (SDL_MUSTLOCK(src)) SDL_LockSurface(src);
        if (SDL_MUSTLOCK(dst)) SDL_LockSurface(dst);
        pngApplyEmbeddedPalette(src);
        pngCopyIndexed(src, dst);
        if (SDL_MUSTLOCK(dst)) SDL_UnlockSurface(dst);
        if (SDL_MUSTLOCK(src)) SDL_UnlockSurface(src);
        ok = 1;
    } else {
        ok = pngCopyTruecolor(src, dst);
    }

    if (ok) {
        compareReplacementTitle640PngWithLegacy(filename, dst, src, legacyColors, legacyColorCount, replacementPath, src->w, src->h);
        gfx_presentHiRes();
        LogInfo(("asset replacement: loaded hi-res title PNG %s for %s (%dx%d)", replacementPath, filename, src->w, src->h));
    } else {
        LogWarn(("asset replacement: could not convert hi-res title PNG %s; using legacy PIC", replacementPath));
    }
    SDL_DestroySurface(src);
    return ok;
}

int loadReplacementPngToSprite(const char *filename, int segment) {
    SDL_Surface *dst = gfx_getSpriteSurface(segment);
    return dst ? loadReplacementPngToSurface(filename, dst) : 0;
}

void showPicFile(SDL_IOStream *handle, int page) {
    if (!handle) return;
    (void)page; /* all pages are the single back buffer */
    /* Decode straight into the back buffer; the decoder overwrites every row. */
    picDecodeToSurface(handle, gfx_getCurPageSurface());
}

void decodePic(SDL_IOStream *handle, int segment) {
    /* A real sprite buffer (gfx_allocSpriteBuf) is backed by a surface; decode
     * into it. Anything else (no buffer registered) falls back to scratch. */
    SDL_Surface *dst = gfx_getSpriteSurface(segment);
    picDecodeToSurface(handle, dst ? dst : picScratchSurface());
}

void decodePicRaw(SDL_IOStream *handle, int segment) {
    /* Identical to decodePic in the original (both decode the PIC row-by-row
     * into the segment, fully overwriting it); resolve the same real surface. */
    SDL_Surface *dst = gfx_getSpriteSurface(segment);
    picDecodeToSurface(handle, dst ? dst : picScratchSurface());
}

/* picBlit is start.exe's EGA hi-res title decoder (Title640.pic). The original
 * (stcode.asm _picBlit) decodes 700 rows of 320 pixels and writes them through
 * the EGRAPHIC driver's planar fillRow/copyRow (slots 0x33/0x35):
 *   - each 320-pixel row is bit-packed into 4 EGA planes of 40 bytes;
 *   - copyRow rep-movs 40 bytes/plane to A000:rowOffset, rowOffset += 0x28 (40).
 * The EGA pitch is 80 bytes (640 px), so consecutive 40-byte writes tile the
 * framebuffer: even rows are the left 320 px of a scanline, odd rows the right
 * 320 px. Hence screen y = row/2, x = (row&1)*320 + col, for 350 scanlines.
 * Each decoded byte is the 4-bit EGA pixel value (fillRow uses bits 0-3), so we
 * mask to 0x0F and write straight into the 640x350 hi-res title surface, then
 * present it (the original wrote to visible VRAM, needing no explicit present).
 * The pageIndex arg selected the EGA page in the original; the port has a single
 * hi-res surface, so it is ignored. */
void picBlit(SDL_IOStream *handle, int pageIndex) {
    SDL_Surface *dst;
    uint8 *base;
    int pitch;
    int row, col;
    (void)pageIndex;
    if (!handle) return;

    dst = gfx_getHiResSurface();
    if (!dst) return;
    base = (uint8 *)dst->pixels;
    pitch = dst->pitch;

    /* Start from a black surface so the not-yet-decoded region below the sweep
     * line shows as the cleared screen the original mode-set produced. */
    SDL_memset(base, 0, (size_t)pitch * dst->h);

    picDecodeInit(handle);
    for (row = 0; row < 700; row++) {
        int y = row >> 1;
        int xoff = (row & 1) ? 320 : 0;
        uint8 *rowp;
        picDecodeNextRow();
        if (y >= dst->h) continue;
        rowp = base + (size_t)y * pitch + xoff;
        for (col = 0; col < 320; col++)
            rowp[col] = picDecodedRowBuf[col] & 0x0F;
        /* Present the partially filled surface every few scanlines to reproduce
         * the vsync'd reveal. */
        if ((row & 1) && (y & 7) == 7)
            gfx_presentHiRes();
    }
    gfx_presentHiRes();
}
