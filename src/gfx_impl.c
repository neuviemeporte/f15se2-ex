/*
 * gfx_impl.c Pure-C replacement for MGRAPHIC.EXE overlay (Mode 13h, 320x200x256)
 */

#include <SDL3/SDL.h>

#include "gfx_impl.h"
#include "gfx.h"
#include "struct.h"
#include "slot.h"
#include "output.h"
#include <dos.h>
#include <stdio.h>

#include "fontdata.h"

/* The SDL window and renderer are owned here: the graphics layer brings up the
 * video output and every gfx_* function presents through it. The original game
 * rendered to a 320x200 MCGA framebuffer; we present that through an SDL renderer
 * scaled to a resizable window. The 320x200 / 640x350 logical resolutions live
 * in gfx.h. */
#define INITIAL_WINDOW_WIDTH  640
#define INITIAL_WINDOW_HEIGHT 400

/* Fixed fire colour-cycle rate, independent of render frame rate. The original
 * stepped the cycle once per rendered frame; we pin it at 15 Hz so the pulse
 * looks the same regardless of how fast we present. */
#define FIRE_CYCLE_HZ 15
#define FIRE_CYCLE_NS (SDL_NS_PER_SECOND / FIRE_CYCLE_HZ)
static SDL_Window   *sdlWindow   = NULL;
static SDL_Renderer *sdlRenderer = NULL;

/* Forward declarations for the page-surface model, used by gfx_videoShutdown
 * before their definitions further down. */
static GfxState FAR *gfx_getState(void);
static SDL_Palette *gfxPalette;   /* shared 256-entry VGA DAC palette */

/* Bring up the SDL window and renderer. The 320x200 logical surface is stretched
 * to fill the resizable window (SDL_LOGICAL_PRESENTATION_STRETCH). */
void gfx_videoInit(void)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
        FATAL("SDL_Init failed: %s", SDL_GetError());

    sdlWindow = SDL_CreateWindow(
        "F-15 SE2 EX v0.0.1",
        INITIAL_WINDOW_WIDTH,
        INITIAL_WINDOW_HEIGHT,
        SDL_WINDOW_RESIZABLE);
    if (!sdlWindow)
        FATAL("Window creation failed: %s", SDL_GetError());

    /* Enable SDL_EVENT_TEXT_INPUT so the keyboard slots (ovlimpl.c) receive
     * shifted/localised ASCII for pilot-name entry. */
    SDL_StartTextInput(sdlWindow);

    sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
    if (!sdlRenderer)
        FATAL("Renderer creation failed: %s", SDL_GetError());

    SDL_SetRenderVSync(sdlRenderer, 1);

    if (!SDL_SetRenderLogicalPresentation(sdlRenderer, LOGICAL_WIDTH, LOGICAL_HEIGHT,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX))
        INFO("SetRenderLogicalPresentation failed: %s", SDL_GetError());
}

/* Toggle borderless-desktop fullscreen (Alt+Enter). */
void gfx_toggleFullscreen(void)
{
    bool full = (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(sdlWindow, !full);
}

void gfx_videoShutdown(void)
{
    GfxState FAR *s = gfx_getState();
    int i;
    for (i = 0; i < 16; i++) {
        if (s->pageSurfaces[i]) {
            SDL_DestroySurface(s->pageSurfaces[i]);
            s->pageSurfaces[i] = NULL;
        }
    }
    if (gfxPalette) { SDL_DestroyPalette(gfxPalette); gfxPalette = NULL; }
    if (sdlRenderer) SDL_DestroyRenderer(sdlRenderer);
    if (sdlWindow)   SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
}

/* File-scope object used only for its address: FP_SEG of its far pointer
 * yields f15.exe's DGROUP segment, recorded in GfxState.f15DataSeg so the
 * const tables below (palettes, font tables) can be reached via far pointer
 * regardless of which process's DS is current at call time (Finding A). */
static int dgroupAnchor;

/* The graphics layer's shared state. In the original game this lived in the
 * MGRAPHIC overlay segment so it survived far-calls from start/egame/end into
 * the overlay's namespace; in the merged single-process build every caller is
 * in the same address space, so it is just a file-scope global shared directly. */
static GfxState gfxState;

static GfxState FAR *gfx_getState(void) {
    return &gfxState;
}

/* ---- Page backbuffers (SDL surface model) ----------------------------------
 * The original game drew into 64KB DOS segments (pageSegs[]) with the visible
 * page at VGA segment 0xA000. Those segments are not real memory in the native
 * port, so each page is instead backed by a 320x200 8-bit SDL_Surface. The pic
 * decoder writes palette indices into the current page's surface; gfx_flipPage /
 * gfx_commitPage push the visible page (index 0) to the renderer.
 *
 * The surfaces share one 256-entry palette holding the standard VGA DAC table,
 * generated below (ported from vgapal.c). Per-image DAC palettes (gfx_setDac)
 * are not wired into it yet — that follows when the DAC path is ported. */

/* Standard VGA 256-colour palette generator, ported from vgapal.c. Fills the
 * module-static table in 64-level VGA values, then up-converts to 8-bit. */
static uint8 s_vgaPal[256 * 3];
static int   s_palWritten;

static void palAdd(int r, int g, int b)
{
    int i = s_palWritten * 3;
    s_vgaPal[i] = (uint8)r; s_vgaPal[i + 1] = (uint8)g; s_vgaPal[i + 2] = (uint8)b;
    s_palWritten++;
}
static void palAddGray(int v) { palAdd(v, v, v); }

static void palAdd16(int lo, int melo, int mehi, int hi)
{
    int r, g, b, i, h, l;
    for (i = 0; i < 16; i++) {
        if (i & 8) { h = hi;   l = melo; }
        else       { h = mehi; l = lo;   }
        r = g = b = l;
        if (i & 4) r = h;
        if (i & 2) g = h;
        if (i & 1) b = h;
        if (i == 6) g = melo;   /* brown, not dark yellow */
        palAdd(r, g, b);
    }
}

static int palAddRun(int start, int ch, int lo, int melo, int me, int mehi, int hi)
{
    int r = lo, g = lo, b = lo, i, up, v = 0;
    if (start & 4) r = hi;
    if (start & 2) g = hi;
    if (start & 1) b = hi;
    palAdd(r, g, b);
    up = (start & ch) ? 0 : 1;
    for (i = 0; i < 3; i++) {
        if (up) v = (i == 0) ? melo : (i == 1) ? me : mehi;
        else    v = (i == 0) ? mehi : (i == 1) ? me : melo;
        if      (ch == 4) r = v;
        else if (ch == 2) g = v;
        else              b = v;
        palAdd(r, g, b);
    }
    return start ^ ch;
}

static void palAddCycle(int lo, int melo, int me, int mehi, int hi)
{
    int hue = 1;
    hue = palAddRun(hue, 4, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 1, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 2, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 4, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 1, lo, melo, me, mehi, hi);
    (void)palAddRun(hue, 2, lo, melo, me, mehi, hi);
}

static SDL_Palette *gfx_buildPalette(void)
{
    static const int gray[16] = {0,5,8,11,14,17,20,24,28,32,36,40,45,50,56,63};
    SDL_Color colors[256];
    SDL_Palette *pal;
    int i;

    s_palWritten = 0;
    palAdd16(0, 21, 42, 63);
    for (i = 0; i < 16; i++) palAddGray(gray[i]);
    palAddCycle( 0, 16, 31, 47, 63);
    palAddCycle(31, 39, 47, 55, 63);
    palAddCycle(45, 49, 54, 58, 63);
    palAddCycle( 0,  7, 14, 21, 28);
    palAddCycle(14, 17, 21, 24, 28);
    palAddCycle(20, 22, 24, 26, 28);
    palAddCycle( 0,  4,  8, 12, 16);
    palAddCycle( 8, 10, 12, 14, 16);
    palAddCycle(11, 12, 13, 15, 16);
    for (i = 0; i < 8; i++) palAddGray(0);

    /* 64-level -> 8-bit: shift left 2 and replicate the top 2 bits. */
    for (i = 0; i < 256; i++) {
        int r = s_vgaPal[i*3] << 2, g = s_vgaPal[i*3+1] << 2, b = s_vgaPal[i*3+2] << 2;
        colors[i].r = (uint8)(r | (r >> 6));
        colors[i].g = (uint8)(g | (g >> 6));
        colors[i].b = (uint8)(b | (b >> 6));
        colors[i].a = 255;
    }
    pal = SDL_CreatePalette(256);
    if (pal) SDL_SetPaletteColors(pal, colors, 0, 256);
    return pal;
}

/* Lazily create the 320x200 8-bit surface backing a page index. */
static SDL_Surface *ensurePage(int page)
{
    GfxState FAR *s = gfx_getState();
    if (page < 0 || page >= 16) return NULL;
    if (!s->pageSurfaces[page]) {
        SDL_Surface *surf = SDL_CreateSurface(LOGICAL_WIDTH, LOGICAL_HEIGHT,
                                              SDL_PIXELFORMAT_INDEX8);
        if (!surf) FATAL("SDL_CreateSurface failed: %s", SDL_GetError());
        if (!gfxPalette) gfxPalette = gfx_buildPalette();
        if (gfxPalette) SDL_SetSurfacePalette(surf, gfxPalette);
        s->pageSurfaces[page] = surf;
    }
    return s->pageSurfaces[page];
}

struct SDL_Surface *gfx_getPageSurface(int page) { return ensurePage(page); }
struct SDL_Surface *gfx_getCurPageSurface(void) { return ensurePage(gfx_getState()->curPage); }
int FAR CDECL gfx_curPage(void) { return gfx_getState()->curPage; }

/* Map a legacy page *segment* token to its page index (defined below). */
static int gfx_pageForSeg(uint16 seg);

/* The SDL surface backing a legacy page-segment token (curPageSeg / pageSegs[]).
 * DOS segments are not real memory natively, so MK_FP(seg, off) is replaced by
 * writing into the page's surface pixels. NULL if the seg names no page. */
static SDL_Surface *gfx_surfaceForSeg(uint16 seg) {
    int page = gfx_pageForSeg(seg);
    return (page >= 0) ? ensurePage(page) : NULL;
}

/* Public: writable pixel base + stride of the page named by a legacy segment
 * token, for the egame HUD primitives (eghudr/eghudm) that drew straight into
 * the page segment. Replaces their MK_FP(seg, off). NULL if seg names no page.
 * The image is always LOGICAL_WIDTH x LOGICAL_HEIGHT. */
uint8 *gfx_pagePixelsForSeg(uint16 seg, int *pitchOut) {
    SDL_Surface *surf = gfx_surfaceForSeg(seg);
    if (!surf) return NULL;
    if (pitchOut) *pitchOut = surf->pitch;
    return (uint8 *)surf->pixels;
}

/* ---- Sprite buffers --------------------------------------------------------
 * The DOS build decoded sprite sheets into 64KB "segments" (allocBuffer) and
 * gfx_blitSprite read palette indices straight out of them. Natively each
 * sprite buffer is a 320x200 8-bit SDL surface addressed by a small integer
 * handle (which the caller keeps where the old build kept the segment value).
 * decodePic fills the surface; gfx_blitSprite reads it. */
#define MAX_SPRITE_BUFS 8
static SDL_Surface *s_spriteBufs[MAX_SPRITE_BUFS];

int gfx_allocSpriteBuf(void)
{
    int i;
    for (i = 0; i < MAX_SPRITE_BUFS; i++) {
        if (!s_spriteBufs[i]) {
            SDL_Surface *surf = SDL_CreateSurface(LOGICAL_WIDTH, LOGICAL_HEIGHT,
                                                  SDL_PIXELFORMAT_INDEX8);
            if (!surf) FATAL("SDL_CreateSurface failed: %s", SDL_GetError());
            if (!gfxPalette) gfxPalette = gfx_buildPalette();
            if (gfxPalette) SDL_SetSurfacePalette(surf, gfxPalette);
            s_spriteBufs[i] = surf;
            return i + 1;            /* 1-based handle; 0 means "none" */
        }
    }
    return 0;
}

struct SDL_Surface *gfx_getSpriteSurface(int handle)
{
    if (handle < 1 || handle > MAX_SPRITE_BUFS) return NULL;
    return s_spriteBufs[handle - 1];
}

void gfx_freeSpriteBuf(int handle)
{
    if (handle < 1 || handle > MAX_SPRITE_BUFS) return;
    if (s_spriteBufs[handle - 1]) {
        SDL_DestroySurface(s_spriteBufs[handle - 1]);
        s_spriteBufs[handle - 1] = NULL;
    }
}

/* While the 640x350 title is up, the renderer presents the separate hi-res
 * surface (see gfx_presentHiRes). video_setHiRes sets this; gfx_setMode13
 * clears it when the title is dismissed. */
static bool gfxHiResActive = false;

/* Push a page's surface to the renderer (vsync-paced present). */
static void gfx_presentPage(int page)
{
    SDL_Surface *surf;
    SDL_Texture *tex;
    /* During the hi-res title, the page-0 framebuffer still holds the prior
     * 320x200 image (e.g. labs.pic). Redirect generic flips/commits to the
     * hi-res title surface so frame-pacing presents don't clobber it. */
    if (gfxHiResActive) {
        gfx_presentHiRes();
        return;
    }
    surf = ensurePage(page);
    if (!surf || !sdlRenderer) return;
    tex = SDL_CreateTextureFromSurface(sdlRenderer, surf);
    if (!tex) return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_RenderClear(sdlRenderer);
    {
        /* Explosion screen-shake: the original jittered the CRTC display-start
         * byte (gfx_dacCycle); natively we shift the presented frame left by the
         * same 0-3 pixels. Drawn into the full logical 320x200 area otherwise. */
        int shake = gfx_getState()->shakeOffset;
        if (shake) {
            SDL_FRect dst = { (float)-shake, 0.0f, (float)LOGICAL_WIDTH, (float)LOGICAL_HEIGHT };
            SDL_RenderTexture(sdlRenderer, tex, NULL, &dst);
        } else {
            SDL_RenderTexture(sdlRenderer, tex, NULL, NULL);
        }
    }
    SDL_RenderPresent(sdlRenderer);
    SDL_DestroyTexture(tex);
}

/* Hi-res (640x350) title surface. The EGA-title path (picimpl.c picBlit)
 * decodes the planar Title640.pic into this surface; gfx_presentHiRes pushes
 * it. video_setHiRes already switched the renderer's logical presentation to
 * 640x350; gfx_setMode13 restores 320x200 once the title is dismissed. */
static SDL_Surface *gfxHiResSurface;

SDL_Surface *gfx_getHiResSurface(void)
{
    if (!gfxHiResSurface) {
        gfxHiResSurface = SDL_CreateSurface(HIRES_WIDTH, HIRES_HEIGHT,
                                            SDL_PIXELFORMAT_INDEX8);
        if (!gfxHiResSurface) FATAL("SDL_CreateSurface failed: %s", SDL_GetError());
        if (!gfxPalette) gfxPalette = gfx_buildPalette();
        if (gfxPalette) SDL_SetSurfacePalette(gfxHiResSurface, gfxPalette);
    }
    return gfxHiResSurface;
}

void gfx_presentHiRes(void)
{
    SDL_Surface *surf = gfx_getHiResSurface();
    SDL_Texture *tex;
    if (!surf || !sdlRenderer) return;
    tex = SDL_CreateTextureFromSurface(sdlRenderer, surf);
    if (!tex) return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_RenderClear(sdlRenderer);
    SDL_RenderTexture(sdlRenderer, tex, NULL, NULL);
    SDL_RenderPresent(sdlRenderer);
    SDL_DestroyTexture(tex);
}

/* Initialize row offset table */
static void initRowOffsets(void)
{
    int i;
    if (gfx_getState()->rowOffsetsReady) return;
    for (i = 0; i < 200; i++)
        gfx_getState()->rowOffsets[i] = (uint16)(i * 320);
    gfx_getState()->rowOffsetsReady = 1;
}

/* ---- Slot 0x00: gfx_allocPage ---- */
int FAR CDECL gfx_allocPage(int n)
{
    uint16 seg;
    initRowOffsets();
    if (n < 0 || n >= 16) return 0;
    /* In the original game, the MGRAPHIC overlay persists across exes and
     * gfx_setMode13 was already called by start.exe. In our NO_ASM build,
     * each exe has fresh static state. Bootstrap mode 13h on first use. */
    if (gfx_getState()->pageSegs[0] == 0) {
        gfx_setMode13();
    }
    /* DOS allocated a fresh 64KB segment per page; natively each page is an SDL
     * surface addressed by index. Hand out a stable, unique non-zero token per
     * page (gfx_pageForSeg maps it back) and create the backing surface now. */
    seg = (uint16)(0xC000 + n);
    ensurePage(n);
    /* Don't overwrite page 0 if it's already the VGA framebuffer.
     * end.exe draws directly to 0xA000; gfx_setPageN(0) must keep
     * returning 0xA000 so all rendering is immediately visible. */
    if (n != 0 || gfx_getState()->pageSegs[0] != 0xA000) {
        gfx_getState()->pageSegs[n] = seg;
    }
    return (int)seg;
}

/* ---- Slot 0x3c: gfx_setMode13 ----
 * Switch to the 320x200 game resolution. Formerly an INT 10h mode-13h set; now
 * the renderer presents the 320x200 logical surface through SDL. This is also
 * the lo-res restore after the (possibly hi-res) title. */
void FAR CDECL gfx_setMode13(void)
{
    GfxState FAR *s; /* Declare at function level for MSC small model */

    initRowOffsets();

    SDL_SetRenderLogicalPresentation(sdlRenderer, LOGICAL_WIDTH, LOGICAL_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    gfxHiResActive = false;

    s = gfx_getState();
    s->pageSegs[0] = 0xA000;
    s->curPageSeg = s->pageSegs[1]; /* default to back buffer */
    s->curPage = 1;
    s->modeFlag = 1;
}

/* Title-screen hi-res attempt: ask SDL to present at 640x350 and report whether it took. */
bool video_setHiRes(void)
{
    bool ok = SDL_SetRenderLogicalPresentation(sdlRenderer, HIRES_WIDTH, HIRES_HEIGHT,
                                          SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (ok) gfxHiResActive = true;
    return ok;
}

/* ---- Slot 0x45: gfx_waitRetrace ---- */
void FAR CDECL gfx_waitRetrace(void)
{
    /* Frame pacing now comes from the vsync'd present in gfx_flipPage/gfx_presentPage. */
}

/* ---- Slot 0x46: gfx_flipPage ---- */
void FAR CDECL gfx_flipPage(void)
{
    gfx_presentPage(0);
}

/* ---- Slot 0x4b: gfx_storeBufPtr ---- */
void FAR CDECL gfx_storeBufPtr(uint16 seg, int pageIdx)
{
    GfxState FAR *s = gfx_getState();
    if (pageIdx >= 0 && pageIdx < 16) {
        s->pageSegs[pageIdx] = seg;
    }
    return;
}

/* ---- Slot 0x3b: gfx_clearPage ----
 * Register-called via the _gfx_clearPage asm shim (regshim.asm): the caller
 * passes the target segment in ES (decodePic sets it directly, showPicFile via
 * _gfx_getPageSeg). The shim pushes ES as `seg`. We record it as curPageSeg so
 * the subsequent fillRow/fillRow2 (which write curPageSeg) land in this buffer,
 * then clear it — matching MGRAPHIC's slot 0x3b (`rep stosw` to ES:0). */
void FAR CDECL gfx_clearPage(uint16 seg)
{
    GfxState FAR *s = gfx_getState();
    SDL_Surface *surf;
    int y;
    s->curPageSeg = seg;
    surf = gfx_surfaceForSeg(seg);
    if (!surf) return;
    for (y = 0; y < surf->h; y++)
        SDL_memset((uint8 *)surf->pixels + (size_t)y * surf->pitch, 0, surf->w);
}

/* ---- Slot 0x0e: gfx_setPageN ---- */
void FAR CDECL gfx_setPageN(uint16 pageNum)
{
    GfxState FAR *s = gfx_getState();
    s->curPage = (pageNum < 16) ? (int)pageNum : 0;
    /* Don't re-init mode if page 0 is VGA framebuffer */
    if (s->pageSegs[0] == 0xA000) {
        s->curPageSeg = s->pageSegs[pageNum];
        return;
    }
    initRowOffsets();
    /* Bootstrap mode 13h for first use */
    if (s->pageSegs[0] == 0) {
        gfx_setMode13();
    }
    s->curPageSeg = s->pageSegs[pageNum];
    return;
}

/* ---- Slot 0x0f: gfx_setCurPageSeg ---- */
/* Slot 0x0f: register-called via the _gfx_setCurPageSeg shim — AX = segment;
 * curPageSeg = AX. MGRAPHIC's slot 0x0f is `mov [curPageSeg],ax` — a SETTER (the
 * getter is slot 0x10). clearRect saves curPageSeg via 0x10 and restores it here. */
void FAR CDECL gfx_setCurPageSeg(uint16 seg)
{
    gfx_getState()->curPageSeg = seg;
}

/* ---- Slot 0x17: gfx_getBufSize ---- */
int FAR CDECL gfx_getBufSize(void)
{
    return (int)0xFA00; /* baked constant 64000 (cs:0x1d2 in MGRAPHIC) */
}

/* ---- Slots 0x3a/0x38/0x33/0x35: register-called by the ASM pic renderer ----
 * The overlay slot symbols (gfx_getRowOffset/gfx_getPageSeg/gfx_fillRow/
 * gfx_copyRow) are tiny asm shims in regshim.asm that marshal the register
 * args (DI/SI/BP/BX) into cdecl stack args and call these *_impl bodies. DS is
 * the caller's DGROUP throughout, so `srcBuf` is read as a near pointer. */

/* Slot 0x3a: DI = y -> AX = row byte offset (y*320). */
int FAR CDECL gfx_getRowOffset(int y)
{
    GfxState FAR *s = gfx_getState();
    initRowOffsets();
    if (y >= 0 && y < 200)
        return (int)s->rowOffsets[y];
    return (int)((uint16)y * 320);
}

/* Slot 0x38: SI = page -> select it as current page, return its segment. */
int FAR CDECL gfx_getPageSeg(uint16 page)
{
    GfxState FAR *s = gfx_getState();
    if (page < 16) {
        s->curPageSeg = s->pageSegs[page];
        s->curPage = (int)page;
    }
    return (int)s->curPageSeg;
}

/* Slot 0x33: DI = rowOffset, BP = srcBuf (caller DS), BX = rowNum.
 * Copy one 320-byte decoded row into the current page (MCGA: direct write). */
void FAR CDECL gfx_fillRow(uint16 rowOffset, uint16 srcBuf, uint16 rowNum)
{
    GfxState FAR *s = gfx_getState();
    const uint8 *src = (const uint8 *)srcBuf;          /* near ptr, caller's DS */
    SDL_Surface *surf = gfx_surfaceForSeg(s->curPageSeg);
    int row, col;
    (void)rowNum;
    if (!surf) return;
    row = (int)(rowOffset / LOGICAL_WIDTH);  /* rowOffset is a linear y*320 index */
    if (row < 0 || row >= surf->h) return;
    {
        uint8 *dst = (uint8 *)surf->pixels + (size_t)row * surf->pitch;
        for (col = 0; col < LOGICAL_WIDTH && col < surf->w; col++)
            dst[col] = src[col];
    }
}

/* Slot 0x35: DI = rowOffset. In MCGA the row is already in the page (fillRow
 * wrote directly), so this is a no-op. */
void FAR CDECL gfx_copyRow(uint16 rowOffset)
{
    (void)rowOffset;
}

/* ---- Slot 0x3f: gfx_getModecode ---- */
int FAR CDECL gfx_getModecode(void)
{
    return 3; /* MCGA mode code */
}

/* ---- Slot 0x22: gfx_nop22 ---- */
void FAR CDECL gfx_nop22(void)
{
    return; /* bare RETF in MGRAPHIC — does NOT reset blitOffset */
}

/* ---- Slot 0x1a: gfx_setBlitOffset ---- */
void FAR CDECL gfx_setBlitOffset(int offset)
{
    GfxState FAR *s = gfx_getState();
    s->blitOffset = (uint16)offset;
    return;
}

/* ---- Slot 0x25: gfx_dirtyRect ---- */
void FAR CDECL gfx_dirtyRect(void)
{
    /* Register-called in overlay — stub for now */
    return;
}

/* Slot 0x01 (gfx_fillDirty), 0x02 (gfx_blitTransparent), 0x03 (gfx_blitVariant),
 * 0x04 (gfx_copyBlock) and 0x06 (gfx_drawStringUnclipped) are register-called
 * glyph slots — their slot symbols are asm shims in regshim.asm that marshal
 * BP (param block) + BX (string) into gfx_drawStringClipped_impl (defined below
 * next to gfx_drawString). No C stub here; the shim provides the symbol. */

/* ---- Font data ---- */

/* Font width tables extracted from MGRAPHIC.EXE */
static const uint8 g_font1_widths[96] = {
    5,2,4,7,6,8,8,2,3,3,6,6,3,4,2,8,8,3,6,6,7,6,6,6,6,6,2,3,5,5,5,6,
    8,8,6,7,8,6,6,8,8,2,6,6,6,8,8,8,6,8,6,6,6,6,6,8,8,8,8,4,8,4,6,8,
    2,6,6,5,6,6,4,6,6,2,3,6,2,8,6,6,6,6,5,6,4,6,6,8,6,6,6,4,2,4,5,8};
static const uint8 g_font3_widths[96] = {
    3,2,4,5,4,5,5,2,3,3,6,4,3,4,2,4,5,3,5,5,5,5,5,5,5,5,2,3,4,4,4,5,
    5,5,5,5,5,5,5,5,5,2,5,5,5,6,5,5,5,5,5,5,4,5,6,6,6,6,6,3,6,3,4,5,
    2,4,4,4,4,4,3,4,4,2,3,4,2,6,4,4,4,4,4,4,4,4,4,6,4,4,4,4,2,3,3,5};
static const uint8 g_font4_widths[96] = {
    4,2,4,6,4,7,6,2,3,4,4,4,3,5,2,7,5,3,5,5,6,5,5,5,5,5,2,3,4,5,4,5,
    7,6,5,6,6,5,5,7,7,2,5,5,5,6,6,7,5,7,5,5,4,5,6,8,7,8,7,3,7,3,6,6,
    2,5,5,4,5,5,3,5,5,2,3,5,2,8,5,5,5,5,5,5,4,5,6,8,5,5,5,4,2,4,5,7};
static const uint8 g_font5_widths[96] = {
    3,2,4,5,4,5,5,2,3,3,6,4,3,4,2,4,5,3,5,5,5,5,5,5,5,5,2,3,4,4,4,5,
    5,5,5,5,5,5,5,5,5,2,5,5,5,6,5,5,5,5,5,5,4,5,6,6,6,6,6,3,6,3,4,5,
    2,4,4,4,4,4,3,4,4,2,3,4,2,6,4,4,4,4,4,4,4,4,4,6,4,4,4,4,2,3,3,5};

/* Font index 0 is the small in-flight HUD font (3x5, fixed advance 4). The
 * original registers it at runtime via low-memory pointer tables MGRAPHIC reads
 * (0:0xE2 width / 0:0xEE glyph / 0:0xFA rowsize); it is not statically present
 * in MGRAPHIC.EXE (which only bakes fonts 1,3,4,5). g_font0_* was captured from
 * the live glyph engine — see fontdata.h. All advances are 4 (fixed pitch). */
static const uint8 g_font0_widths[96] = {
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
    4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4};
static const uint8 * const g_fontWidthTables[8] = {
    g_font0_widths, g_font1_widths, NULL, g_font3_widths,
    g_font4_widths, g_font5_widths, NULL, NULL
};
static const uint8 g_fontHeightsArr[8] = {5, 8, 7, 6, 7, 6, 4, 0};
static const uint8 g_fontMaxWidths[8] = {4, 8, 6, 6, 8, 6, 0, 0};

/* Bitmap pointers per font index — NULL means no bitmap available */
static uint8 *g_fontBitmapPtrs[8] = {
    (uint8 *)g_font0_bitmaps, (uint8 *)g_font1_bitmaps, NULL, (uint8 *)g_font3_bitmaps,
    (uint8 *)g_font4_bitmaps, (uint8 *)g_font5_bitmaps, NULL, NULL
};
static const uint8 g_fontBitmapRowSize[8] = {5, 8, 0, 6, 7, 6, 0, 0};

/* ---- Shared glyph engine (slots 0x01-0x06) ----
 * MGRAPHIC has one core blitter (0x04 @0x4ab) that the string slots fall into
 * after running 0-3 clip stages. The clip stages cut partial glyphs at the
 * edges of a window whose bounds live in the param block:
 *   [bp+0xe]/word[7] = top Y     [bp+0x10]/word[8] = bottom Y
 *   [bp+0x12]/word[9] and [bp+0x14]/word[10] = the two X bounds
 * Rather than reproduce MGRAPHIC's char-count clip math (which divides by the
 * font width table — different data in this build), we render with our proven C
 * font path (same as the working slot 0x05) and clip every glyph pixel to the
 * window rectangle. Edge glyphs are pixel-clipped; this confines each tape/label
 * to its sub-window exactly as the original clip chain does. The param-block
 * field mapping (page[0], color[2], x[4], y[5], font[6]) is shared with the
 * cdecl slot 0x05 caller, so one core serves both. */
static void drawStringCore(int16 *params, const char *string,
                           int clipL, int clipR, int clipT, int clipB)
{
    SDL_Surface *surf;
    uint8 *base;
    int pitch, surfW, surfH;
    int x, y, color;
    int charIdx;
    uint8 ch;
    int row, col;
    uint16 fontIdx;
    uint8 height, rowSize;
    uint8 *bitmaps;
    const uint8 *widthTab;

    if (!string || !params) return;

    /* The font tables are file-scope arrays in this module; in the merged
     * single-process build they are reached directly as native pointers (the
     * old DOS build had to re-base them on f15's DGROUP via far pointer). */
    x = (int)params[4];
    y = (int)params[5];
    color = (int)params[2];
    fontIdx = (uint16)params[6] & 7;
    height   = g_fontHeightsArr[fontIdx];
    rowSize  = g_fontBitmapRowSize[fontIdx];
    bitmaps  = g_fontBitmapPtrs[fontIdx];
    widthTab = g_fontWidthTables[fontIdx];

    /* The page is backed by an SDL surface (same buffer the pic decoder and
     * clearRect target); glyph pixels are written straight into it. */
    surf = gfx_getPageSurface((int)params[0]);
    if (!surf) return;
    base  = (uint8 *)surf->pixels;
    pitch = surf->pitch;
    surfW = surf->w;
    surfH = surf->h;

    for (charIdx = 0; string[charIdx] != 0 && charIdx < 256; charIdx++) {
        ch = (uint8)string[charIdx];

        /* Inline color escape: chars >= 0x80 change the text color.
         * The new color is (ch & 0x7F). No glyph is drawn. */
        if (ch & 0x80) {
            color = ch & 0x7F;
            continue;
        }

        if (x > clipR) break;          /* rest of the string is right of window */

        if (bitmaps && ch >= 0x20) {
            uint8 *glyph = bitmaps + (ch - 0x20) * (uint16)rowSize;
            for (row = 0; row < height; row++) {
                int py = y + row;
                uint8 bits;
                uint8 *dstRow;
                if (py < clipT || py > clipB) continue;   /* row outside window */
                if (py < 0 || py >= surfH) continue;       /* off the surface */
                bits = glyph[row];
                dstRow = base + (size_t)py * pitch;
                for (col = 0; col < 8; col++) {
                    int px = x + col;
                    if ((bits & 0x80) && px >= clipL && px <= clipR &&
                        px >= 0 && px < surfW)
                        dstRow[px] = (uint8)color;
                    bits <<= 1;
                }
            }
        }
        x += widthTab ? (ch >= 0x20 ? widthTab[ch-0x20] : 8) : 8;
    }

    /* Update x position and color in the param block (start/end's menu text
     * relies on the advanced x to chain successive draws). */
    params[4] = (int16)x;
    params[2] = (int16)color;
}

/* ---- Slot 0x05: gfx_drawString (cdecl, unclipped) ---- */
void FAR CDECL gfx_drawString(int16 *pageNum, const char *string)
{
    drawStringCore(pageNum, string, 0, 319, 0, 199);
    return;
}

/* Register-called glyph slots (0x01/0x02/0x03/0x06) — entered via regshim.asm
 * with BP = param block (near, caller DS) and BX = string. `mode` selects which
 * clip stages the variant ran: bit0 = horizontal window (params word 9/10),
 * bit1 = vertical window (params word 7/8). The two X (resp. Y) bounds are
 * stored without a fixed min/max order across blocks, so normalise them. */
void gfx_drawStringClipped_impl(int16 *params, const char *string, int mode)
{
    int clipL = 0, clipR = 319, clipT = 0, clipB = 199;
    if (!params) return;
    if (mode & 1) {                    /* horizontal clip window */
        int bound1 = (int)params[9], bound2 = (int)params[10];
        clipL = bound1 < bound2 ? bound1 : bound2;
        clipR = bound1 < bound2 ? bound2 : bound1;
    }
    if (mode & 2) {                    /* vertical clip window */
        int bound1 = (int)params[7], bound2 = (int)params[8];
        clipT = bound1 < bound2 ? bound1 : bound2;
        clipB = bound1 < bound2 ? bound2 : bound1;
    }
    if (clipL < 0) clipL = 0;
    if (clipR > 319) clipR = 319;
    if (clipT < 0) clipT = 0;
    if (clipB > 199) clipB = 199;
    drawStringCore(params, string, clipL, clipR, clipT, clipB);
}

/* Slots 0x01/0x02/0x03/0x04/0x06: the clipped glyph variants. Each selects which
 * clip stages run (bit0 = horizontal window, bit1 = vertical window) and shares
 * the gfx_drawStringClipped_impl core. They take the param block + string as real
 * arguments (formerly marshalled from BP/BX by regshim.asm). */
void FAR CDECL gfx_fillDirty(int16 *params, const char *string)           { gfx_drawStringClipped_impl(params, string, 2); }
void FAR CDECL gfx_blitTransparent(int16 *params, const char *string)     { gfx_drawStringClipped_impl(params, string, 1); }
void FAR CDECL gfx_blitVariant(int16 *params, const char *string)         { gfx_drawStringClipped_impl(params, string, 1); }
void FAR CDECL gfx_copyBlock(int16 *params, const char *string)           { gfx_drawStringClipped_impl(params, string, 0); }
void FAR CDECL gfx_drawStringUnclipped(int16 *params, const char *string) { gfx_drawStringClipped_impl(params, string, 3); }

/* ---- Slot 0x2a: gfx_copyRect ---- */
/* Copy a width x height rect between two page surfaces. The original streamed
 * rows between DOS page segments via movedata; natively the pages are SDL
 * surfaces, so copy row-by-row between them (clipped to each surface). */
void FAR CDECL gfx_copyRect(int srcPage, uint16 srcX, uint16 srcY,
                            int dstPage, uint16 dstX, uint16 dstY,
                            int width, int height)
{
    SDL_Surface *src = ensurePage(srcPage);
    SDL_Surface *dst = ensurePage(dstPage);
    int row;

    if (!src || !dst || width <= 0 || height <= 0) return;

    for (row = 0; row < height; row++) {
        int sy = (int)srcY + row;
        int dy = (int)dstY + row;
        int w  = width;
        if (sy < 0 || sy >= src->h || dy < 0 || dy >= dst->h) continue;
        if ((int)srcX + w > src->w) w = src->w - (int)srcX;
        if ((int)dstX + w > dst->w) w = dst->w - (int)dstX;
        if (w <= 0) continue;
        SDL_memmove((uint8 *)dst->pixels + (size_t)dy * dst->pitch + dstX,
                    (uint8 *)src->pixels + (size_t)sy * src->pitch + srcX,
                    (size_t)w);
    }
    return;
}

/* ---- Slot 0x29: gfx_switchColor ---- */
void FAR CDECL gfx_switchColor(int16 *pageDesc, int x1, int y1,
                               int x2, int y2, int oldColor, int newColor)
{
    SDL_Surface *surf = gfx_getPageSurface((int)*pageDesc);
    uint8 *base;
    int pitch, row, col;

    if (!surf) return;
    base  = (uint8 *)surf->pixels;
    pitch = surf->pitch;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= surf->w) x2 = surf->w - 1;
    if (y2 >= surf->h) y2 = surf->h - 1;

    for (row = y1; row <= y2; row++) {
        uint8 *dst = base + (size_t)row * pitch;
        for (col = x1; col <= x2; col++) {
            if (dst[col] == (uint8)oldColor)
                dst[col] = (uint8)newColor;
        }
    }
    return;
}

/* clearRect - fill a rectangular region of a page with a solid colour.
 * Called with a PageDesc pointer: word 0 is the page index, word 3 the fill
 * colour. Writes straight into the page's backing SDL surface — the same buffer
 * the pic decoder and blitters target — so no DOS segment is involved. */
void clearRect(int16 *pageNum, int x1, int y1, int x2, int y2)
{
    SDL_Surface *surf = gfx_getPageSurface((int)*pageNum);
    uint8 color = (uint8)pageNum[3];
    uint8 *base;
    int pitch, row, col;

    if (!surf) return;
    base  = (uint8 *)surf->pixels;
    pitch = surf->pitch;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= surf->w) x2 = surf->w - 1;
    if (y2 >= surf->h) y2 = surf->h - 1;
    for (row = y1; row <= y2; row++) {
        uint8 *dst = base + (size_t)row * pitch + x1;
        for (col = x1; col <= x2; col++)
            *dst++ = color;
    }
}

/* ---- Slot 0x44: gfx_setDac ---- */
/* Palette data extracted from original MGRAPHIC.EXE overlay */
static const uint8 g_palettes[5][48] = {
    /* Palette 0: standard VGA 16-color */
    {0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a,
     0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
     0x15,0x15,0x15, 0x15,0x15,0x3f, 0x15,0x3f,0x15, 0x15,0x3f,0x3f,
     0x3f,0x15,0x15, 0x3f,0x15,0x3f, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f},
    /* Palette 1: magenta darkened */
    {0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a,
     0x2a,0x00,0x00, 0x00,0x00,0x00, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
     0x15,0x15,0x15, 0x15,0x15,0x3f, 0x15,0x3f,0x15, 0x15,0x3f,0x3f,
     0x3f,0x15,0x15, 0x3f,0x15,0x3f, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f},
    /* Palette 2: bright red/yellow zeroed */
    {0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x2a,0x00, 0x00,0x2a,0x2a,
     0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
     0x15,0x15,0x15, 0x15,0x15,0x3f, 0x15,0x3f,0x15, 0x15,0x3f,0x3f,
     0x3f,0x15,0x15, 0x00,0x00,0x00, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f},
    /* Palette 3: all black (fade out) */
    {0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
     0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
     0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00,
     0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00, 0x00,0x00,0x00},
    /* Palette 4: green zeroed */
    {0x00,0x00,0x00, 0x00,0x00,0x2a, 0x00,0x00,0x00, 0x00,0x2a,0x2a,
     0x2a,0x00,0x00, 0x2a,0x00,0x2a, 0x2a,0x15,0x00, 0x2a,0x2a,0x2a,
     0x15,0x15,0x15, 0x15,0x15,0x3f, 0x00,0x00,0x00, 0x15,0x3f,0x3f,
     0x3f,0x15,0x15, 0x3f,0x15,0x3f, 0x3f,0x3f,0x15, 0x3f,0x3f,0x3f}
};

/* Apply one of the baked DAC palettes by rewriting the low 16 entries of the
 * shared surface palette. The original issued INT 10h AX=1012h to load 16 DAC
 * registers (BX=0, CX=16); natively every page surface shares one SDL_Palette,
 * so updating it in place recolours all pages on the next present. Only entries
 * 0-15 are touched, matching the original's 16-register block — the upper 240
 * VGA colours from gfx_buildPalette stay put. This is what makes palette-1
 * screens (e.g. mission select's Wall.Pic) render colour 5 as black instead of
 * the standard palette's magenta. */
/* Upload `count` consecutive DAC registers starting at `startReg` from `count`
 * 6-bit VGA RGB triples. This is the native form of INT 10h AX=1012h (load DAC
 * register block): the shared surface palette is rewritten in place so every
 * page recolours on the next present. setupDac (egsys.c) drives the 0x10-0xFF
 * flight/cockpit colours through this; gfx_setDac uses it for the 0-15 block. */
void gfx_setDacRange(uint16 startReg, uint16 count, const uint8 *vgaTriples)
{
    SDL_Color colors[256];
    uint16 i;
    if (count == 0) return;
    if ((uint32)startReg + count > 256) count = (uint16)(256 - startReg);
    if (!gfxPalette) gfxPalette = gfx_buildPalette();
    if (!gfxPalette) return;
    for (i = 0; i < count; i++) {
        /* 6-bit VGA value -> 8-bit (shift left 2, replicate top 2 bits). */
        int r = vgaTriples[i*3] << 2, g = vgaTriples[i*3+1] << 2, b = vgaTriples[i*3+2] << 2;
        colors[i].r = (uint8)(r | (r >> 6));
        colors[i].g = (uint8)(g | (g >> 6));
        colors[i].b = (uint8)(b | (b >> 6));
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(gfxPalette, colors, (int)startReg, (int)count);
}

void FAR CDECL gfx_setDac(uint16 palIdx)
{
    if (palIdx > 4) return;
    gfx_setDacRange(0, 16, g_palettes[palIdx]);
    gfx_waitRetrace();
}

/* ---- Slot 0x21: gfx_setColor ---- */
void FAR CDECL gfx_setColor(int color)
{
    GfxState FAR *s = gfx_getState();
    s->fillColor = (uint8)color;
    return;
}

/* ---- Slot 0x0c: gfx_initOverlay ----
 * MGRAPHIC: `mov ax,[cs:pageSegTable+2]; mov [cs:curPageSeg],ax` — sets the
 * current draw page to page 1 (the back buffer). Called once at egame startup. */
void FAR CDECL gfx_initOverlay(void)
{
    GfxState FAR *s = gfx_getState();
    s->curPageSeg = s->pageSegs[1];
    s->curPage = 1;
    return;
}
/* Slot 0x0d: register-called via the _gfx_setPage1 shim (regshim.asm) — AX = a
 * page index; curPageSeg = pageSegs[AX]. MGRAPHIC's slot 0x0d takes the index in
 * AX (the name is a misnomer); clearRect uses it to select the page to clear. */
int FAR CDECL gfx_setPage1(uint16 page)
{
    GfxState FAR *s = gfx_getState();
    if (page < 16) {
        s->curPageSeg = s->pageSegs[page];
        s->curPage = (int)page;
    }
    return (int)s->curPageSeg;
}
/* Slot 0x10: called via the _gfx_getCurPageSeg shim, which preserves ES (a
 * gfx_getState() call here loads ES, and clearRect needs ES kept across this). */
int FAR CDECL gfx_getCurPageSeg(void)
{
    GfxState FAR *s = gfx_getState();
    return (int)s->curPageSeg;
}
void FAR CDECL gfx_getCurPage(int page)
{
    GfxState FAR *s = gfx_getState();
    return;
}
/* Slot 0x11 (≡0x49): thunk to the sprite core (MGRAPHIC @0x7ca -> @0x7db). The
 * core is UNCONDITIONALLY transparent — `lodsb; or al,al; jz skip; mov [es:di],al`
 * over width*height — with NO flags test (flags lives at SpriteParams+0x18, past
 * the 8-word block the core reads). egame chooses transparent (0x11) vs opaque
 * (copyRect 0x2a / blitToCurrent 0x30) at the C level, so slot 0x11 must always
 * skip zero bytes. An earlier `flags & 0x10` gate made the HUD gun-sight blit
 * opaque, copying its black background as a square behind the reticle (bug 7). */
int FAR CDECL gfx_blitSprite(struct SpriteParams *p)
{
    SDL_Surface *srcSurf, *dstSurf;
    uint8 *srcBase, *dstBase;
    int srcPitch, dstPitch, srcW, srcH, dstW, dstH;
    int row, col, w, h;

    if (!p) return 0;
    if (p->page < 0 || p->page >= 16) return 0;
    /* bufPtr is the sprite-sheet source. The radar/tac-map/HUD sheet (F15.SPR)
     * is loaded into a page surface, so bufPtr carries that page-segment token
     * (e.g. 0xC002 from gfx_allocPage); resolve it like gfx_blitCore does. Fall
     * back to the 1-based sprite-buffer handle for any decodePic sprite buffer. */
    srcSurf = gfx_surfaceForSeg((uint16)p->bufPtr);
    if (!srcSurf) srcSurf = gfx_getSpriteSurface((int)p->bufPtr);
    dstSurf = gfx_getPageSurface((int)p->page);
    if (!srcSurf || !dstSurf) return 0;
    srcBase = (uint8 *)srcSurf->pixels; srcPitch = srcSurf->pitch;
    dstBase = (uint8 *)dstSurf->pixels; dstPitch = dstSurf->pitch;
    srcW = srcSurf->w; srcH = srcSurf->h;
    dstW = dstSurf->w; dstH = dstSurf->h;
    w = p->width;
    h = p->height;

    for (row = 0; row < h; row++) {
        int sy = (int)p->srcY + row;
        int dy = (int)p->dstY + row;
        if (sy < 0 || sy >= srcH || dy < 0 || dy >= dstH) continue;
        for (col = 0; col < w; col++) {
            int sx = (int)p->srcX + col;
            int dx = (int)p->dstX + col;
            uint8 px;
            if (sx < 0 || sx >= srcW || dx < 0 || dx >= dstW) continue;
            px = srcBase[(size_t)sy * srcPitch + sx];
            if (px) dstBase[(size_t)dy * dstPitch + dx] = px;
        }
    }
    return 0;
}
/* Slot 0x1f: register-called via the _gfx_drawLine shim (regshim.asm).
 * MGRAPHIC's slot 0x1f takes its endpoints in registers: AX=x1, BX=y1,
 * CX=x2, DX=y2 (verified by disassembly), drawing to the current page with
 * the stored fill colour. The shim marshals those registers into these cdecl
 * stack args. (Calling this body directly with cdecl stack args — as the
 * noasm child trampolines do — also works.) */
/* Cohen-Sutherland region code against the 320x200 page. */
static int gfx_lineOutcode(int x, int y)
{
    int code = 0;
    if (x < 0) code |= 1; else if (x > 319) code |= 2;
    if (y < 0) code |= 4; else if (y > 199) code |= 8;
    return code;
}

void FAR CDECL gfx_drawLine(uint16 ux1, uint16 uy1, uint16 ux2, uint16 uy2)
{
    GfxState FAR *s = gfx_getState();
    SDL_Surface *surf;
    uint8 *base;
    int pitch;
    uint8 color = s->fillColor;
    int vx, vy;          /* blitOffset decomposed into a viewport origin */
    int x1, y1, x2, y2;  /* endpoints translated into absolute page coords */
    int code1, code2;
    int dx, dy, sx, sy, err, e2;

    /* MGRAPHIC slot 0x1f adds the blitOffset ([cs:0x1a0]) viewport base to the
     * start offset and is loop-counter-bounded; for off-screen endpoints it
     * just wraps writes inside the 64K page. Our earlier position-based loop
     * (while x0!=x2) infinite-looped once a delta overflowed a 16-bit int —
     * the 3D projection emits clamped near-plane coords like (13618,28486),
     * which hung the game on the first terrain frame. Rather than reproduce
     * MGRAPHIC's wrapping (~28000 useless writes per off-screen line, which is
     * far too slow in C and paints on-screen garbage), clip the segment to the
     * page with Cohen-Sutherland and draw only the visible part. The blitOffset
     * is folded in as a viewport origin so the radar/MFD lines land in their
     * sub-window instead of the main viewport. */
    vx = (int)((uint16)s->blitOffset % 320u);
    vy = (int)((uint16)s->blitOffset / 320u);
    x1 = (int)(int16)ux1 + vx; y1 = (int)(int16)uy1 + vy;
    x2 = (int)(int16)ux2 + vx; y2 = (int)(int16)uy2 + vy;

    /* Clip the segment to [0,319]x[0,199]. */
    code1 = gfx_lineOutcode(x1, y1);
    code2 = gfx_lineOutcode(x2, y2);
    for (;;) {
        if ((code1 | code2) == 0) break;          /* trivially inside */
        if ((code1 & code2) != 0) return;          /* trivially outside */
        {
            int outcode = code1 ? code1 : code2;
            int clipX = 0, clipY = 0;
            if (outcode & 8) { clipX = x1 + (long)(x2 - x1) * (199 - y1) / (y2 - y1); clipY = 199; }
            else if (outcode & 4) { clipX = x1 + (long)(x2 - x1) * (0 - y1) / (y2 - y1); clipY = 0; }
            else if (outcode & 2) { clipY = y1 + (long)(y2 - y1) * (319 - x1) / (x2 - x1); clipX = 319; }
            else { clipY = y1 + (long)(y2 - y1) * (0 - x1) / (x2 - x1); clipX = 0; }
            if (outcode == code1) { x1 = clipX; y1 = clipY; code1 = gfx_lineOutcode(x1, y1); }
            else          { x2 = clipX; y2 = clipY; code2 = gfx_lineOutcode(x2, y2); }
        }
    }

    /* Bresenham over the now-on-screen segment (deltas <= 320, no overflow).
     * Writes into the current page's backing surface. */
    surf = gfx_getCurPageSurface();
    if (!surf) return;
    base  = (uint8 *)surf->pixels;
    pitch = surf->pitch;
    dx = x2 - x1; if (dx < 0) dx = -dx;
    dy = y2 - y1; if (dy < 0) dy = -dy;
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        base[(size_t)y1 * pitch + x1] = color;
        if (x1 == x2 && y1 == y2) break;
        e2 = err + err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}
/* drawLineWrapper - draw a line from the lineX1..lineY2 globals.
 * The original clipped the endpoints (Cohen-Sutherland) before passing them to
 * the line slot in registers; gfx_drawLine now does that clipping itself, so
 * this just marshals the globals into its by-value args. */
extern int16 lineX1, lineY1, lineX2, lineY2;
void drawLineWrapper(void)
{
    gfx_drawLine((uint16)lineX1, (uint16)lineY1, (uint16)lineX2, (uint16)lineY2);
}
/* Slot 0x20: register-called via the _gfx_setDrawColor shim — AH = fill colour.
 * Stores the clearRect/fill colour (MGRAPHIC slot 0x20 = `mov [fillColor],ah`). */
void FAR CDECL gfx_setDrawColor(uint16 color)
{
    gfx_getState()->fillColor = (uint8)color;
}
void FAR CDECL gfx_nop23(void) { return; }
/* Slot 0x25/0x28: fill the per-row dirty spans. minBuf points at the per-row
 * dirtyMinBuf; the matching dirtyMaxBuf sits 0x1b8 bytes after it. For each row
 * y in [yMin..yMax] fill the span [minBuf[y]..maxBuf[y]] of curPageSeg with
 * fillColor. This is the actual rectangle clear behind clearRect (MGRAPHIC slot
 * 0x25==0x28). A row whose min==max==0 or ==0x13F is treated as empty and
 * skipped, matching the original's range guard. (The DOS build passed BX = a
 * near offset into the caller's DS; the merged build passes a real pointer.) */
void FAR CDECL gfx_dirtyRect2(const int16 *spanMinBuf, uint16 yMin, uint16 yMax)
{
    GfxState FAR *s = gfx_getState();
    const uint16 *minBuf = (const uint16 *)spanMinBuf;
    const uint16 *maxBuf = (const uint16 *)((const char *)spanMinBuf + 0x1b8);
    uint8 fill = s->fillColor;
    SDL_Surface *surf = gfx_surfaceForSeg(s->curPageSeg);
    uint8 *pagePx;
    int16 firstRow = (int16)yMin;   /* AX */
    int16 lastRow  = (int16)yMax;   /* CX */
    int y;
    if (!surf) return;
    pagePx = (uint8 *)surf->pixels;
    /* MGRAPHIC slot 0x25: `or ax,ax; js exit` — if firstRow < 0, draw nothing. */
    if (firstRow < 0) return;
    if (lastRow > 199) lastRow = 199;                          /* rowOffsets[] safety */
    for (y = (int)lastRow; y >= (int)firstRow; y--) {
        uint16 spanLo = minBuf[y];
        uint16 spanHi = maxBuf[y];
        uint16 width, col;
        uint16 off;
        int row, col0;
        uint8 *dst;
        /* MGRAPHIC's degenerate-row test is UNSIGNED (`cmp hi,lo; jc skip; ja
         * draw`): skip when hi < lo, draw when hi > lo, and when equal skip only
         * if the column is 0 or 0x13f (else a single pixel). The edge-walker in
         * egame clips real spans to [0,0x13f] before storing, so a "negative"
         * (large-unsigned) lo is a non-span sentinel that this unsigned compare
         * skips. An earlier SIGNED reading clamped such a lo to 0 and filled
         * [0..hi], painting a spurious full-width scanline across the left-MFD
         * ocean (and the equivalent on 3D fills). */
        if (spanHi < spanLo) continue;                                 /* unsigned */
        if (spanHi == spanLo && (spanHi == 0 || spanHi == 319)) continue;
        /* Clamp the write extent to the visible row. The 3D projection emits
         * near-plane-clamped columns (e.g. ~0x7000), so an unclamped width would
         * loop tens of thousands of times per row in C (MGRAPHIC wraps cheaply in
         * the 64K page; we clip instead). This does NOT change the draw decision
         * above — only how many bytes land on screen. */
        if (spanLo > 319) continue;                                /* span off right edge */
        if (spanHi > 319) spanHi = 319;
        width = (uint16)(spanHi - spanLo + 1);
        /* The original wrote at the linear page offset rowOffsets[y]+blitOffset+
         * spanLo; split it into (row,col) so the surface pitch (not assumed 320)
         * applies and the run stays inside one visible row. */
        off = (uint16)(s->rowOffsets[y] + (uint16)s->blitOffset + spanLo);
        row = off / LOGICAL_WIDTH;
        col0 = off % LOGICAL_WIDTH;
        if (row < 0 || row >= surf->h) continue;
        dst = pagePx + (size_t)row * surf->pitch + col0;
        for (col = 0; col < width && col0 + (int)col < LOGICAL_WIDTH; col++)
            dst[col] = fill;
    }
}
/* Slot 0x2d: getDisplayPage — returns the back-buffer page index (no args).
 * MGRAPHIC's slot 0x2d is `mov al,[cs:0x1a2]; retf` — it returns the stored
 * display-page byte (the page the frame is composited into), NOT a segment.
 * The renderer (render3DView / renderHudFrame / tac map) targets this page,
 * and gfx_dacAnimate (slot 0x2c) presents it to the visible page 0.
 *
 * NOASM-only divergence: MGRAPHIC's span engine selects the fill page from the
 * view descriptor each frame, so the back buffer is always the 3D target. Our
 * span fills (gfx_dirtyRect/gfx_drawLine) instead follow curPageSeg, which the
 * alternate-view cockpit blit (openBlitClosePic(..., *g_pageFront)) leaves on
 * the visible page 0 — so without re-syncing here the F2/F3/F4 world composites
 * onto the visible page and gfx_dacAnimate then stomps it with the stale back
 * buffer (the flicker). render3DView calls this right before the 3D setup, so
 * re-select the display page as the active draw page to keep them in sync. */
int FAR CDECL gfx_getDisplayPage(void)
{
    GfxState FAR *s = gfx_getState();
    if (s->displayPage < 16 && s->pageSegs[0] == 0xA000)
        s->curPageSeg = s->pageSegs[s->displayPage];
    return (int)s->displayPage;
}

int FAR CDECL gfx_setFont(uint16 ch, uint16 fontIdx)
{
    /* Returns the pixel advance width of a single character. stringWidth()
     * sums this over a string to centre text, so it MUST agree with the
     * x-advance gfx_drawString uses (wt[ch-0x20]); otherwise centred text
     * lands off-screen and drawString's clip test discards every glyph.
     * The width tables are file-scope arrays reached directly as native
     * pointers, exactly as drawStringCore reads them. */
    const uint8 *wt;
    if (fontIdx >= 8) return 8;
    /* Chars >= 0x80 are inline color escapes - no glyph, no width */
    if (ch >= 0x80) return 0;
    wt = g_fontWidthTables[fontIdx];
    if (!wt || ch < 0x20) return 8;
    return wt[ch - 0x20];
}
int FAR CDECL gfx_getAuxBufSize(void) { return 0x1950; }
int FAR CDECL gfx_getFreeMem(void)
{
    /* Slot 0x32: DOS free-memory probe (INT 21h/AH=48h, BX=0xFFFF -> largest
     * free block in paragraphs). Stubbed to 0 in NO_ASM; takes no args despite
     * the old "fontSetup" mislabel. */
    return 0;
}
/* gfx_fillRow (0x33) and gfx_copyRow (0x35) are register-called: their slot
 * symbols are asm shims in regshim.asm -> gfx_fillRow_impl / gfx_copyRow_impl
 * above. Slot 0x34 (fillRow2) is IDENTICAL to slot 0x33 in MGRAPHIC (both
 * `rep movsw` the decoded row to ES:DI), so slot 0x34 points at the same
 * gfx_fillRow shim — decodePic (pic_decodepic.inc) uses it to fill the
 * caller's sprite buffer. This unused stub is kept only for legacy linkage. */
void FAR CDECL gfx_fillRow2(uint16 x, uint16 y)
{
    (void)x;
    (void)y;
    return;
}
void FAR CDECL gfx_nop36(void) { return; }
void FAR CDECL gfx_nop37(void) { return; }
void FAR CDECL gfx_setFadeSteps(int steps) { (void)steps; return; }
/* Slot 0x3e: linear byte offset of pixel (col,row) = col + rowTable[row].
 * MGRAPHIC's arg order is col FIRST ([ss:bx+4]), row SECOND ([ss:bx+6]) — the
 * opposite of the natural (y,x). egame computes every MFD viewport origin via
 * gfx_setBlitOffset(gfx_calcRowAddr(xOrigin, yOrigin)); an earlier (y,x) reading
 * transposed the result (col*320+row instead of row*320+col), which left the
 * main viewport correct (origin 0,0) but transposed every sub-window — the
 * middle MFD's −32x/+32y offset and the left/right MFD misplacement. */
int FAR CDECL gfx_calcRowAddr(int col, int row)
{
    GfxState FAR *s = gfx_getState();
    if (!s->rowOffsetsReady) return (int)(row * 320 + col);
    return (int)(s->rowOffsets[row] + col);
}
/* Slots 0x40/0x41: MGRAPHIC stored the arg to absolute 0000:0x00CC / 0x00CE — a
 * 4-byte scratch (the unused INT 0x33 vector) the asm overlay's clip/draw paths
 * read back as the active clip rectangle. The native line drawer (gfx_drawLine)
 * clips to the page itself and folds blitOffset in as the viewport origin, so
 * nothing reads this scratch back; keep a real variable for the store so the
 * old physical address (near-null on a flat address space → SEGV) is gone. */
static uint16 gfxOvlClipScratch[2];
void FAR CDECL gfx_setOvlVal1(int val)
{
    gfxOvlClipScratch[0] = (uint16)val;
    return;
}
void FAR CDECL gfx_setOvlVal2(int val)
{
    gfxOvlClipScratch[1] = (uint16)val;
    return;
}
int FAR CDECL gfx_getPresetOffset1(void)
{
    return 0x5580; /* baked constant — NOT live blitOffset (that is slot 0x1e) */
}
int FAR CDECL gfx_getModeFlag(void)
{
    GfxState FAR *s = gfx_getState();
    return (int)s->modeFlag;
}
int FAR CDECL gfx_getModeFlag2(void)
{
    GfxState FAR *s = gfx_getState();
    return (int)s->modeFlag;
}
/* Slots 0x4e/0x4d: getters (no args). MGRAPHIC's bodies returned overlay state;
 * the merged build has no live source for them yet, so they report 0 — the value
 * start.exe's sprite-load path keys off (gfx_getVal()==0). */
int FAR CDECL gfx_getVal(void) { return 0; }
int FAR CDECL gfx_getVal2(void) { return 0; }
void FAR CDECL gfx_setDacAnimCount(uint16 count)
{
    GfxState FAR *s = gfx_getState();
    s->dacCounter = (uint8)count;
    return;
}
void FAR CDECL gfx_commitPage(void)
{
    gfx_presentPage(0);
}
void FAR CDECL gfx_nop51(void) { return; }
void FAR CDECL gfx_blitSpriteClipped(int16 *ptr) { gfx_blitSprite((struct SpriteParams *)ptr); }
void FAR CDECL gfx_blitSpriteClipped2(void) { return; }
void FAR CDECL gfx_blitSpriteOpaque(int16 *ptr) { gfx_blitSprite((struct SpriteParams *)ptr); }
void FAR CDECL gfx_blitSpriteOpaque2(void) { return; }

/* Map a legacy page *segment* value back to its page index. gfx_blitToCurrent is
 * called with a raw segment (e.g. page1Ptr from gfx_allocPage) rather than a page
 * index; the same value was recorded in pageSegs[] via gfx_storeBufPtr, so an
 * exact match identifies the source page. Returns the first match (lowest index),
 * or -1 if the segment names no page. */
static int gfx_pageForSeg(uint16 seg)
{
    GfxState FAR *s = gfx_getState();
    int i;
    for (i = 0; i < 16; i++)
        if (s->pageSegs[i] == seg) return i;
    return -1;
}

/* ---- Slot 0x30: gfx_blitToCurrent ---- */
/* Copy a whole source page onto the current draw page. The original block-copied
 * 64000 bytes between DOS page segments; natively both are SDL surfaces, so copy
 * the 320x200 image row-by-row from the source page into the current page. */
void FAR CDECL gfx_blitToCurrent(int16 pagePtr)
{
    GfxState FAR *s = gfx_getState();
    int srcPage = gfx_pageForSeg((uint16)pagePtr);
    SDL_Surface *src, *dst;
    int row;

    if (srcPage < 0) return;
    src = ensurePage(srcPage);
    dst = ensurePage(s->curPage);
    if (!src || !dst || src == dst) return;

    for (row = 0; row < LOGICAL_HEIGHT; row++)
        SDL_memcpy((uint8 *)dst->pixels + (size_t)row * dst->pitch,
                   (uint8 *)src->pixels + (size_t)row * src->pitch,
                   LOGICAL_WIDTH);
    return;
}

/* ---- Slot 0x12/0x4a: gfx_blitCore — transparent sprite core ----
 * Register-called via the _gfx_blitCore shim (regshim.asm): BP = near pointer
 * (caller's DS) to an 8-word param block. MGRAPHIC's slot 0x12 (code @0x7db) is
 * `lodsb; or al,al; jz skip; mov [es:di],al` over width*height, dest segment
 * from pageSegTable[dstPage], no clipping, no blitOffset. The block layout is the
 * first 8 words of struct SpriteParams:
 *   [0] src segment   [1] src col   [2] src row   [3] dst page index
 *   [4] dst col       [5] dst row   [6] width     [7] height
 * Used by egame for the HUD gun-sight / symbol blits (egseg2.asm) — the
 * transparency (skip-zero) is what gives the gun sight its see-through
 * background instead of an opaque black box. */
void FAR CDECL gfx_blitCore(int16 *blk)
{
    GfxState FAR *s = gfx_getState();
    uint16 srcSeg = (uint16)blk[0];
    uint16 srcCol = (uint16)blk[1];
    int    srcRow = blk[2];
    uint16 dstCol = (uint16)blk[4];
    int    dstRow = blk[5];
    int    w = blk[6];
    int    h = blk[7];
    int    row, col;
    SDL_Surface *srcSurf, *dstSurf;
    if (blk[3] < 0 || blk[3] >= 16) return;
    srcSurf = gfx_surfaceForSeg(srcSeg);
    dstSurf = gfx_surfaceForSeg(s->pageSegs[blk[3]]);
    if (!srcSurf || !dstSurf) return;
    for (row = 0; row < h; row++) {
        int sr = srcRow + row, dr = dstRow + row;
        uint8 *src, *dst;
        if (sr < 0 || sr >= srcSurf->h || dr < 0 || dr >= dstSurf->h) continue;
        src = (uint8 *)srcSurf->pixels + (size_t)sr * srcSurf->pitch + srcCol;
        dst = (uint8 *)dstSurf->pixels + (size_t)dr * dstSurf->pitch + dstCol;
        for (col = 0; col < w; col++) {
            uint8 px;
            if (srcCol + col >= (uint16)srcSurf->w || dstCol + col >= (uint16)dstSurf->w) break;
            px = src[col];
            if (px) dst[col] = px;
        }
    }
}

/* ---- Stubs for declared-but-unimplemented slots ---- */
/* gfx_blitVariant (0x03), gfx_copyBlock (0x04), gfx_drawStringUnclipped (0x06):
 * register-called glyph slots — provided by regshim.asm shims (see above). */
void FAR CDECL gfx_clipRight(void)           { return; }
void FAR CDECL gfx_clipTop(void)             { return; }
void FAR CDECL gfx_clipLeft(void)            { return; }
void FAR CDECL gfx_clipBottom(void)          { return; }

/* ---- Slot 0x0b: gfx_complexRender — HUD pitch-ladder renderer ----
 * MGRAPHIC code @0x615. Register-called (via the _gfx_complexRender shim in
 * regshim.asm) with BX=row, DX=orientation(dl), CX=mode-gate(cl), SI=ladder
 * variant (0 or 2). It writes colour 0x0f into curPageSeg along a column whose
 * X base + row Y-bounds come from a 12-word geometry table at MGRAPHIC data-seg
 * 0x1c84 (baked below — the disasm's `mov ds,0` is a relocated reference to the
 * overlay's data base, so the table is statically extractable, NOT seg-0 BSS):
 *   base[i]=0x1c84[i]   loY[i]=0x1c84[i+4]   hiY[i]=0x1c84[i+8]
 * It walks rows from BX downward by 2, drawing a 1/2/3-pixel-thick mark per row
 * driven by a cycling 1..10 thickness counter (init 0xA), the thickness running
 * left (SI==0) or right (SI!=0). egame's drawInstrumentGauges calls it twice
 * (SI=0 and SI=2).
 *
 * Performance note: egame computes BX via `sub word [g_tapeRenderX],AX` (egseg2.asm
 * :240) which can underflow to a large unsigned value, so BX may start far above
 * hiY. The original asm then burns up to ~32k cheap skip iterations decrementing
 * BX by 2 until it reaches the loY..hiY window — negligible in hand-asm, but slow
 * enough in C to look like a hang (it froze the flight on the first HUD frame).
 * Those leading iterations never draw, so we fast-forward over them, advancing
 * the iteration index `t` so the thickness counter keeps the exact same phase as
 * the naive loop would have at the first in-window row. */
static const int g_ladderGeom[12] = {
    71, 248, 120, 200, 26, 26, 68, 68, 86, 86, 98, 98
};

void FAR CDECL gfx_complexRender(int bxArg, int dxArg, int cxArg, int siArg)
{
    GfxState FAR *s = gfx_getState();
    SDL_Surface *surf;
    uint8 *page;
    uint8 color = 0x0f;
    int dir;            /* +1 (SI!=0, cld) or -1 (SI==0, std) */
    int wi;             /* word index into the geometry table */
    uint16 base, loY, hiY;
    uint16 bx;
    long t;             /* 1-based iteration index of the naive MGRAPHIC loop */
    int cl = cxArg & 0xff;
    int dl = dxArg & 0xff;

    dir = (siArg == 0) ? -1 : 1;     /* set from the ORIGINAL si (before +=4) */
    bx = (uint16)(bxArg - 1);
    if ((int8)dl >= 1) bx += 20;     /* `cmp dl,1; jl` is a signed byte test */
    if (cl != 0) { siArg += 4; bx++; }

    wi = siArg / 2;                  /* si is a byte offset; table is word-indexed */
    if (wi < 0 || wi + 8 > 11) return;
    /* g_ladderGeom is a real const array in this single-binary build; read it
     * directly (the old MK_FP(f15DataSeg, ...) far-pointer reconstruction is the
     * dead 16-bit-DOS path). */
    base = (uint16)g_ladderGeom[wi];
    loY  = (uint16)g_ladderGeom[wi + 4];
    hiY  = (uint16)g_ladderGeom[wi + 8];

    surf = gfx_surfaceForSeg(s->curPageSeg);
    if (!surf) return;
    page = (uint8 *)surf->pixels;
    initRowOffsets();

    /* Skip the leading non-drawing iterations (bx > hiY). bx steps by 2, so the
     * first in-window value is bx - 2*ceil((bx-hiY)/2); advance t to match. The
     * (uint16) cast yields the true unsigned gap even when bx underflowed. */
    t = 1;
    if (bx > hiY) {
        long skip = ((long)(uint16)(bx - hiY) + 1L) / 2L;
        bx = (uint16)(bx - (uint16)(skip * 2L));
        t += skip;
    }

    for (; ; t++) {
        int phase, thickness;
        if (bx < loY) break;                 /* unsigned, matches `jc` */
        phase = (int)((t - 1L) % 10L);            /* thickness counter: 1..10 phase */
        thickness = (phase == 0) ? 10 : phase;
        if (bx <= hiY) {                     /* `ja` skips the store when bx>hiY */
            uint16 dstOff = (uint16)(s->rowOffsets[bx] + base);
            if (thickness == 5) {
                page[dstOff] = color; dstOff = (uint16)(dstOff + dir);
                page[dstOff] = color;
            } else if (thickness == 10) {
                page[dstOff] = color; dstOff = (uint16)(dstOff + dir);
                page[dstOff] = color; dstOff = (uint16)(dstOff + dir);
                if (cl == 0) page[dstOff] = color;
            } else {
                page[dstOff] = color;
            }
        }
        bx -= 2;
    }
}
void FAR CDECL gfx_spriteVariant1(void)      { return; }
void FAR CDECL gfx_spriteVariant2(void)      { return; }
void FAR CDECL gfx_nop15(void)              { return; }
void FAR CDECL gfx_nop16(void)              { return; }
void FAR CDECL gfx_setBlitOffset2(void)      { GfxState FAR *s=gfx_getState(); s->blitOffset=0; return; }
void FAR CDECL gfx_setBlitOffset3(void)      { GfxState FAR *s=gfx_getState(); s->blitOffset=0; return; }
void FAR CDECL gfx_setBlitOffsetReg(void)          { return; } /* reg-called stub: blitOffset=AX, no shim yet */
int FAR CDECL gfx_getPresetOffset2(void)         { return 0x1950; } /* baked constant 0x1950 */
int FAR CDECL gfx_getBlitOffset(void)         { GfxState FAR *s=gfx_getState(); return (int)s->blitOffset; }
void FAR CDECL gfx_plotPixel(void)              { return; } /* stub: real 0x24 plots one pixel at cached [0x1b6e]/[0x1b70] */
void FAR CDECL gfx_storePageSeg(void)        { return; }
void FAR CDECL gfx_setPageSeg(void)          { return; }
/* Slot 0x2b: zero-fill the PHYSICAL VGA buffer (0xA000), 64000 bytes — MGRAPHIC
 * sets ES=0xA000 and `rep stosw` 0x7d00 words. Independent of the page table, so
 * it always clears the visible framebuffer. (egame only calls this from a dead
 * path, but start/end use it; implement faithfully.) */
void FAR CDECL gfx_clearVga(void)
{
    SDL_Surface *front = ensurePage(0);
    int y;
    if (!front) return;
    for (y = 0; y < front->h; y++)
        SDL_memset((uint8 *)front->pixels + (size_t)y * front->pitch, 0, front->w);
}
/* Slot 0x2c: present the composited back buffer to the visible page.
 * MGRAPHIC's slot 0x2c copies the full 64000-byte page from pageSegs[1] (the
 * back buffer, where gameMainLoop's renderFrame/renderHudFrame composite the
 * frame) to pageSegs[0] (the visible page), then sets displayPage=1. It is
 * called once per frame from gameMainLoop (egame_rc.asm). Args (AX/BX) ignored.
 * Without this the dynamic frame never reaches the screen — only the static
 * cockpit copied to page 0 at startup, plus direct-to-page-0 draws, show. */
void FAR CDECL gfx_dacAnimate(void)
{
    GfxState FAR *s = gfx_getState();
    /* Copy from the page the frame was just composited into (curPage), not a
     * hardcoded page 1: the side/rear views render into page 0, so sourcing page 1
     * here copied a stale frame over the live one (the alternate-view flicker). */
    SDL_Surface *back = ensurePage(s->curPage);
    SDL_Surface *front = ensurePage(0);
    if (back && front && back != front) {
        int y, w = (back->w < front->w) ? back->w : front->w;
        int h = (back->h < front->h) ? back->h : front->h;
        for (y = 0; y < h; y++) {
            SDL_memcpy((uint8 *)front->pixels + (size_t)y * front->pitch,
                       (const uint8 *)back->pixels + (size_t)y * back->pitch,
                       (size_t)w);
        }
    }
    s->displayPage = 1;
    /* Advance the fire colour-cycle on a fixed FIRE_CYCLE_HZ wall-clock schedule. */
    {
        Uint64 now = SDL_GetTicksNS();
        int guard = 0;
        if (s->cycleClockNs == 0)
            s->cycleClockNs = now - FIRE_CYCLE_NS;
        while (now - s->cycleClockNs >= FIRE_CYCLE_NS && guard++ < 4) {
            s->cycleClockNs += FIRE_CYCLE_NS;
            gfx_dacCycle();
        }
        if (now - s->cycleClockNs >= FIRE_CYCLE_NS)
            s->cycleClockNs = now;       /* fell far behind: snap forward */
    }
    gfx_presentPage(0);
    return;
}
/* ---- Slot 0x2e: gfx_dacCycle — DAC fire/target colour-cycle ----
 * MGRAPHIC code @0x9be. Per frame it advances a phase counter (LCG x*5+1) at
 * data-seg 0x1ccc, picks one of 4 palette indices from the table at 0x1cc8
 * ({0x0c,0x04,0x0c,0x0e}), looks up that RGB triple in the 16-entry palette at
 * 0x1b85, and writes it to 9 DAC registers starting at 0x8d stepping +0x10
 * (0x8d,0x9d,..,0xfd,0x0d) via ports 0x3C8/0x3C9 — pulsing the fire/target
 * colour ramp red->dark-red->red->yellow. If the screen-shake countdown (the
 * cs:0x9b2 byte, our dacCounter, seeded by slot 0x4f) is nonzero it also jitters
 * CRTC start-address-low (reg 0x0D) and decrements it, resetting it to 0 on the
 * final frame. The 0x1ccc/0x1cc8/0x1b85 tables are baked into MGRAPHIC's data
 * segment (the disasm's `mov ds,0` immediate is relocated to the overlay's data
 * base, so they are NOT segment-0 BSS and ARE statically extractable). */
static const uint8 g_dacFirePalette[16][3] = {
    {0x00,0x00,0x00},{0x00,0x00,0x2a},{0x00,0x2a,0x00},{0x00,0x2a,0x2a},
    {0x2a,0x00,0x00},{0x2a,0x00,0x2a},{0x2a,0x15,0x00},{0x2a,0x2a,0x2a},
    {0x15,0x15,0x15},{0x15,0x15,0x3f},{0x15,0x3f,0x15},{0x15,0x3f,0x3f},
    {0x3f,0x15,0x15},{0x3f,0x15,0x3f},{0x3f,0x3f,0x15},{0x3f,0x3f,0x3f}
};
static const uint8 g_dacFireIndex[4] = {0x0c, 0x04, 0x0c, 0x0e};

void FAR CDECL gfx_dacCycle(void)
{
    GfxState FAR *s = gfx_getState();
    uint16 phase;
    uint8 idx;
    SDL_Color col;
    uint8 reg;
    int r, g, b;

    if (!gfxPalette) gfxPalette = gfx_buildPalette();
    if (!gfxPalette) return;

    /* Advance the phase counter (ax = ax*5 + 1) and pick the fire colour. */
    phase = (uint16)(s->dacPhase * 5u + 1u);
    s->dacPhase = phase;
    idx = (uint8)(g_dacFireIndex[(uint8)phase & 3] & 0x0f);
    /* 6-bit VGA value -> 8-bit (shift left 2, replicate top 2 bits). */
    r = g_dacFirePalette[idx][0] << 2;
    g = g_dacFirePalette[idx][1] << 2;
    b = g_dacFirePalette[idx][2] << 2;
    col.r = (uint8)(r | (r >> 6));
    col.g = (uint8)(g | (g >> 6));
    col.b = (uint8)(b | (b >> 6));
    col.a = 255;

    /* Pulse the triple into the 9 DAC entries 0x8d,0x9d,..,0xfd,0x0d (reg wraps)
     * — natively just rewrite those shared-palette colours in place. */
    reg = 0x8d;
    do {
        SDL_SetPaletteColors(gfxPalette, &col, (int)reg, 1);
        reg = (uint8)(reg + 0x10);
    } while (reg != 0x1d);

    /* Screen-shake: while the countdown is nonzero, shift the presented frame by
     * the phase high byte (0-3 px), decrementing the countdown and clearing the
     * shift on the frame it reaches zero. The original jittered the CRTC
     * display-start byte; gfx_presentPage applies shakeOffset instead. */
    if (s->dacCounter != 0) {
        s->shakeOffset = (int)((phase >> 8) & 3);
        if (--s->dacCounter == 0) s->shakeOffset = 0;
    } else {
        s->shakeOffset = 0;
    }
    return;
}
void FAR CDECL gfx_setPageBuf(void)          { return; }
int FAR CDECL gfx_getConst1(void)           { return 1; } /* baked constant 1 (cs:0x1d8 in MGRAPHIC) */

/* ---- Initialise the shared GfxState ----
 * Called once at startup from game_init(). The slot-dispatch table, OvlHeader
 * and MISC/SOUND stub overlays that used to live here are gone: in the merged
 * single-process build the gfx functions are called directly, so only the
 * state defaults remain. */
void gfx_initState(void)
{
    GfxState FAR *s = gfx_getState();

    s->modeFlag = 1;
    s->displayPage = 1;  /* MGRAPHIC cs:0x1a2 default; back buffer = page 1 */
    s->dacPhase = 0x4d2; /* MGRAPHIC data-seg 0x1ccc seed (dacCycle phase) */
    /* pageSegs[] and rowOffsetsReady are zero by default (file-scope global). */

    /* Record f15's DGROUP segment so the gfx const tables (palettes, font
     * tables) remain reachable via far pointer regardless of the caller's DS
     * (Finding A). */
    {
        void FAR *anchorFp = (void FAR *)&dgroupAnchor;
        s->f15DataSeg = FP_SEG(anchorFp);
    }
}
