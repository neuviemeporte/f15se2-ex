/*
 * gfx_impl.h — Header for pure-C MGRAPHIC.EXE replacement
 *
 * When NO_ASM is defined, gfx_impl.c provides implementations for all
 * slot functions declared in slot.h. This header provides supporting
 * type definitions and documentation.
 */

#ifndef GFX_IMPL_H
#define GFX_IMPL_H

#include "inttype.h"
#include "pointers.h"

/* Forward declaration so GfxState can hold SDL backbuffers without pulling the
 * full SDL header. */
struct SDL_Surface;

/* Shared gfx state. */
typedef struct {
    uint16 rowOffsets[200]; /* replaces g_rowOffsets[] */
    uint16 curPageSeg;      /* replaces g_curPageSeg  */
    int16  blitOffset;      /* replaces g_blitOffset  */
    uint8  modeFlag;        /* replaces g_modeFlag = 1 */
    uint8  fillColor;       /* replaces g_fillColor   */
    uint8  dacCounter;      /* replaces g_dacCounter  */
    uint8  rowOffsetsReady; /* replaces g_rowOffsetsReady */
    uint16 pageSegs[16];    /* replaces g_pageSegs[]  */
    uint16 f15DataSeg;      /* FP_SEG of f15.exe's DGROUP — see Finding A.
                             * Lets gfx functions reach their own const tables
                             * (palettes, font tables) via far pointer when a
                             * child far-calls in with DS = the child's DGROUP. */
    uint8  displayPage;     /* MGRAPHIC cs:0x1a2 — the back-buffer page index
                             * returned by getDisplayPage (slot 0x2d). The frame
                             * is composited here (page 1) then presented to the
                             * visible page 0 by gfx_dacAnimate (slot 0x2c). */
    uint16 dacPhase;        /* MGRAPHIC data-seg 0x1ccc — the DAC fire-cycle phase
                             * counter advanced by gfx_dacCycle (slot 0x2e) each
                             * frame (LCG x*5+1); seeded 0x4d2 in gfx_initState. */
    struct SDL_Surface *pageSurfaces[16]; /* SDL draw buffers, one per page index
                             * . Lazily created 320x200 8-bit surfaces;
			     * gfx_flipPage pushes page 0 to the renderer. */
    int    curPage;         /* index of the current draw page. */
} GfxState;

/* Initialise the shared GfxState defaults. Called once at startup. */
void gfx_initState(void);

/* Page backbuffers: each page index is backed by a 320x200 8-bit SDL_Surface.
 * The pic decoder renders into these; gfx_flipPage/gfx_commitPage push the visible
 * page (index 0) to the renderer. Both lazily create the surface on first use. */
struct SDL_Surface *gfx_getPageSurface(int page);
struct SDL_Surface *gfx_getCurPageSurface(void);

/*
 * Reference structures documenting how the overlay accesses caller data.
 * These CANNOT be used in the asm data segments (which must maintain exact
 * byte layout), but serve as documentation for how the C implementation
 * interprets the data, and can be used with casts in C code.
 */

/* Dirty rectangle tracking buffer pair.
 * The overlay accesses these via hardcoded offset: maxBuf = minBuf + 0x1B8.
 * Each array has 220 entries (one per row-pair, covering 200 visible rows
 * with some padding). Values are X pixel coordinates (0-319).
 *
 * In asm: _dirtyMinBuf and _dirtyMaxBuf must be declared contiguously
 * with exactly 0x1B8 bytes between their starts.
 */
typedef struct {
    int16 minX[220];  /* 0x000: per-row minimum dirty X (0x1B8 bytes = 440 = 220 words) */
    int16 maxX[220];  /* 0x1B8: per-row maximum dirty X */
} DirtyRectBuf;

/* Sprite blit parameter block.
 * Passed to slots 0x11/0x47/0x49 as int16* pointer.
 * The overlay loads BP from the pointer and accesses [bp+N].
 */
typedef struct {
    int16 page;       /* [0] destination page index */
    int16 srcX;       /* [1] source X in sprite sheet */
    int16 srcY;       /* [2] source Y in sprite sheet */
    int16 bufPtr;     /* [3] sprite buffer segment */
    int16 dstX;       /* [4] destination X on page */
    int16 dstY;       /* [5] destination Y on page */
    int16 width;      /* [6] sprite width in pixels */
    int16 height;     /* [7] sprite height in pixels */
    int16 flags;      /* [8] blit flags (0x10 = transparent) */
} SpriteBlitParams;

#endif /* GFX_IMPL_H */
