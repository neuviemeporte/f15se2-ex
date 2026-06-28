#ifndef R2D_H
#define R2D_H

/*
 * 2D overlay renderer seam (see docs/render-2d-overlay.md).
 *
 * The 2D overlay (HUD, sprites, text, menus, cockpit) is submitted to the *same*
 * renderer as the in-flight 3D path (r3d.h) — there is no separate 2D backend.
 *
 * Step 1 (this file's current scope) confines only the present/compose boundary
 * behind the renderer: the game still draws into the 320x200 paletted page
 * surfaces (gfx_impl.c), and this seam owns *how that virtual overlay page reaches
 * the window*. The software backend presents it through SDL_Renderer (letterbox);
 * the GL backend composites it as a flat textured quad over the GL 3D
 * (r3dgl_present). The active 2D backend always matches the active 3D backend:
 * the GL backend owns the window, so the two must agree.
 *
 * Later steps move the page model -> image/primitive submission + a hidden back
 * buffer and add virtual-vs-real resolution + oversized submission for widescreen
 * (docs/render-2d-overlay.md, Steps 2-7).
 */

struct SDL_Surface;

/*
 * Virtual-vs-real resolution (docs/render-2d-overlay.md, Step 2).
 *
 * 2D is authored in a virtual coordinate box (320x200 in flight, 640x350 for
 * the hi-res title). R2DMapping is the uniform-scale + centring placement of
 * that box onto the real window (letterbox/pillarbox bars on the unused axis).
 *
 * r2d_computeMapping is the SINGLE source of truth for the virtual->window
 * mapping: the software present, the GL 2D composite and the GL 3D viewport all
 * derive their placement from it, so the HUD, the 3D and the present quad stay
 * aligned and nothing stretches on non-1.6 displays.
 *
 * Submission contract: 2D is authored inside the virtual box, but a submission
 * MAY fall outside it (oversized, reserved for future widescreen 2D art) — out-
 * of-box pixels are clipped to the window, NOT to the virtual box. No stock call
 * site relies on this; the stock UI never leaves the box.
 */
typedef struct {
    int   virtW, virtH;  /* virtual (authoring) resolution */
    int   winW, winH;    /* real window framebuffer size in pixels */
    float scale;         /* window pixels per virtual pixel (uniform) */
    int   offX, offY;    /* top-left of the centred virtual box, window pixels */
} R2DMapping;

/* Fit a virtW x virtH virtual box into a winW x winH window preserving aspect
 * (uniform scale, centred). The one place the letterbox math lives. */
void r2d_computeMapping(int virtW, int virtH, int winW, int winH, R2DMapping *out);

/*
 * Image submission API (docs/render-2d-overlay.md, Step 3).
 *
 * An R2DImage is an owned 2D image (sprite sheet, decoded PIC, captured screen
 * region) the renderer can sample. Today the software realization is an INDEX8
 * SDL_Surface and drawing is a direct clipped blit (no projection); a later GPU
 * backend realizes images as textures and draws them as ortho quads. Callers
 * submit *images and rects*, never "textured quad" — the realization is the
 * backend's (docs/render-2d-overlay.md, "one renderer, 2D submission path").
 */
typedef struct R2DImage R2DImage;

/* Create a blank w x h image (cleared to index 0). NULL on failure. */
R2DImage *r2d_registerImage(int w, int h);

/* Create a w x h image holding a snapshot of the (x,y,w,h) region of `src`.
 * Used for the cockpit/popup save-restore (capture a screen region, draw it back
 * later) — the page->page copy of the page-model era. NULL on failure. */
R2DImage *r2d_captureImage(struct SDL_Surface *src, int x, int y, int w, int h);

/* The image's backing surface, for code that fills it directly (the PIC decoder
 * decodes into this). NULL if img is NULL. */
struct SDL_Surface *r2d_imageSurface(R2DImage *img);

/* Draw the (srcX,srcY,w,h) sub-rect of `img` into `dst` at (dstX,dstY). If
 * `key` >= 0, source pixels equal to it are skipped (transparency, conventionally
 * 0); if `key` < 0 the copy is opaque. Clipped to both surfaces. */
void r2d_drawImage(R2DImage *img, int srcX, int srcY, int w, int h,
                   struct SDL_Surface *dst, int dstX, int dstY, int key);

/* Release an image. Safe on NULL. */
void r2d_releaseImage(R2DImage *img);

/* The one clipped INDEX8 blit underlying every 2D image/page copy: copy a w x h
 * rect from src(srcX,srcY) to dst(dstX,dstY). key >= 0 skips matching source
 * pixels (sprite transparency); key < 0 is an opaque copy. Clipped to both
 * surfaces (negative offsets included). Operates directly on surfaces so the
 * page->page copies (copyRect/blitToCurrent) and the image draws share it. */
void r2d_blit(struct SDL_Surface *src, int srcX, int srcY,
              struct SDL_Surface *dst, int dstX, int dstY,
              int w, int h, int key);

/* Present the virtual overlay page (its w/h are the virtual size) to the window
 * through the active backend. shakeOffset is the explosion screen-shake in
 * virtual pixels (0-3), applied as a horizontal present offset (Step 6 moves it
 * to a scene shake). */
void r2d_present(struct SDL_Surface *page, int shakeOffset);

/* Name of the active 2D backend ("opengl1" or "software"); follows the 3D
 * backend. */
const char *r2d_backendName(void);

/* The software path's SDL_Renderer present lives in gfx_impl.c (it owns the
 * renderer); gfx_impl registers it here at video init so r2d_present can dispatch
 * to it without the renderer leaking into this module. */
void r2d_registerSoftwarePresent(void (*present)(struct SDL_Surface *page, int shakeOffset));

#endif /* R2D_H */
