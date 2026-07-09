/* 2D overlay renderer seam — the present/compose dispatch for the 2D overlay page.
 *
 * Selection follows the active 3D backend rather than re-probing: the GL backend
 * owns the window (it requested an SDL_WINDOW_OPENGL surface instead of an
 * SDL_Renderer), so 2D must composite through GL exactly when 3D does. The
 * software present is provided by gfx_impl.c (which owns the SDL_Renderer) via a
 * registered callback so this module needs no SDL renderer state of its own. */
#include <SDL3/SDL.h>
#include "r2d.h"
#include "r3d_gl.h"

/* The shared 256-entry VGA palette lives in the software backend (gfx_impl.c).
 * Image blits copy raw indices and never read it, but attaching it keeps an
 * INDEX8 image well-formed for any SDL surface op and for the present path. */
struct SDL_Palette *gfx_getPalette(void);

struct R2DImage {
    SDL_Surface *surf;    /* INDEX8 backing store */
    unsigned int cacheTex; /* backend texture cache (GLuint on GL); 0 = none */
    int cacheGen;          /* palette generation the cache was built for */
};

static void (*s_imageDestroyHook)(R2DImage *);

void r2d_registerImageDestroy(void (*hook)(R2DImage *)) { s_imageDestroyHook = hook; }

unsigned int r2d_imageCacheTex(R2DImage *img) { return img ? img->cacheTex : 0; }
int r2d_imageCacheGen(R2DImage *img) { return img ? img->cacheGen : -1; }
void r2d_imageSetCache(R2DImage *img, unsigned int tex, int gen) {
    if (!img) return;
    img->cacheTex = tex;
    img->cacheGen = gen;
}

void r2d_blit(SDL_Surface *src, int sx, int sy,
              SDL_Surface *dst, int dx, int dy,
              int w, int h, int key) {
    const Uint8 *sbase;
    Uint8 *dbase;
    int spitch, dpitch, c0, c1, r0, r1, row, col;

    if (!src || !dst || w <= 0 || h <= 0) return;

    /* Intersect the run [0,w) x [0,h) with both surfaces: column `col` reads
     * src(sx+col)/writes dst(dx+col), so valid cols satisfy both bounds. */
    c0 = 0; c1 = w;
    if (-sx > c0) c0 = -sx;
    if (-dx > c0) c0 = -dx;
    if (src->w - sx < c1) c1 = src->w - sx;
    if (dst->w - dx < c1) c1 = dst->w - dx;
    r0 = 0; r1 = h;
    if (-sy > r0) r0 = -sy;
    if (-dy > r0) r0 = -dy;
    if (src->h - sy < r1) r1 = src->h - sy;
    if (dst->h - dy < r1) r1 = dst->h - dy;
    if (c1 <= c0 || r1 <= r0) return;

    sbase = (const Uint8 *)src->pixels;
    dbase = (Uint8 *)dst->pixels;
    spitch = src->pitch;
    dpitch = dst->pitch;
    for (row = r0; row < r1; row++) {
        const Uint8 *srow = sbase + (size_t)(sy + row) * spitch + sx;
        Uint8 *drow = dbase + (size_t)(dy + row) * dpitch + dx;
        if (key < 0) {
            SDL_memmove(drow + c0, srow + c0, (size_t)(c1 - c0));
        } else {
            for (col = c0; col < c1; col++) {
                Uint8 px = srow[col];
                if (px != (Uint8)key) drow[col] = px;
            }
        }
    }
}

R2DImage *r2d_registerImage(int w, int h) {
    R2DImage *img;
    SDL_Palette *pal;
    if (w <= 0 || h <= 0) return NULL;
    img = (R2DImage *)SDL_calloc(1, sizeof(*img));
    if (!img) return NULL;
    img->surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_INDEX8);
    if (!img->surf) {
        SDL_free(img);
        return NULL;
    }
    pal = gfx_getPalette();
    if (pal) SDL_SetSurfacePalette(img->surf, pal);
    return img;
}

R2DImage *r2d_captureImage(SDL_Surface *src, int x, int y, int w, int h) {
    R2DImage *img = r2d_registerImage(w, h);
    if (!img) return NULL;
    r2d_blit(src, x, y, img->surf, 0, 0, w, h, -1);
    return img;
}

R2DImage *r2d_imageFromSurface(SDL_Surface *surf) {
    R2DImage *img;
    if (!surf) return NULL;
    img = (R2DImage *)SDL_calloc(1, sizeof(*img));
    if (!img) return NULL;
    img->surf = surf;   /* adopted; any format (INDEX8 or RGBA) */
    return img;
}

SDL_Surface *r2d_imageSurface(R2DImage *img) { return img ? img->surf : NULL; }

void r2d_drawImage(R2DImage *img, int srcX, int srcY, int w, int h,
                   SDL_Surface *dst, int dstX, int dstY, int key) {
    if (!img) return;
    r2d_blit(img->surf, srcX, srcY, dst, dstX, dstY, w, h, key);
}

void r2d_releaseImage(R2DImage *img) {
    if (!img) return;
    if (s_imageDestroyHook) s_imageDestroyHook(img);
    SDL_DestroySurface(img->surf);
    SDL_free(img);
}

void r2d_computeMapping(int virtW, int virtH, int winW, int winH,
                        int squarePixels, R2DMapping *out) {
    /* parY stretches the box vertically so it displays at TARGET_DAR (the DS box is
     * virtW x virtH*parY, square-pixel); 1.0 keeps square pixels (legacy present). */
    float parY = squarePixels ? 1.0f
                              : ((float)virtW / (float)virtH) / TARGET_DAR;
    float dispH = (float)virtH * parY;
    float sw = (float)winW / (float)virtW;
    float sh = (float)winH / dispH;
    float s = sw < sh ? sw : sh;  /* uniform DS scale that fits the window */
    out->virtW = virtW;
    out->virtH = virtH;
    out->winW = winW;
    out->winH = winH;
    out->scaleX = s;
    out->scaleY = s * parY;
    out->offX = (int)((winW - virtW * out->scaleX) * 0.5f);
    out->offY = (int)((winH - virtH * out->scaleY) * 0.5f);
}

static void (*s_swPresent)(struct SDL_Surface *page, int shakeOffset);

void r2d_registerSoftwarePresent(void (*present)(struct SDL_Surface *page, int shakeOffset)) {
    s_swPresent = present;
}

/* ---- Native 2D overlay layer --------------------------------------------- */

/* On a GL flight frame every 2D submission draws IMMEDIATELY, directly onto the
 * active framebuffer at its call site (r3dgl_draw*), at native window resolution —
 * no recording, no present-time replay. Off a GL frame (software backend, or the
 * pure-2D menu/briefing/debrief screens, which never open a vector frame) the same
 * call bakes into the retained 320x200 page via the registered rasterizer, exactly
 * as before. This IS the "framebuffer retained?" question inverted. */

static void (*s_swLine)(int, int, int, int, int);
static void (*s_swPoint)(int, int, int);

void r2d_registerSoftwarePrims(void (*line)(int, int, int, int, int),
                               void (*point)(int, int, int)) {
    s_swLine = line;
    s_swPoint = point;
}

static void (*s_swImage)(R2DImage *, int, int, int, int, int, int, int);

void r2d_registerSoftwareImage(void (*image)(R2DImage *, int, int, int, int, int, int, int)) {
    s_swImage = image;
}

void r2d_submitImage(R2DImage *img, int srcX, int srcY, int w, int h,
                     int dstX, int dstY, int key) {
    if (img && r2d_vectorActive()) {
        r3dgl_drawImage(img, srcX, srcY, w, h, dstX, dstY, w, h, key); /* 1:1 footprint */
    } else if (s_swImage) {
        s_swImage(img, srcX, srcY, w, h, dstX, dstY, key);
    }
}

void r2d_submitImageScaled(R2DImage *img, int srcX, int srcY, int srcW, int srcH,
                           int dstX, int dstY, int dstW, int dstH, int key) {
    /* GL-only (HD art). Off a vector frame there is no software scaled-blit, so
     * drop — callers fall back to the legacy sprite. */
    if (!img || !r2d_vectorActive()) return;
    r3dgl_drawImage(img, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, key);
}

int r2d_submitImageF(R2DImage *img, int srcX, int srcY, int srcW, int srcH,
                     float dstX, float dstY, float dstW, float dstH, int key) {
    if (!img || !r2d_vectorActive()) return 0;
    r3dgl_drawImageF(img, srcX, srcY, srcW, srcH, dstX, dstY, dstW, dstH, key);
    return 1;
}

/* Set for the duration of a GL flight frame (between gl_beginScene's main 3D view
 * and the present). 2D submissions draw immediately while this is set — pure-2D
 * screens (debrief/briefing/menus) have no 3D pass, so their primitives bake into
 * the page as before (accumulating, correctly ordered with their sprites). */
static int s_vectorFrame;

void r2d_vectorBeginFrame(void) {
    s_vectorFrame = 1;
    if (r3dgl_active()) r3dgl_beginOverlay(); /* set the immediate-draw letterbox+shake context */
}

int r2d_vectorActive(void) { return s_vectorFrame; }

int r2d_hasNativeOverlay(void) { return r3dgl_active(); }

void r2d_submitLine(int x1, int y1, int x2, int y2, int color) {
    if (r2d_vectorActive()) r3dgl_drawLine(x1, y1, x2, y2, color);
    else if (s_swLine) s_swLine(x1, y1, x2, y2, color);
}

void r2d_submitPoint(int x, int y, int color) {
    if (r2d_vectorActive()) r3dgl_drawPoint(x, y, color);
    else if (s_swPoint) s_swPoint(x, y, color);
}

void r2d_submitRect(int x0, int y0, int x1, int y1, int color) {
    if (r2d_vectorActive()) r3dgl_drawRect(x0, y0, x1, y1, color);
    /* else: caller bakes via fillSpanRect (software retained page) */
}

/* Cohen-Sutherland clip of an integer segment to the inclusive box [x0,x1]x[y0,y1].
 * Returns 0 if fully outside (nothing to draw). Mirrors the drawClipLineGlobal /
 * gfx_drawLine clippers; used only for the software scope-line fallback. */
static int csOutcode(int x, int y, int x0, int y0, int x1, int y1) {
    int c = 0;
    if (x < x0) c |= 1; else if (x > x1) c |= 2;
    if (y < y0) c |= 4; else if (y > y1) c |= 8;
    return c;
}
static int csClip(int *px1, int *py1, int *px2, int *py2,
                  int x0, int y0, int x1, int y1) {
    int ax = *px1, ay = *py1, bx = *px2, by = *py2;
    int c1 = csOutcode(ax, ay, x0, y0, x1, y1);
    int c2 = csOutcode(bx, by, x0, y0, x1, y1);
    for (;;) {
        int oc, cx = 0, cy = 0;
        if ((c1 | c2) == 0) break;
        if ((c1 & c2) != 0) return 0;
        oc = c1 ? c1 : c2;
        if (oc & 8)      { cx = ax + (long)(bx - ax) * (y1 - ay) / (by - ay); cy = y1; }
        else if (oc & 4) { cx = ax + (long)(bx - ax) * (y0 - ay) / (by - ay); cy = y0; }
        else if (oc & 2) { cy = ay + (long)(by - ay) * (x1 - ax) / (bx - ax); cx = x1; }
        else             { cy = ay + (long)(by - ay) * (x0 - ax) / (bx - ax); cx = x0; }
        if (oc == c1) { ax = cx; ay = cy; c1 = csOutcode(ax, ay, x0, y0, x1, y1); }
        else          { bx = cx; by = cy; c2 = csOutcode(bx, by, x0, y0, x1, y1); }
    }
    *px1 = ax; *py1 = ay; *px2 = bx; *py2 = by;
    return 1;
}

void r2d_submitScopeLine(float x1, float y1, float x2, float y2, int color,
                         int cx0, int cy0, int cx1, int cy1) {
    if (r2d_vectorActive()) {
        r3dgl_drawScopeLine(x1, y1, x2, y2, color, cx0, cy0, cx1, cy1);
    } else if (s_swLine) {
        int ix1 = (int)SDL_floorf(x1 + 0.5f), iy1 = (int)SDL_floorf(y1 + 0.5f);
        int ix2 = (int)SDL_floorf(x2 + 0.5f), iy2 = (int)SDL_floorf(y2 + 0.5f);
        if (csClip(&ix1, &iy1, &ix2, &iy2, cx0, cy0, cx1 - 1, cy1 - 1))
            s_swLine(ix1, iy1, ix2, iy2, color);
    }
}

void r2d_submitPoly(const short *xy, int n, int color,
                    int clipX0, int clipY0, int clipX1, int clipY1) {
    /* GL vector frame only; software fills the face in the page span rasterizer
     * and never reaches here (the call site branches on r2d_vectorActive first). */
    if (n < 3 || !r2d_vectorActive()) return;
    r3dgl_drawPoly(xy, n, color, clipX0, clipY0, clipX1, clipY1);
}

void r2d_vectorMarkPresented(void) {
    s_vectorFrame = 0;
}

void r2d_present(struct SDL_Surface *page, int shakeOffset) {
    if (r3dgl_active()) {
        r3dgl_present(page, shakeOffset);
        return;
    }
    if (s_swPresent) s_swPresent(page, shakeOffset);
}

const char *r2d_backendName(void) {
    return r3dgl_active() ? "opengl1" : "software";
}
