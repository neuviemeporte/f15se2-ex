/*
 * gfx_impl.c Pure-C replacement for MGRAPHIC.EXE overlay (Mode 13h, 320x200x256)
 */

#include <SDL3/SDL.h>

#include "gfx_impl.h"
#include "gfx.h"
#include "r2d.h"
#include "r3d_gl.h"
#include "struct.h"
#include "log.h"
#include "version.h"
#include <dos.h>
#include <stdio.h>

#include "fontdata.h"

/* The SDL window and renderer are owned here: the graphics layer brings up the
 * video output and every gfx_* function presents through it. The original game
 * rendered to a 320x200 MCGA framebuffer; we present that through an SDL renderer
 * scaled to a resizable window. The 320x200 / 640x350 logical resolutions live
 * in gfx.h. */
#define INITIAL_WINDOW_WIDTH 640
#define INITIAL_WINDOW_HEIGHT 400

/* Fixed fire colour-cycle rate, independent of render frame rate. The original
 * stepped the cycle once per rendered frame; we pin it at 15 Hz so the pulse
 * looks the same regardless of how fast we present. */
#define FIRE_CYCLE_HZ 15
#define FIRE_CYCLE_NS (SDL_NS_PER_SECOND / FIRE_CYCLE_HZ)
static SDL_Window *sdlWindow = NULL;
static SDL_Renderer *sdlRenderer = NULL;
static bool s_useGL = false; /* OpenGL backend owns the context + present */

/* Forward declarations for the page-surface model, used by gfx_videoShutdown
 * before their definitions further down. */
static GfxState FAR *gfx_getState(void);
static SDL_Palette *gfxPalette; /* shared 256-entry VGA DAC palette */
static int gfxPaletteGen;       /* bumped on every palette-entry change (cache invalidation) */
static void gfx_presentSurfaceSW(SDL_Surface *surf, int shake);
static void gfx_swLine(int x1, int y1, int x2, int y2, int color);
static void gfx_swPoint(int x, int y, int color);
static void gfx_swImage(struct R2DImage *img, int srcX, int srcY, int w, int h,
                        int dstX, int dstY, int key);

/* Bring up the SDL window and renderer. The 320x200 logical surface is stretched
 * to fill the resizable window (SDL_LOGICAL_PRESENTATION_STRETCH). */
void gfx_videoInit(void) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        LogCritical(("SDL_Init failed: %s", SDL_GetError()));

    /* The OpenGL 3D backend (r3d_gl.c) presents through a GL context rather than
     * an SDL_Renderer (the two can't share a window). When it's selected, request
     * a GL-capable window and bring the context up here; the present path then
     * routes through the GL composite instead of the renderer. */
    /* The software 2D backend present lives here (it owns the SDL_Renderer);
     * register it with the r2d seam so r2d_present can dispatch to it when GL is
     * not active. */
    r2d_registerSoftwarePresent(gfx_presentSurfaceSW);
    r2d_registerSoftwarePrims(gfx_swLine, gfx_swPoint);
    r2d_registerSoftwareImage(gfx_swImage);

    s_useGL = r3dgl_wantGL();

    /* GL path: request the framebuffer attributes (incl. MSAA) before window
     * creation, then bring the context up. MSAA can force a pixel format the driver
     * can't satisfy, so if the context won't come up we retry once without it before
     * giving up GL entirely — losing only the anti-aliasing, not the whole backend. */
    if (s_useGL) {
        int msaa = r3dgl_msaaSamples();
        for (;;) {
            r3dgl_setGLAttributes(msaa);
            sdlWindow = SDL_CreateWindow("F-15 SE2 EX " F15_VERSION,
                                         INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT,
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
            if (sdlWindow && r3dgl_initContext(sdlWindow)) break; /* GL up */
            if (sdlWindow) { SDL_DestroyWindow(sdlWindow); sdlWindow = NULL; }
            if (msaa > 0) {
                LogWarn(("GL init failed with %dx MSAA; retrying without it", msaa));
                msaa = 0;
                continue;
            }
            LogCritical(("GL init failed; falling back to software renderer"));
            s_useGL = false;
            break;
        }
    }

    if (!sdlWindow) {
        sdlWindow = SDL_CreateWindow("F-15 SE2 EX " F15_VERSION, INITIAL_WINDOW_WIDTH,
                                     INITIAL_WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE);
        if (!sdlWindow)
            LogCritical(("Window creation failed: %s", SDL_GetError()));
    }

    /* Enable SDL_EVENT_TEXT_INPUT so the keyboard slots (ovlimpl.c) receive
     * shifted/localised ASCII for pilot-name entry. */
    SDL_StartTextInput(sdlWindow);

    if (s_useGL) return;

    sdlRenderer = SDL_CreateRenderer(sdlWindow, NULL);
    if (!sdlRenderer)
        LogCritical(("Renderer creation failed: %s", SDL_GetError()));

    SDL_SetRenderVSync(sdlRenderer, 1);
    /* No SDL logical presentation: the software present (gfx_presentSurfaceSW)
     * letterboxes the page itself through the shared r2d mapping, so the virtual
     * size and the virtual->window placement live in exactly one place. */
}

void gfx_setTextInputEnabled(bool enabled) {
    if (enabled) SDL_StartTextInput(sdlWindow);
    else SDL_StopTextInput(sdlWindow);
}

/* Toggle borderless-desktop fullscreen (Alt+Enter). */
void gfx_toggleFullscreen(void) {
    bool full = (SDL_GetWindowFlags(sdlWindow) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreen(sdlWindow, !full);
    if (!full) SDL_HideCursor();
    else SDL_ShowCursor();
}

void gfx_videoShutdown(void) {
    GfxState FAR *s = gfx_getState();
    int i;
    for (i = 0; i < 16; i++) {
        if (s->pageSurfaces[i]) {
            SDL_DestroySurface(s->pageSurfaces[i]);
            s->pageSurfaces[i] = NULL;
        }
    }
    if (gfxPalette) {
        SDL_DestroyPalette(gfxPalette);
        gfxPalette = NULL;
    }
    if (sdlRenderer) SDL_DestroyRenderer(sdlRenderer);
    if (sdlWindow) SDL_DestroyWindow(sdlWindow);
    SDL_Quit();
}

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
static int s_palWritten;

static void palAdd(int r, int g, int b) {
    int i = s_palWritten * 3;
    s_vgaPal[i] = (uint8)r;
    s_vgaPal[i + 1] = (uint8)g;
    s_vgaPal[i + 2] = (uint8)b;
    s_palWritten++;
}
static void palAddGray(int v) { palAdd(v, v, v); }

static void palAdd16(int lo, int melo, int mehi, int hi) {
    int r, g, b, i, h, l;
    for (i = 0; i < 16; i++) {
        if (i & 8) {
            h = hi;
            l = melo;
        } else {
            h = mehi;
            l = lo;
        }
        r = g = b = l;
        if (i & 4) r = h;
        if (i & 2) g = h;
        if (i & 1) b = h;
        if (i == 6) g = melo; /* brown, not dark yellow */
        palAdd(r, g, b);
    }
}

static int palAddRun(int start, int ch, int lo, int melo, int me, int mehi, int hi) {
    int r = lo, g = lo, b = lo, i, up, v = 0;
    if (start & 4) r = hi;
    if (start & 2) g = hi;
    if (start & 1) b = hi;
    palAdd(r, g, b);
    up = (start & ch) ? 0 : 1;
    for (i = 0; i < 3; i++) {
        if (up)
            v = (i == 0) ? melo : (i == 1) ? me
                                           : mehi;
        else
            v = (i == 0) ? mehi : (i == 1) ? me
                                           : melo;
        if (ch == 4)
            r = v;
        else if (ch == 2)
            g = v;
        else
            b = v;
        palAdd(r, g, b);
    }
    return start ^ ch;
}

static void palAddCycle(int lo, int melo, int me, int mehi, int hi) {
    int hue = 1;
    hue = palAddRun(hue, 4, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 1, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 2, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 4, lo, melo, me, mehi, hi);
    hue = palAddRun(hue, 1, lo, melo, me, mehi, hi);
    (void)palAddRun(hue, 2, lo, melo, me, mehi, hi);
}

static SDL_Palette *gfx_buildPalette(void) {
    static const int gray[16] = {0, 5, 8, 11, 14, 17, 20, 24, 28, 32, 36, 40, 45, 50, 56, 63};
    SDL_Color colors[256];
    SDL_Palette *pal;
    int i;

    s_palWritten = 0;
    palAdd16(0, 21, 42, 63);
    for (i = 0; i < 16; i++) palAddGray(gray[i]);
    palAddCycle(0, 16, 31, 47, 63);
    palAddCycle(31, 39, 47, 55, 63);
    palAddCycle(45, 49, 54, 58, 63);
    palAddCycle(0, 7, 14, 21, 28);
    palAddCycle(14, 17, 21, 24, 28);
    palAddCycle(20, 22, 24, 26, 28);
    palAddCycle(0, 4, 8, 12, 16);
    palAddCycle(8, 10, 12, 14, 16);
    palAddCycle(11, 12, 13, 15, 16);
    for (i = 0; i < 8; i++) palAddGray(0);

    /* 64-level -> 8-bit: shift left 2 and replicate the top 2 bits. */
    for (i = 0; i < 256; i++) {
        int r = s_vgaPal[i * 3] << 2, g = s_vgaPal[i * 3 + 1] << 2, b = s_vgaPal[i * 3 + 2] << 2;
        colors[i].r = (uint8)(r | (r >> 6));
        colors[i].g = (uint8)(g | (g >> 6));
        colors[i].b = (uint8)(b | (b >> 6));
        colors[i].a = 255;
    }
    pal = SDL_CreatePalette(256);
    if (pal) SDL_SetPaletteColors(pal, colors, 0, 256);
    gfxPaletteGen++;
    return pal;
}

SDL_Palette *gfx_getPalette(void) {
    if (!gfxPalette) gfxPalette = gfx_buildPalette();
    return gfxPalette;
}

/* Monotonic counter bumped whenever a palette entry changes (gfx_setDacRange /
 * gfx_dacCycle / rebuild). The GL backend keys its per-image RGBA texture cache on
 * it so a static sprite sheet re-uploads only when the palette actually moved. */
int gfx_paletteGeneration(void) { return gfxPaletteGen; }

int gfx_getShakeOffset(void) { return gfx_getState()->shakeOffset; }

void gfx_paletteRGB(int idx, uint8 *r, uint8 *g, uint8 *b) {
    SDL_Palette *pal = gfx_getPalette();
    if (!pal || idx < 0 || idx >= pal->ncolors) {
        *r = *g = *b = 0;
        return;
    }
    *r = pal->colors[idx].r;
    *g = pal->colors[idx].g;
    *b = pal->colors[idx].b;
}

/* Lazily create the 320x200 8-bit surface backing a page index. */
static SDL_Surface *ensurePage(int page) {
    GfxState FAR *s = gfx_getState();
    if (page < 0 || page >= 16) return NULL;
    /* Pages 0 (front/visible) and 1 (back/composite) alias to one hidden back
     * buffer. The DOS double buffer is redundant natively — the page surface is
     * only snapshotted into the window at present time (SDL texture upload / GL
     * composite), so a single draw surface can't tear, and every compose-then-
     * present sequence finishes drawing before it presents. All page-1 references
     * resolve to page 0's surface; the per-frame back->front copy (gfx_dacAnimate)
     * and the front/back dual writes become self-copies. */
    if (page == 1) page = 0;
    if (!s->pageSurfaces[page]) {
        SDL_Surface *surf = SDL_CreateSurface(LOGICAL_WIDTH, LOGICAL_HEIGHT,
                                              SDL_PIXELFORMAT_INDEX8);
        if (!surf) LogCritical(("SDL_CreateSurface failed: %s", SDL_GetError()));
        if (!gfxPalette) gfxPalette = gfx_buildPalette();
        if (gfxPalette) SDL_SetSurfacePalette(surf, gfxPalette);
        s->pageSurfaces[page] = surf;
    }
    return s->pageSurfaces[page];
}

struct SDL_Surface *gfx_getPageSurface(int page) { return ensurePage(page); }
/* The single hidden back buffer. Every draw target in the game is page index 0
 * or 1, which alias to buffer 0, so the "current page" is always it. */
struct SDL_Surface *gfx_getCurPageSurface(void) { return ensurePage(0); }

/* Public: writable pixel base + stride of a page's surface (the single back
 * buffer), for the egame HUD primitives (eghudr fillSpanRect) that fill it
 * directly. The image is always LOGICAL_WIDTH x LOGICAL_HEIGHT. */
uint8 *gfx_pagePixels(int page, int *pitchOut) {
    SDL_Surface *surf = ensurePage(page);
    if (!surf) return NULL;
    if (pitchOut) *pitchOut = surf->pitch;
    return (uint8 *)surf->pixels;
}

/* ---- Sprite buffers --------------------------------------------------------
 * The DOS build decoded sprite sheets into 64KB "segments" (allocBuffer) and
 * gfx_blitSprite read palette indices straight out of them. Natively each sprite
 * buffer is an R2DImage (a 320x200 INDEX8 surface) addressed by a small integer
 * handle (which the caller keeps where the old build kept the segment value).
 * decodePic fills the surface (gfx_getSpriteSurface); gfx_blitSprite reads it
 * via the shared r2d_blit. */
#define MAX_SPRITE_BUFS 8
static R2DImage *s_spriteBufs[MAX_SPRITE_BUFS];

int gfx_allocSpriteBuf(void) {
    int i;
    for (i = 0; i < MAX_SPRITE_BUFS; i++) {
        if (!s_spriteBufs[i]) {
            s_spriteBufs[i] = r2d_registerImage(LOGICAL_WIDTH, LOGICAL_HEIGHT);
            if (!s_spriteBufs[i]) LogCritical(("r2d_registerImage failed"));
            return i + 1; /* 1-based handle; 0 means "none" */
        }
    }
    return 0;
}

struct SDL_Surface *gfx_getSpriteSurface(int handle) {
    if (handle < 1 || handle > MAX_SPRITE_BUFS) return NULL;
    return r2d_imageSurface(s_spriteBufs[handle - 1]);
}

/* The R2DImage behind a sprite-buffer handle, for the image-submission path
 * (r2d_submitImage) — the renderer owns realization (page blit or GL quad). */
static R2DImage *gfx_spriteImage(int handle) {
    if (handle < 1 || handle > MAX_SPRITE_BUFS) return NULL;
    return s_spriteBufs[handle - 1];
}

R2DImage *gfx_spriteBufImage(int handle) {
    return gfx_spriteImage(handle);
}

/* Software realization of an image submission: clipped blit into the back buffer.
 * Registered with r2d so a submitted sprite rasterizes straight into the page. */
static void gfx_swImage(R2DImage *img, int srcX, int srcY, int w, int h,
                        int dstX, int dstY, int key) {
    r2d_blit(r2d_imageSurface(img), srcX, srcY, ensurePage(0), dstX, dstY, w, h, key);
}

void gfx_freeSpriteBuf(int handle) {
    if (handle < 1 || handle > MAX_SPRITE_BUFS) return;
    r2d_releaseImage(s_spriteBufs[handle - 1]);
    s_spriteBufs[handle - 1] = NULL;
}

/* While the 640x350 title is up, the renderer presents the separate hi-res
 * surface (see gfx_presentHiRes). video_setHiRes sets this; gfx_setMode13
 * clears it when the title is dismissed. */
static bool gfxHiResActive = false;

/* Software present: blit a page surface through the SDL_Renderer (vsync-paced).
 * Registered with r2d (r2d_registerSoftwarePresent) as the software 2D backend's
 * present; r2d_present calls it when GL is not active.
 *
 * The virtual->window letterbox comes from the shared r2d mapping (the single
 * source of truth, derived from the surface's own dimensions so the 320x200
 * overlay and the 640x350 hi-res title both map correctly) rather than SDL's
 * logical presentation; we render into the centred dst rect over a black-cleared
 * window. */
static void gfx_presentSurfaceSW(SDL_Surface *surf, int shake) {
    SDL_Texture *tex;
    R2DMapping m;
    int win_w, win_h;
    SDL_FRect dst;
    if (!surf || !sdlRenderer) return;
    tex = SDL_CreateTextureFromSurface(sdlRenderer, surf);
    if (!tex) return;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    SDL_GetRenderOutputSize(sdlRenderer, &win_w, &win_h);
    /* Square pixels: the software path presents the 320x200 page uniformly scaled
     * (fast, "fat" look). Non-square aspect correction here is an opt-in later step
     * (a present-time SDL stretch), not the default. */
    r2d_computeMapping(surf->w, surf->h, win_w, win_h, 1, &m);

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_RenderClear(sdlRenderer);
    /* Explosion screen-shake: the original jittered the CRTC display-start byte
     * (gfx_dacCycle); natively we shift the presented frame left by the same 0-3
     * virtual pixels (scaled to window space). */
    dst.x = (float)m.offX - (float)shake * m.scaleX;
    dst.y = (float)m.offY;
    dst.w = (float)surf->w * m.scaleX;
    dst.h = (float)surf->h * m.scaleY;
    SDL_RenderTexture(sdlRenderer, tex, NULL, &dst);
    SDL_RenderPresent(sdlRenderer);
    SDL_DestroyTexture(tex);
}

/* Push a page's surface to the active 2D backend (GL composite or software
 * renderer) via the r2d seam (vsync-paced present). */
static void gfx_presentPage(int page) {
    /* During the hi-res title, the page-0 framebuffer still holds the prior
     * 320x200 image (e.g. labs.pic). Redirect generic flips/commits to the
     * hi-res title surface so frame-pacing presents don't clobber it. */
    if (gfxHiResActive) {
        gfx_presentHiRes();
        return;
    }
    r2d_present(ensurePage(page), gfx_getState()->shakeOffset);
}

/* Hi-res (640x350) title surface. The EGA-title path (picimpl.c picBlit)
 * decodes the planar Title640.pic into this surface; gfx_presentHiRes pushes
 * it. video_setHiRes already switched the renderer's logical presentation to
 * 640x350; gfx_setMode13 restores 320x200 once the title is dismissed. */
static SDL_Surface *gfxHiResSurface;

SDL_Surface *gfx_getHiResSurface(void) {
    if (!gfxHiResSurface) {
        gfxHiResSurface = SDL_CreateSurface(HIRES_WIDTH, HIRES_HEIGHT,
                                            SDL_PIXELFORMAT_INDEX8);
        if (!gfxHiResSurface) LogCritical(("SDL_CreateSurface failed: %s", SDL_GetError()));
        if (!gfxPalette) gfxPalette = gfx_buildPalette();
        if (gfxPalette) SDL_SetSurfacePalette(gfxHiResSurface, gfxPalette);
    }
    return gfxHiResSurface;
}

void gfx_presentHiRes(void) {
    r2d_present(gfx_getHiResSurface(), 0);
}

/* Initialize row offset table */
static void initRowOffsets(void) {
    int i;
    if (gfx_getState()->rowOffsetsReady) return;
    for (i = 0; i < 200; i++)
        gfx_getState()->rowOffsets[i] = (uint16)(i * 320);
    gfx_getState()->rowOffsetsReady = 1;
}

/* ---- Slot 0x3c: gfx_setMode13 ----
 * Switch to the 320x200 game resolution (the native equivalent of the DOS INT
 * 10h mode-13h set): the renderer presents the 320x200 logical surface through
 * SDL. This is also the lo-res restore after the (possibly hi-res) title. */
void FAR CDECL gfx_setMode13(void) {
    initRowOffsets();
    gfxHiResActive = false;
    gfx_getState()->modeFlag = 1;
}

/* Title-screen hi-res: switch to the 640x350 title surface. Both backends scale
 * whatever surface they're handed to the window via the shared r2d mapping (which
 * derives the virtual size from the surface), so this just flags hi-res; the
 * present picks up the 640x350 hi-res surface from gfx_presentHiRes. */
bool video_setHiRes(void) {
    gfxHiResActive = true;
    return true;
}

/* ---- Slot 0x45: gfx_waitRetrace ---- */
void FAR CDECL gfx_waitRetrace(void) {
    /* Frame pacing now comes from the vsync'd present in gfx_flipPage/gfx_presentPage. */
}

/* ---- Slot 0x46: gfx_flipPage ---- */
void FAR CDECL gfx_flipPage(void) {
    gfx_presentPage(0);
}

/* A screen that draws HD/native-overlay content each frame (not into the page) can
 * register a hook that fully reproduces its current frame; gfx_repaint calls it
 * instead of the bare page re-present, so an expose/focus event redraws the overlay
 * (e.g. the briefing's HD wall + pointer arm) rather than dropping to the page's
 * legacy sprites. NULL (the default) restores the plain page re-present. */
static void (*g_repaintHook)(void);
void gfx_setRepaintHook(void (*hook)(void)) { g_repaintHook = hook; }

/* Re-present the current visible frame (page 0, or the hi-res title surface).
 * gfx_presentPage already redirects to the hi-res path when the title is up. */
void gfx_repaint(void) {
    /* On a live GL flight frame the 3D is in the framebuffer, not the page, so
     * re-presenting only the page here would blank it. The compositor still holds
     * the last fully-composited frame, and the next flight frame redraws within a
     * frame, so skip the bare re-present. Pure-2D screens (menus/briefing/debrief,
     * which block in key-waits and produce no frame of their own) still need it. */
    if (r3dgl_active() && r3dgl_flightLive()) return;
    if (g_repaintHook) {
        g_repaintHook();
        return;
    }
    gfx_presentPage(0);
}


/* Slot 0x3a: DI = y -> AX = row byte offset (y*320). */
int FAR CDECL gfx_getRowOffset(int y) {
    GfxState FAR *s = gfx_getState();
    initRowOffsets();
    if (y >= 0 && y < 200)
        return (int)s->rowOffsets[y];
    return (int)((uint16)y * 320);
}

/* ---- Slot 0x3f: gfx_getModecode ---- */
int FAR CDECL gfx_getModecode(void) {
    return 3; /* MCGA mode code */
}

/* ---- Slot 0x22: gfx_nop22 ---- */
void FAR CDECL gfx_nop22(void) {
    return; /* bare RETF in MGRAPHIC — does NOT reset blitOffset */
}

/* ---- Slot 0x1a: gfx_setBlitOffset ---- */
void FAR CDECL gfx_setBlitOffset(int offset) {
    GfxState FAR *s = gfx_getState();
    s->blitOffset = (uint16)offset;
    return;
}

/* ---- Slot 0x25: gfx_dirtyRect ---- */
/* eg3drast.c hands us the real span buffer pointer; gfx_dirtyRect2 walks rows
 * [yMin..yMax]. */
void FAR CDECL gfx_dirtyRect(int16 *spanBuf, int yMin, int yMax) {
    gfx_dirtyRect2(spanBuf, (uint16)yMin, (uint16)yMax);
}

/* Slot 0x01 (gfx_fillDirty), 0x02 (gfx_blitTransparent), 0x03 (gfx_blitVariant),
 * 0x04 (gfx_copyBlock) and 0x06 (gfx_drawStringUnclipped) are register-called
 * glyph slots — their slot symbols are asm shims in regshim.asm that marshal
 * BP (param block) + BX (string) into gfx_drawStringClipped_impl (defined below
 * next to gfx_drawString). No C stub here; the shim provides the symbol. */

/* ---- Font data ---- */

/* Font width tables extracted from MGRAPHIC.EXE */
static const uint8 g_font1_widths[96] = {
    5, 2, 4, 7, 6, 8, 8, 2, 3, 3, 6, 6, 3, 4, 2, 8, 8, 3, 6, 6, 7, 6, 6, 6, 6, 6, 2, 3, 5, 5, 5, 6,
    8, 8, 6, 7, 8, 6, 6, 8, 8, 2, 6, 6, 6, 8, 8, 8, 6, 8, 6, 6, 6, 6, 6, 8, 8, 8, 8, 4, 8, 4, 6, 8,
    2, 6, 6, 5, 6, 6, 4, 6, 6, 2, 3, 6, 2, 8, 6, 6, 6, 6, 5, 6, 4, 6, 6, 8, 6, 6, 6, 4, 2, 4, 5, 8};
static const uint8 g_font3_widths[96] = {
    3, 2, 4, 5, 4, 5, 5, 2, 3, 3, 6, 4, 3, 4, 2, 4, 5, 3, 5, 5, 5, 5, 5, 5, 5, 5, 2, 3, 4, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 4, 5, 6, 6, 6, 6, 6, 3, 6, 3, 4, 5,
    2, 4, 4, 4, 4, 4, 3, 4, 4, 2, 3, 4, 2, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 2, 3, 3, 5};
static const uint8 g_font4_widths[96] = {
    4, 2, 4, 6, 4, 7, 6, 2, 3, 4, 4, 4, 3, 5, 2, 7, 5, 3, 5, 5, 6, 5, 5, 5, 5, 5, 2, 3, 4, 5, 4, 5,
    7, 6, 5, 6, 6, 5, 5, 7, 7, 2, 5, 5, 5, 6, 6, 7, 5, 7, 5, 5, 4, 5, 6, 8, 7, 8, 7, 3, 7, 3, 6, 6,
    2, 5, 5, 4, 5, 5, 3, 5, 5, 2, 3, 5, 2, 8, 5, 5, 5, 5, 5, 5, 4, 5, 6, 8, 5, 5, 5, 4, 2, 4, 5, 7};
static const uint8 g_font5_widths[96] = {
    3, 2, 4, 5, 4, 5, 5, 2, 3, 3, 6, 4, 3, 4, 2, 4, 5, 3, 5, 5, 5, 5, 5, 5, 5, 5, 2, 3, 4, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 2, 5, 5, 5, 6, 5, 5, 5, 5, 5, 5, 4, 5, 6, 6, 6, 6, 6, 3, 6, 3, 4, 5,
    2, 4, 4, 4, 4, 4, 3, 4, 4, 2, 3, 4, 2, 6, 4, 4, 4, 4, 4, 4, 4, 4, 4, 6, 4, 4, 4, 4, 2, 3, 3, 5};

/* Font index 0 is the small in-flight HUD font (3x5, fixed advance 4). The
 * original registers it at runtime via low-memory pointer tables MGRAPHIC reads
 * (0:0xE2 width / 0:0xEE glyph / 0:0xFA rowsize); it is not statically present
 * in MGRAPHIC.EXE (which only bakes fonts 1,3,4,5). g_font0_* was captured from
 * the live glyph engine — see fontdata.h. All advances are 4 (fixed pitch). */
static const uint8 g_font0_widths[96] = {
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4};
static const uint8 *const g_fontWidthTables[8] = {
    g_font0_widths, g_font1_widths, NULL, g_font3_widths,
    g_font4_widths, g_font5_widths, NULL, NULL};
static const uint8 g_fontHeightsArr[8] = {5, 8, 7, 6, 7, 6, 4, 0};
static const uint8 g_fontMaxWidths[8] = {4, 8, 6, 6, 8, 6, 0, 0};

/* Bitmap pointers per font index — NULL means no bitmap available */
static uint8 *g_fontBitmapPtrs[8] = {
    (uint8 *)g_font0_bitmaps, (uint8 *)g_font1_bitmaps, NULL, (uint8 *)g_font3_bitmaps,
    (uint8 *)g_font4_bitmaps, (uint8 *)g_font5_bitmaps, NULL, NULL};
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
                           int clipL, int clipR, int clipT, int clipB) {
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
    int submit;

    if (!string || !params) return;

    /* The font tables are file-scope arrays in this module; in the merged
     * single-process build they are reached directly as native pointers (the
     * old DOS build had to re-base them on f15's DGROUP via far pointer). */
    x = (int)params[4];
    y = (int)params[5];
    color = (int)params[2];
    fontIdx = (uint16)params[6] & 7;
    height = g_fontHeightsArr[fontIdx];
    rowSize = g_fontBitmapRowSize[fontIdx];
    bitmaps = g_fontBitmapPtrs[fontIdx];
    widthTab = g_fontWidthTables[fontIdx];

    /* On a GL flight frame, submit each lit glyph pixel as a native point (drawn
     * over the composited frame at window resolution) instead of baking it into the
     * shared 320x200 page. Software (retained) keeps writing the page directly. */
    submit = r2d_vectorActive();

    /* The page is backed by an SDL surface (same buffer the pic decoder and
     * clearRect target); glyph pixels are written straight into it. */
    surf = gfx_getPageSurface((int)params[0]);
    if (!surf) return;
    base = (uint8 *)surf->pixels;
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

        if (x > clipR) break; /* rest of the string is right of window */

        if (bitmaps && ch >= 0x20) {
            uint8 *glyph = bitmaps + (ch - 0x20) * (uint16)rowSize;
            for (row = 0; row < height; row++) {
                int py = y + row;
                uint8 bits;
                uint8 *dstRow;
                if (py < clipT || py > clipB) continue; /* row outside window */
                if (py < 0 || py >= surfH) continue;    /* off the surface */
                bits = glyph[row];
                dstRow = base + (size_t)py * pitch;
                for (col = 0; col < 8; col++) {
                    int px = x + col;
                    if ((bits & 0x80) && px >= clipL && px <= clipR &&
                        px >= 0 && px < surfW) {
                        if (submit) r2d_submitPoint(px, py, color);
                        else dstRow[px] = (uint8)color;
                    }
                    bits <<= 1;
                }
            }
        }
        x += widthTab ? (ch >= 0x20 ? widthTab[ch - 0x20] : 8) : 8;
    }

    /* Update x position and color in the param block (start/end's menu text
     * relies on the advanced x to chain successive draws). */
    params[4] = (int16)x;
    params[2] = (int16)color;
}

/* Rotated, sub-grid HUD label (GL native-res overlay only). Draws `string` in font
 * `fontIdx`/`color` with its glyph grid placed by two basis vectors: (exX,exY) is the
 * on-screen 320-space step per text column, (eyX,eyY) per row, both emanating from the
 * float anchor (ax,ay) = the string's top-left in absolute 320-space. Each lit font
 * texel becomes one rotated parallelogram cell submitted via r2d_submitQuadF, so the
 * label rotates with (and glides sub-grid along) the pitch-ladder lines instead of
 * snapping upright to the 320x200 grid. The software backend keeps the upright integer
 * glyph engine (drawStringCore); this is a no-op there. Scissored to the half-open
 * clip rect, matching the glyph slot's clip window. */
void FAR gfx_drawGlyphStrRot(const char *string, int fontIdx, int color,
                             float ax, float ay, float exX, float exY,
                             float eyX, float eyY,
                             int cx0, int cy0, int cx1, int cy1) {
    uint8 height, rowSize;
    uint8 *bitmaps;
    const uint8 *widthTab;
    int ci, row, col;
    float penX = 0.0f; /* running text-column offset (in glyph texels) */
    if (!string) return;
    fontIdx &= 7;
    height = g_fontHeightsArr[fontIdx];
    rowSize = g_fontBitmapRowSize[fontIdx];
    bitmaps = g_fontBitmapPtrs[fontIdx];
    widthTab = g_fontWidthTables[fontIdx];
    if (!bitmaps) return;
    for (ci = 0; string[ci] != 0 && ci < 256; ci++) {
        uint8 ch = (uint8)string[ci];
        if (ch & 0x80) { color = ch & 0x7F; continue; } /* inline colour escape */
        if (ch >= 0x20) {
            uint8 *glyph = bitmaps + (ch - 0x20) * (uint16)rowSize;
            for (row = 0; row < height; row++) {
                uint8 bits = glyph[row];
                for (col = 0; col < 8; col++) {
                    if (bits & 0x80) {
                        float gx = penX + col, gy = (float)row;
                        float q[8];
                        q[0] = ax + gx * exX + gy * eyX;
                        q[1] = ay + gx * exY + gy * eyY;
                        q[2] = ax + (gx + 1) * exX + gy * eyX;
                        q[3] = ay + (gx + 1) * exY + gy * eyY;
                        q[4] = ax + (gx + 1) * exX + (gy + 1) * eyX;
                        q[5] = ay + (gx + 1) * exY + (gy + 1) * eyY;
                        q[6] = ax + gx * exX + (gy + 1) * eyX;
                        q[7] = ay + gx * exY + (gy + 1) * eyY;
                        r2d_submitQuadF(q, color, cx0, cy0, cx1, cy1);
                    }
                    bits <<= 1;
                }
            }
        }
        penX += (widthTab && ch >= 0x20) ? widthTab[ch - 0x20] : 8;
    }
}

/* ---- Slot 0x05: gfx_drawString (cdecl, unclipped) ---- */
void FAR CDECL gfx_drawString(int16 *pageNum, const char *string) {
    drawStringCore(pageNum, string, 0, 319, 0, 199);
    return;
}

/* Register-called glyph slots (0x01/0x02/0x03/0x06) — entered via regshim.asm
 * with BP = param block (near, caller DS) and BX = string. `mode` selects which
 * clip stages the variant ran: bit0 = horizontal window (params word 9/10),
 * bit1 = vertical window (params word 7/8). The two X (resp. Y) bounds are
 * stored without a fixed min/max order across blocks, so normalise them. */
void gfx_drawStringClipped_impl(int16 *params, const char *string, int mode) {
    int clipL = 0, clipR = 319, clipT = 0, clipB = 199;
    if (!params) return;
    if (mode & 1) { /* horizontal clip window */
        int bound1 = (int)params[9], bound2 = (int)params[10];
        clipL = bound1 < bound2 ? bound1 : bound2;
        clipR = bound1 < bound2 ? bound2 : bound1;
    }
    if (mode & 2) { /* vertical clip window */
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
 * arguments. */
void FAR CDECL gfx_fillDirty(int16 *params, const char *string) { gfx_drawStringClipped_impl(params, string, 2); }
void FAR CDECL gfx_blitTransparent(int16 *params, const char *string) { gfx_drawStringClipped_impl(params, string, 1); }
void FAR CDECL gfx_blitVariant(int16 *params, const char *string) { gfx_drawStringClipped_impl(params, string, 1); }
void FAR CDECL gfx_copyBlock(int16 *params, const char *string) { gfx_drawStringClipped_impl(params, string, 0); }
void FAR CDECL gfx_drawStringUnclipped(int16 *params, const char *string) { gfx_drawStringClipped_impl(params, string, 3); }

/* Slots 0x01-0x06: the clipped glyph engine. The egame HUD selects the clip mode
 * by slot index; map each to the real glyph function. */
void FAR CDECL gfx_drawGlyphStr(int16 *desc, const char *str, int slot) {
    switch (slot) {
    case 0x01:
        gfx_fillDirty(desc, str);
        break;
    case 0x02:
        gfx_blitTransparent(desc, str);
        break;
    case 0x03:
        gfx_blitVariant(desc, str);
        break;
    case 0x04:
        gfx_copyBlock(desc, str);
        break;
    case 0x06:
    default:
        gfx_drawStringUnclipped(desc, str);
        break;
    }
}

/* ---- Slot 0x2a: gfx_copyRect ---- */
/* Opaque copy of a width x height rect between two page surfaces (clipped to
 * each). The original streamed rows between DOS page segments via movedata;
 * natively both pages are SDL surfaces, copied by the shared r2d_blit. */
void FAR CDECL gfx_copyRect(int srcPage, uint16 srcX, uint16 srcY,
                            int dstPage, uint16 dstX, uint16 dstY,
                            int width, int height) {
    r2d_blit(ensurePage(srcPage), (int)srcX, (int)srcY,
             ensurePage(dstPage), (int)dstX, (int)dstY,
             width, height, -1);
}

/* ---- Off-buffer save/restore images ----
 * Bridge page indices to the r2d image API: gfx_captureToImage copies a page
 * region into an owned image (the save-under), gfx_restoreFromImage copies it
 * back. An image is a standalone 320x200 surface, not a page. Coordinates match
 * gfx_copyRect's so call sites map 1:1. */
struct R2DImage *gfx_allocImage(int w, int h) { return r2d_registerImage(w, h); }
void gfx_freeImage(struct R2DImage *img) { r2d_releaseImage(img); }

void gfx_captureToImage(struct R2DImage *img, int srcPage, int srcX, int srcY,
                        int dstX, int dstY, int w, int h) {
    r2d_blit(ensurePage(srcPage), srcX, srcY,
             r2d_imageSurface(img), dstX, dstY, w, h, -1);
}

void gfx_restoreFromImage(struct R2DImage *img, int dstPage, int srcX, int srcY,
                          int dstX, int dstY, int w, int h) {
    r2d_drawImage(img, srcX, srcY, w, h, ensurePage(dstPage), dstX, dstY, -1);
}

int gfx_readImagePixel(struct R2DImage *img, int x, int y) {
    SDL_Surface *surf = r2d_imageSurface(img);
    if (!surf || x < 0 || y < 0 || x >= surf->w || y >= surf->h) {
        return -1;
    }
    return ((const Uint8 *)surf->pixels)[y * surf->pitch + x];
}

void gfx_drawSpriteOpaque(int handle, int srcX, int srcY, int dstPage,
                          int dstX, int dstY, int w, int h) {
    (void)dstPage; /* single back buffer */
    r2d_submitImage(gfx_spriteImage(handle), srcX, srcY, w, h, dstX, dstY, -1);
}

/* ---- Slot 0x29: gfx_switchColor ---- */
void FAR CDECL gfx_switchColor(int16 *pageDesc, int x1, int y1,
                               int x2, int y2, int oldColor, int newColor) {
    SDL_Surface *surf = gfx_getPageSurface((int)*pageDesc);
    uint8 *base;
    int pitch, row, col;

    if (!surf) return;
    base = (uint8 *)surf->pixels;
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
void clearRect(int16 *pageNum, int16 x1, int16 y1, int16 x2, int16 y2) {
    SDL_Surface *surf = gfx_getPageSurface((int)*pageNum);
    uint8 color = (uint8)pageNum[3];
    uint8 *base;
    int pitch, row, col;

    if (!surf) return;
    base = (uint8 *)surf->pixels;
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
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x2a, 0x00, 0x00, 0x2a, 0x2a,
     0x2a, 0x00, 0x00, 0x2a, 0x00, 0x2a, 0x2a, 0x15, 0x00, 0x2a, 0x2a, 0x2a,
     0x15, 0x15, 0x15, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x15, 0x15, 0x3f, 0x3f,
     0x3f, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f},
    /* Palette 1: magenta darkened */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x2a, 0x00, 0x00, 0x2a, 0x2a,
     0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x15, 0x00, 0x2a, 0x2a, 0x2a,
     0x15, 0x15, 0x15, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x15, 0x15, 0x3f, 0x3f,
     0x3f, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f},
    /* Palette 2: bright red/yellow zeroed */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x2a, 0x00, 0x00, 0x2a, 0x2a,
     0x2a, 0x00, 0x00, 0x2a, 0x00, 0x2a, 0x2a, 0x15, 0x00, 0x2a, 0x2a, 0x2a,
     0x15, 0x15, 0x15, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x15, 0x15, 0x3f, 0x3f,
     0x3f, 0x15, 0x15, 0x00, 0x00, 0x00, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f},
    /* Palette 3: all black (fade out) */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    /* Palette 4: green zeroed */
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x2a,
     0x2a, 0x00, 0x00, 0x2a, 0x00, 0x2a, 0x2a, 0x15, 0x00, 0x2a, 0x2a, 0x2a,
     0x15, 0x15, 0x15, 0x15, 0x15, 0x3f, 0x00, 0x00, 0x00, 0x15, 0x3f, 0x3f,
     0x3f, 0x15, 0x15, 0x3f, 0x15, 0x3f, 0x3f, 0x3f, 0x15, 0x3f, 0x3f, 0x3f}};

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
void gfx_setDacRange(uint16 startReg, uint16 count, const uint8 *vgaTriples) {
    SDL_Color colors[256];
    uint16 i;
    if (count == 0) return;
    if ((uint32)startReg + count > 256) count = (uint16)(256 - startReg);
    if (!gfxPalette) gfxPalette = gfx_buildPalette();
    if (!gfxPalette) return;
    for (i = 0; i < count; i++) {
        /* 6-bit VGA value -> 8-bit (shift left 2, replicate top 2 bits). */
        int r = vgaTriples[i * 3] << 2, g = vgaTriples[i * 3 + 1] << 2, b = vgaTriples[i * 3 + 2] << 2;
        colors[i].r = (uint8)(r | (r >> 6));
        colors[i].g = (uint8)(g | (g >> 6));
        colors[i].b = (uint8)(b | (b >> 6));
        colors[i].a = 255;
    }
    SDL_SetPaletteColors(gfxPalette, colors, (int)startReg, (int)count);
    gfxPaletteGen++;
}

void FAR CDECL gfx_setDac(uint16 palIdx) {
    if (palIdx > 4) return;
    gfx_setDacRange(0, 16, g_palettes[palIdx]);
    gfx_waitRetrace();
}

/* ---- Slot 0x21: gfx_setColor ---- */
void FAR CDECL gfx_setColor(int color) {
    GfxState FAR *s = gfx_getState();
    s->fillColor = (uint8)color;
    return;
}

/* Slot 0x11 (≡0x49): thunk to the sprite core (MGRAPHIC @0x7ca -> @0x7db). The
 * core is UNCONDITIONALLY transparent — `lodsb; or al,al; jz skip; mov [es:di],al`
 * over width*height — with NO flags test (flags lives at SpriteParams+0x18, past
 * the 8-word block the core reads). egame chooses transparent (0x11) vs opaque
 * (copyRect 0x2a / blitToCurrent 0x30) at the C level, so slot 0x11 must always
 * skip zero bytes — a flags test here would copy the gun-sight's black background
 * as a square behind the reticle. */
int FAR CDECL gfx_blitSprite(struct SpriteParams *p) {
    if (!p) return 0;
    if (p->page < 0 || p->page >= 16) return 0;
    /* bufPtr is a 1-based sprite-buffer handle (F15.SPR via gfxBufPtr, the
     * theater/menu sheets). Submit the sprite to the renderer; the realization
     * is the backend's (software page blit / GL quad). Unconditionally
     * transparent (skip index 0) — the gun-sight/symbol sprites rely on the
     * see-through background. */
    r2d_submitImage(gfx_spriteImage((int)p->bufPtr),
                    (int)p->srcX, (int)p->srcY, (int)p->width, (int)p->height,
                    (int)p->dstX, (int)p->dstY, 0);
    return 0;
}
/* Slot 0x1f: register-called via the _gfx_drawLine shim (regshim.asm).
 * MGRAPHIC's slot 0x1f takes its endpoints in registers: AX=x1, BX=y1,
 * CX=x2, DX=y2 (verified by disassembly), drawing to the current page with
 * the stored fill colour. The shim marshals those registers into these cdecl
 * stack args. (Calling this body directly with cdecl stack args — as the
 * noasm child trampolines do — also works.) */
/* Cohen-Sutherland region code against the 320x200 page. */
static int gfx_lineOutcode(int x, int y) {
    int code = 0;
    if (x < 0)
        code |= 1;
    else if (x > 319)
        code |= 2;
    if (y < 0)
        code |= 4;
    else if (y > 199)
        code |= 8;
    return code;
}

/* Software 2D-primitive rasterizers — the software backend's realization of a
 * submitted line/point (registered with r2d via r2d_registerSoftwarePrims).
 * Endpoints/coords arrive already blitOffset-absolute and clipped to the page;
 * these just write the current page surface. The GL backend instead records the
 * submission and replays it at native resolution. */
static void gfx_swLine(int x1, int y1, int x2, int y2, int colorArg) {
    SDL_Surface *surf = gfx_getCurPageSurface();
    uint8 *base;
    int pitch, dx, dy, sx, sy, err, e2;
    uint8 color = (uint8)colorArg;
    if (!surf) return;
    base = (uint8 *)surf->pixels;
    pitch = surf->pitch;
    /* Bresenham over the on-screen segment (deltas <= 320, no overflow). */
    dx = x2 - x1;
    if (dx < 0) dx = -dx;
    dy = y2 - y1;
    if (dy < 0) dy = -dy;
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx - dy;
    for (;;) {
        base[(size_t)y1 * pitch + x1] = color;
        if (x1 == x2 && y1 == y2) break;
        e2 = err + err;
        if (e2 > -dy) {
            err -= dy;
            x1 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y1 += sy;
        }
    }
}

static void gfx_swPoint(int x, int y, int colorArg) {
    SDL_Surface *surf = gfx_getCurPageSurface();
    if (!surf || x < 0 || x >= surf->w || y < 0 || y >= surf->h) return;
    ((uint8 *)surf->pixels)[(size_t)y * surf->pitch + x] = (uint8)colorArg;
}

void FAR CDECL gfx_drawLine(uint16 ux1, uint16 uy1, uint16 ux2, uint16 uy2) {
    GfxState FAR *s = gfx_getState();
    uint8 color = s->fillColor;
    int vx, vy;         /* blitOffset decomposed into a viewport origin */
    int x1, y1, x2, y2; /* endpoints translated into absolute page coords */
    int code1, code2;

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
    x1 = (int)(int16)ux1 + vx;
    y1 = (int)(int16)uy1 + vy;
    x2 = (int)(int16)ux2 + vx;
    y2 = (int)(int16)uy2 + vy;

    /* Clip the segment to [0,319]x[0,199]. */
    code1 = gfx_lineOutcode(x1, y1);
    code2 = gfx_lineOutcode(x2, y2);
    for (;;) {
        if ((code1 | code2) == 0) break;  /* trivially inside */
        if ((code1 & code2) != 0) return; /* trivially outside */
        {
            int outcode = code1 ? code1 : code2;
            int clipX = 0, clipY = 0;
            if (outcode & 8) {
                clipX = x1 + (long)(x2 - x1) * (199 - y1) / (y2 - y1);
                clipY = 199;
            } else if (outcode & 4) {
                clipX = x1 + (long)(x2 - x1) * (0 - y1) / (y2 - y1);
                clipY = 0;
            } else if (outcode & 2) {
                clipY = y1 + (long)(y2 - y1) * (319 - x1) / (x2 - x1);
                clipX = 319;
            } else {
                clipY = y1 + (long)(y2 - y1) * (0 - x1) / (x2 - x1);
                clipX = 0;
            }
            if (outcode == code1) {
                x1 = clipX;
                y1 = clipY;
                code1 = gfx_lineOutcode(x1, y1);
            } else {
                x2 = clipX;
                y2 = clipY;
                code2 = gfx_lineOutcode(x2, y2);
            }
        }
    }

    /* Submit the clipped, blitOffset-absolute segment. The software backend
     * Bresenhams it into the current page (gfx_swLine); the GL backend records it
     * for a crisp native-resolution replay. */
    r2d_submitLine(x1, y1, x2, y2, color);
}
/* drawLineWrapper - draw a line from the lineX1..lineY2 globals. gfx_drawLine
 * does the Cohen-Sutherland clipping itself, so this just marshals the globals
 * into its by-value args. */
extern int16 lineX1, lineY1, lineX2, lineY2;
void drawLineWrapper(void) {
    gfx_drawLine((uint16)lineX1, (uint16)lineY1, (uint16)lineX2, (uint16)lineY2);
}
/* Slot 0x20: register-called via the _gfx_setDrawColor shim — AH = fill colour.
 * Stores the clearRect/fill colour (MGRAPHIC slot 0x20 = `mov [fillColor],ah`). */
void FAR CDECL gfx_setDrawColor(uint16 color) {
    gfx_getState()->fillColor = (uint8)color;
}
void FAR CDECL gfx_nop23(void) { return; }
/* Slot 0x25/0x28: fill the per-row dirty spans. minBuf points at the per-row
 * dirtyMinBuf; the matching dirtyMaxBuf sits 0x1b8 bytes after it. For each row
 * y in [yMin..yMax] fill the span [minBuf[y]..maxBuf[y]] of the back buffer with
 * fillColor. This is the actual rectangle clear behind clearRect (MGRAPHIC slot
 * 0x25==0x28). A row whose min==max==0 or ==0x13F is treated as empty and
 * skipped, matching the original's range guard. (The DOS build passed BX = a
 * near offset into the caller's DS; the merged build passes a real pointer.) */
void FAR CDECL gfx_dirtyRect2(const int16 *spanMinBuf, uint16 yMin, uint16 yMax) {
    GfxState FAR *s = gfx_getState();
    const uint16 *minBuf = (const uint16 *)spanMinBuf;
    const uint16 *maxBuf = (const uint16 *)((const char *)spanMinBuf + 0x1b8);
    uint8 fill = s->fillColor;
    SDL_Surface *surf = ensurePage(0);
    uint8 *pagePx;
    int16 firstRow = (int16)yMin; /* AX */
    int16 lastRow = (int16)yMax;  /* CX */
    int y;
    if (!surf) return;
    pagePx = (uint8 *)surf->pixels;
    /* MGRAPHIC slot 0x25: `or ax,ax; js exit` — if firstRow < 0, draw nothing. */
    if (firstRow < 0) return;
    if (lastRow > 199) lastRow = 199; /* rowOffsets[] safety */
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
        if (spanHi < spanLo) continue; /* unsigned */
        if (spanHi == spanLo && (spanHi == 0 || spanHi == 319)) continue;
        /* Clamp the write extent to the visible row. The 3D projection emits
         * near-plane-clamped columns (e.g. ~0x7000), so an unclamped width would
         * loop tens of thousands of times per row in C (MGRAPHIC wraps cheaply in
         * the 64K page; we clip instead). This does NOT change the draw decision
         * above — only how many bytes land on screen. */
        if (spanLo > 319) continue; /* span off right edge */
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

int FAR CDECL gfx_setFont(uint16 ch, uint16 fontIdx) {
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
void FAR CDECL gfx_setFadeSteps(int steps) {
    (void)steps;
    return;
}
/* Slot 0x3e: linear byte offset of pixel (col,row) = col + rowTable[row].
 * MGRAPHIC's arg order is col FIRST ([ss:bx+4]), row SECOND ([ss:bx+6]) — the
 * opposite of the natural (y,x). egame computes every MFD viewport origin via
 * gfx_setBlitOffset(gfx_calcRowAddr(xOrigin, yOrigin)); an earlier (y,x) reading
 * transposed the result (col*320+row instead of row*320+col), which left the
 * main viewport correct (origin 0,0) but transposed every sub-window — the
 * middle MFD's −32x/+32y offset and the left/right MFD misplacement. */
int FAR CDECL gfx_calcRowAddr(int col, int row) {
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
void FAR CDECL gfx_setOvlVal1(int val) {
    gfxOvlClipScratch[0] = (uint16)val;
    return;
}
void FAR CDECL gfx_setOvlVal2(int val) {
    gfxOvlClipScratch[1] = (uint16)val;
    return;
}
int FAR CDECL gfx_getPresetOffset1(void) {
    return 0x5580; /* baked constant — NOT live blitOffset (that is slot 0x1e) */
}
int FAR CDECL gfx_getModeFlag(void) {
    GfxState FAR *s = gfx_getState();
    return (int)s->modeFlag;
}
void FAR CDECL gfx_setDacAnimCount(uint16 count) {
    GfxState FAR *s = gfx_getState();
    s->dacCounter = (uint8)count;
    return;
}
void FAR CDECL gfx_commitPage(void) {
    gfx_presentPage(0);
}
void FAR CDECL gfx_blitSpriteClipped(int16 *ptr) { gfx_blitSprite((struct SpriteParams *)ptr); }
void FAR CDECL gfx_blitSpriteOpaque(int16 *ptr) { gfx_blitSprite((struct SpriteParams *)ptr); }

/* ---- Slot 0x0b: gfx_complexRender — HUD pitch-ladder renderer ----
 * MGRAPHIC code @0x615. Register-called (via the _gfx_complexRender shim in
 * regshim.asm) with BX=row, DX=orientation(dl), CX=mode-gate(cl), SI=ladder
 * variant (0 or 2). It writes colour 0x0f into the back buffer along a column whose
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
    71, 248, 120, 200, 26, 26, 68, 68, 86, 86, 98, 98};

void FAR CDECL gfx_complexRender(int bxArg, int dxArg, int cxArg, int siArg) {
    GfxState FAR *s = gfx_getState();
    SDL_Surface *surf;
    uint8 *page;
    uint8 color = 0x0f;
    int dir; /* +1 (SI!=0, cld) or -1 (SI==0, std) */
    int wi;  /* word index into the geometry table */
    uint16 base, loY, hiY;
    uint16 bx;
    long t; /* 1-based iteration index of the naive MGRAPHIC loop */
    int cl = cxArg & 0xff;
    int dl = dxArg & 0xff;

    dir = (siArg == 0) ? -1 : 1; /* set from the ORIGINAL si (before +=4) */
    bx = (uint16)(bxArg - 1);
    if ((int8)dl >= 1) bx += 20; /* `cmp dl,1; jl` is a signed byte test */
    if (cl != 0) {
        siArg += 4;
        bx++;
    }

    wi = siArg / 2; /* si is a byte offset; table is word-indexed */
    if (wi < 0 || wi + 8 > 11) return;
    base = (uint16)g_ladderGeom[wi];
    loY = (uint16)g_ladderGeom[wi + 4];
    hiY = (uint16)g_ladderGeom[wi + 8];

    surf = ensurePage(0);
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

    for (;; t++) {
        int phase, thickness;
        if (bx < loY) break;           /* unsigned, matches `jc` */
        phase = (int)((t - 1L) % 10L); /* thickness counter: 1..10 phase */
        thickness = (phase == 0) ? 10 : phase;
        if (bx <= hiY) { /* `ja` skips the store when bx>hiY */
            uint16 dstOff = (uint16)(s->rowOffsets[bx] + base);
            if (thickness == 5) {
                page[dstOff] = color;
                dstOff = (uint16)(dstOff + dir);
                page[dstOff] = color;
            } else if (thickness == 10) {
                page[dstOff] = color;
                dstOff = (uint16)(dstOff + dir);
                page[dstOff] = color;
                dstOff = (uint16)(dstOff + dir);
                if (cl == 0) page[dstOff] = color;
            } else {
                page[dstOff] = color;
            }
        }
        bx -= 2;
    }
}
void FAR CDECL gfx_setBlitOffset2(void) {
    GfxState FAR *s = gfx_getState();
    s->blitOffset = 0;
    return;
}
int FAR CDECL gfx_getPresetOffset2(void) { return 0x1950; } /* baked constant 0x1950 */
int FAR CDECL gfx_getBlitOffset(void) {
    GfxState FAR *s = gfx_getState();
    return (int)s->blitOffset;
}
/* Slot 0x2c: advance the fire colour-cycle and present the frame, once per frame
 * from gameMainLoop (egame_rc.asm). With a single back buffer, compose and
 * present target the one surface, so there is no back->front copy — just present
 * it. Args (AX/BX) ignored. */
void FAR CDECL gfx_dacAnimate(void) {
    GfxState FAR *s = gfx_getState();
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
            s->cycleClockNs = now; /* fell far behind: snap forward */
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
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0x2a}, {0x00, 0x2a, 0x00}, {0x00, 0x2a, 0x2a}, {0x2a, 0x00, 0x00}, {0x2a, 0x00, 0x2a}, {0x2a, 0x15, 0x00}, {0x2a, 0x2a, 0x2a}, {0x15, 0x15, 0x15}, {0x15, 0x15, 0x3f}, {0x15, 0x3f, 0x15}, {0x15, 0x3f, 0x3f}, {0x3f, 0x15, 0x15}, {0x3f, 0x15, 0x3f}, {0x3f, 0x3f, 0x15}, {0x3f, 0x3f, 0x3f}};
static const uint8 g_dacFireIndex[4] = {0x0c, 0x04, 0x0c, 0x0e};

void FAR CDECL gfx_dacCycle(void) {
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
    gfxPaletteGen++;

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

/* ---- Initialise the shared GfxState ----
 * Called once at startup from game_init(). In the merged single-process build the
 * gfx functions are called directly (no slot-dispatch table), so this just sets
 * the state defaults. */
void gfx_initState(void) {
    GfxState FAR *s = gfx_getState();

    s->modeFlag = 1;
    s->dacPhase = 0x4d2; /* MGRAPHIC data-seg 0x1ccc seed (dacCycle phase) */
    /* rowOffsetsReady is zero by default (file-scope global). */
}
