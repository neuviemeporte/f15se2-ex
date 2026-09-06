/*
 * gfx_impl.h — internal header for the pure-C MGRAPHIC.EXE replacement.
 *
 * The renderer's private surface: GfxState, the SDL backbuffer/sprite-surface
 * accessors, and the internal layout structs. Consumed by the renderer
 * implementation (gfx_impl.c) and the software-renderer code in eg3d*.c /
 * egsphere.c. Everything the rest of the game calls lives in the public gfx.h.
 */

#ifndef GFX_IMPL_H
#define GFX_IMPL_H

#include "gfx.h"
#include "inttype.h"
#include <dos.h>

/* Forward declaration so GfxState can hold SDL backbuffers without pulling the
 * full SDL header. */
struct SDL_Surface;

/* Shared gfx state. */
typedef struct {
    uint16 rowOffsets[200];               /* replaces g_rowOffsets[] */
    int16 blitOffset;                     /* replaces g_blitOffset  */
    uint8 modeFlag;                       /* replaces g_modeFlag = 1 */
    uint8 fillColor;                      /* replaces g_fillColor   */
    uint8 dacCounter;                     /* replaces g_dacCounter  */
    uint8 rowOffsetsReady;                /* replaces g_rowOffsetsReady */
    uint16 dacPhase;                      /* MGRAPHIC data-seg 0x1ccc — the DAC fire-cycle phase
                                           * counter advanced by gfx_dacCycle (slot 0x2e) each
                                           * frame (LCG x*5+1); seeded 0x4d2 in gfx_initState. */
    struct SDL_Surface *pageSurfaces[16]; /* SDL draw buffers. Pages 0/1 alias to index 0, the
                                           * single hidden back buffer everything composites into. */
    int shakeOffset;                      /* horizontal screen-shake in pixels, set by gfx_dacCycle
                                           * (the explosion CRTC start-address jitter) and applied
                                           * by the page present. 0 when not shaking. */
    uint64_t cycleClockNs;                /* wall-clock anchor (SDL_GetTicksNS) for advancing the
                                           * fire colour-cycle at the fixed ~60 Hz tick rate,
                                           * decoupled from the present/frame rate. */
} GfxState;

/* Initialise the shared GfxState defaults. Called once at startup. */
void gfx_initState(void);

/* Page backbuffers: each page index is backed by a 320x200 8-bit SDL_Surface.
 * The pic decoder renders into these; gfx_flipPage/gfx_commitPage push the visible
 * page (index 0) to the renderer. Both lazily create the surface on first use. */
struct SDL_Surface *gfx_getPageSurface(int page);
struct SDL_Surface *gfx_getCurPageSurface(void);

/* Sprite buffers: each is a 320x200 8-bit SDL_Surface addressed by a small
 * integer handle. The public gfx_allocSpriteBuf (gfx.h) creates one; the pic
 * decoder fills it and gfx_blitSprite reads it via this accessor. */
struct SDL_Surface *gfx_getSpriteSurface(int handle);

/* The live 256-entry VGA DAC palette (reflects gfx_setDac/gfx_setDacRange). The
 * GL backend reads RGB from it to colour decoded faces and to expand the 2D page
 * for the overlay composite. */
struct SDL_Palette;
struct SDL_Palette *gfx_getPalette(void);
void gfx_paletteRGB(int idx, uint8 *r, uint8 *g, uint8 *b);
int gfx_nearestPaletteIndexRgb8(uint8 r, uint8 g, uint8 b);
int gfx_paletteGeneration(void);

/* Current explosion screen-shake (0-3 virtual px, set by gfx_dacCycle). The GL
 * backend reads it mid-frame to offset the immediate 2D overlay, matching the
 * software present's shake shift. */
int gfx_getShakeOffset(void);

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
    int16 minX[220]; /* 0x000: per-row minimum dirty X (0x1B8 bytes = 440 = 220 words) */
    int16 maxX[220]; /* 0x1B8: per-row maximum dirty X */
} DirtyRectBuf;

/* Sprite blit parameter block.
 * Passed to slots 0x11/0x47/0x49 as int16* pointer.
 * The overlay loads BP from the pointer and accesses [bp+N].
 */
typedef struct {
    int16 page;   /* [0] destination page index */
    int16 srcX;   /* [1] source X in sprite sheet */
    int16 srcY;   /* [2] source Y in sprite sheet */
    int16 bufPtr; /* [3] sprite buffer segment */
    int16 dstX;   /* [4] destination X on page */
    int16 dstY;   /* [5] destination Y on page */
    int16 width;  /* [6] sprite width in pixels */
    int16 height; /* [7] sprite height in pixels */
    int16 flags;  /* [8] blit flags (0x10 = transparent) */
} SpriteBlitParams;

#endif /* GFX_IMPL_H */
