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

void r2d_computeMapping(int virtW, int virtH, int winW, int winH, R2DMapping *out) {
    float sw = (float)winW / (float)virtW;
    float sh = (float)winH / (float)virtH;
    float s = sw < sh ? sw : sh;
    out->virtW = virtW;
    out->virtH = virtH;
    out->winW = winW;
    out->winH = winH;
    out->scale = s;
    out->offX = (int)((winW - virtW * s) * 0.5f);
    out->offY = (int)((winH - virtH * s) * 0.5f);
}

static void (*s_swPresent)(struct SDL_Surface *page, int shakeOffset);

void r2d_registerSoftwarePresent(void (*present)(struct SDL_Surface *page, int shakeOffset)) {
    s_swPresent = present;
}

/* ---- Native 2D vector layer ---------------------------------------------- */

static void (*s_swLine)(int, int, int, int, int);
static void (*s_swPoint)(int, int, int);

static R2DOverlayPrim *s_prims;
static int s_primCount, s_primCap;

/* When set, submissions rasterize into the page even on a GL flight frame (for
 * the radar-scope MFD, whose lines must compose under their blip sprites). */
static int s_forceRaster;

void r2d_setForceRaster(int on) { s_forceRaster = on; }

void r2d_registerSoftwarePrims(void (*line)(int, int, int, int, int),
                               void (*point)(int, int, int)) {
    s_swLine = line;
    s_swPoint = point;
}

static void (*s_swImage)(R2DImage *, int, int, int, int, int, int, int);

void r2d_registerSoftwareImage(void (*image)(R2DImage *, int, int, int, int, int, int, int)) {
    s_swImage = image;
}

static R2DOverlayPrim *primGrow(void) {
    if (s_primCount >= s_primCap) {
        int cap = s_primCap ? s_primCap * 2 : 256;
        R2DOverlayPrim *grown = (R2DOverlayPrim *)SDL_realloc(s_prims, (size_t)cap * sizeof(*grown));
        if (!grown) return NULL;
        s_prims = grown;
        s_primCap = cap;
    }
    return &s_prims[s_primCount++];
}

void r2d_submitImage(R2DImage *img, int srcX, int srcY, int w, int h,
                     int dstX, int dstY, int key) {
    /* On a GL flight frame record into the ordered overlay stream for a
     * textured-quad replay (so sprites and lines keep submission order); the
     * force-raster region (radar scope) and non-GL frames rasterize into the page,
     * exactly like r2d_submitLine. */
    if (img && r2d_vectorActive() && !s_forceRaster) {
        R2DOverlayPrim *p = primGrow();
        if (!p) return;
        p->kind = R2D_PRIM_IMAGE;
        p->color = 0;
        p->x1 = (short)dstX; p->y1 = (short)dstY;
        p->x2 = 0; p->y2 = 0;
        p->img = img;
        p->srcX = (short)srcX; p->srcY = (short)srcY;
        p->imgW = (short)w; p->imgH = (short)h;
        p->key = (short)key;
    } else if (s_swImage) {
        s_swImage(img, srcX, srcY, w, h, dstX, dstY, key);
    }
}

/* Set for the duration of a GL flight frame (between gl_beginScene's main 3D view
 * and the present). 2D submissions only record for native replay while this is
 * set — pure-2D screens (debrief/briefing/menus) have no 3D pass, so their lines
 * rasterize into the page as before (accumulating, correctly ordered with their
 * sprites). */
static int s_vectorFrame;

void r2d_vectorBeginFrame(void) { s_vectorFrame = 1; }

int r2d_vectorActive(void) {
    static int cached = -1;
    if (!s_vectorFrame) return 0;
    if (cached < 0) {
        const char *e = SDL_getenv("F15_VECTOR2D");
        cached = (e && (e[0] == '0' || e[0] == 'n' || e[0] == 'N')) ? 0 : 1;
    }
    return cached;
}

static void primAppend(int x1, int y1, int x2, int y2, int color, int kind) {
    R2DOverlayPrim *p = primGrow();
    if (!p) return;
    p->kind = (unsigned char)kind;
    p->color = (unsigned char)color;
    p->x1 = (short)x1; p->y1 = (short)y1;
    p->x2 = (short)x2; p->y2 = (short)y2;
    p->img = NULL;
}

void r2d_submitLine(int x1, int y1, int x2, int y2, int color) {
    if (r2d_vectorActive() && !s_forceRaster) primAppend(x1, y1, x2, y2, color, R2D_PRIM_LINE);
    else if (s_swLine) s_swLine(x1, y1, x2, y2, color);
}

void r2d_submitPoint(int x, int y, int color) {
    if (r2d_vectorActive() && !s_forceRaster) primAppend(x, y, x, y, color, R2D_PRIM_POINT);
    else if (s_swPoint) s_swPoint(x, y, color);
}

const R2DOverlayPrim *r2d_overlayPrims(int *count) {
    if (count) *count = s_primCount;
    return s_prims;
}

void r2d_vectorMarkPresented(void) {
    s_primCount = 0;
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
