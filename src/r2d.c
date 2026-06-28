/* 2D overlay renderer seam (see docs/render-2d-overlay.md, Step 1) — the
 * present/compose dispatch for the 2D overlay page.
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
    SDL_Surface *surf; /* INDEX8 backing store */
};

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
