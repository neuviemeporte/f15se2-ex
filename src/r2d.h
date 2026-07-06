#ifndef R2D_H
#define R2D_H

/*
 * 2D overlay renderer seam.
 *
 * The 2D overlay (HUD, sprites, text, menus, cockpit) is submitted to the *same*
 * renderer as the in-flight 3D path (r3d.h) — there is no separate 2D backend.
 *
 * This seam owns the present/compose boundary and the image/primitive submission
 * path. The game draws into the 320x200 paletted page surfaces (gfx_impl.c), and
 * the seam owns *how that virtual overlay page reaches the window*. The software
 * backend presents it through SDL_Renderer (letterbox); the GL backend composites
 * it as a flat textured quad over the GL 3D (r3dgl_present). The active 2D backend
 * always matches the active 3D backend: the GL backend owns the window, so the two
 * must agree.
 */

struct SDL_Surface;

/*
 * Virtual-vs-real resolution.
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
/* Target display aspect ratio of the virtual box. VGA 13h (320x200) and the EGA
 * title (640x350) were both shown on a 4:3 CRT, so their pixels were non-square;
 * aspect-correcting a mapping stretches the box to this ratio (see squarePixels). */
#define TARGET_DAR (4.0f / 3.0f)

typedef struct {
    int   virtW, virtH;    /* virtual (authoring) resolution */
    int   winW, winH;      /* real window framebuffer size in pixels */
    float scaleX, scaleY;  /* window pixels per virtual pixel, per axis. Equal when
                            * squarePixels; scaleY = scaleX * parY when aspect-
                            * corrected (parY = (virtW/virtH)/TARGET_DAR, = 1.2 for
                            * 320x200) so the box displays at TARGET_DAR. */
    int   offX, offY;      /* top-left of the centred virtual box, window pixels */
} R2DMapping;

/* Fit a virtW x virtH virtual box into a winW x winH window (uniform DS scale,
 * centred). The one place the letterbox math lives. When squarePixels is 0 the box
 * is aspect-corrected to TARGET_DAR (non-square pixels, scaleY > scaleX); when 1 the
 * pixels stay square (scaleX == scaleY) — the fast/legacy present. */
void r2d_computeMapping(int virtW, int virtH, int winW, int winH,
                        int squarePixels, R2DMapping *out);

/*
 * Image submission API.
 *
 * An R2DImage is an owned 2D image (sprite sheet, decoded PIC, captured screen
 * region) the renderer can sample. The software realization is an INDEX8
 * SDL_Surface and drawing is a direct clipped blit (no projection); a GPU backend
 * realizes images as textures and draws them as ortho quads. Callers submit
 * *images and rects*, never "textured quad" — the realization is the backend's.
 */
typedef struct R2DImage R2DImage;

/* Create a blank w x h image (cleared to index 0). NULL on failure. */
R2DImage *r2d_registerImage(int w, int h);

/* Create a w x h image holding a snapshot of the (x,y,w,h) region of `src`.
 * Used for the cockpit/popup save-restore (capture a screen region, draw it back
 * later). NULL on failure. */
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

/*
 * Native 2D vector layer.
 *
 * The HUD/MFD primitives (lines, pitch-ladder, symbology) and sprites are
 * *submitted* to the renderer rather than rasterized into the 320x200 page. The
 * software backend realizes a submission by rasterizing into the page (the
 * low-end/DOS path, via the registered callbacks below). The GL backend records
 * the submission in 320-space and replays it at the **native window resolution**
 * over the composited frame — a crisp vector HUD and sharp sprites instead of an
 * upscaled low-res image. Lines, points and images share ONE ordered stream so
 * they replay in submission order (a sprite drawn after a line lands over it).
 * Same call sites, the realization is the backend's.
 */
typedef struct {
    unsigned char kind;   /* R2D_PRIM_LINE / _POINT / _IMAGE / _POLY */
    unsigned char color;  /* VGA palette index (line / point / poly fill) */
    short x1, y1, x2, y2; /* line/point: absolute 320-space, clipped to the page.
                           * image: (x1,y1) is the destination corner.
                           * poly: unused (vertices live in the poly pool). */
    /* image submission (kind == R2D_PRIM_IMAGE): the sub-rect of `img` to draw.
     * poly submission (kind == R2D_PRIM_POLY): srcX = first vertex index into the
     * poly-vertex pool (r2d_overlayPolyVerts), srcY = vertex count; x1,y1,x2,y2 =
     * the clip rect (absolute 320-space, half-open) the fill is scissored to. */
    R2DImage *img;
    short srcX, srcY, imgW, imgH;
    short key;            /* <0 opaque; >=0 transparent on that index (sprites: 0) */
} R2DOverlayPrim;
#define R2D_PRIM_LINE  0
#define R2D_PRIM_POINT 1
#define R2D_PRIM_IMAGE 2
#define R2D_PRIM_POLY  3

/* Marks the start of a GL flight frame's 2D overlay (called from the 3D backend
 * once the main 3D view begins). Only between this and the present do 2D
 * submissions record for native replay; pure-2D screens (no 3D pass) rasterize
 * into the page as before. */
void r2d_vectorBeginFrame(void);

/* Whether 2D primitive submissions should be recorded for native replay (a GL
 * flight frame, unless disabled via F15_VECTOR2D=0) rather than rasterized into
 * the page. The gfx submission points branch on this. */
int r2d_vectorActive(void);

/* Whether the active backend replays a submitted overlay at native resolution
 * (the GL backend) as opposed to rasterizing it into the page (software). A 2D
 * screen that wants a native vector frame must gate r2d_vectorBeginFrame() on
 * this: on the software backend there is no native replay, so entering a vector
 * frame would only record the primitives and never draw them. */
int r2d_hasNativeOverlay(void);

/* Whether the active backend RETAINS the 2D overlay across frames. The software
 * backend rasterizes into a page surface that persists until overwritten, so a
 * cached sub-image (the tac map baked into g_eg2dBacking) can be re-composited
 * each frame without re-rendering it — a lightweight-renderer optimization. The
 * GL backend rebuilds the native vector/quad stream every present (r2d_vector-
 * MarkPresented clears it), so anything on that layer must be re-submitted each
 * frame. Callers that cache-and-blit a region (redrawTacMap) branch on this: keep
 * the cache when retained, re-render every frame when not. Inverse of a GL vector
 * frame; false on software and on pure-2D screens (which also rasterize/persist). */
int r2d_overlayRetained(void);

/* Submit a clipped 2D line / point in absolute 320-space with a palette colour
 * index. Records for native replay when r2d_vectorActive(), else hands off to
 * the registered software rasterizer. */
void r2d_submitLine(int x1, int y1, int x2, int y2, int color);
void r2d_submitPoint(int x, int y, int color);

/* Submit a filled convex polygon: `n` vertices as interleaved x,y pairs in
 * absolute 320-space, filled with palette colour `color`. Only meaningful on a
 * GL vector frame (records for a native GL_POLYGON replay); a no-op when vector
 * recording is inactive, since the software backend fills such faces directly in
 * the page span rasterizer (the tac-map fill path branches on r2d_vectorActive()
 * and never calls this). Used for the left-MFD terrain-map tile fills. The verts
 * are the UNCLIPPED projected polygon; (clipX0,clipY0)-(clipX1,clipY1) is the MFD
 * rect (absolute 320-space, half-open) the GL fill is scissored to. */
void r2d_submitPoly(const short *xy, int n, int color,
                    int clipX0, int clipY0, int clipX1, int clipY1);

/* The software backend (gfx_impl.c) registers how it rasterizes a submitted
 * line/point into the page, so r2d need not own page state. */
void r2d_registerSoftwarePrims(void (*line)(int x1, int y1, int x2, int y2, int color),
                               void (*point)(int x, int y, int color));

/*
 * Image (sprite) submission.
 *
 * The UI/HUD sprites are *submitted* to the renderer rather than the call site
 * blitting straight into a page. Draw the (srcX,srcY,w,h) sub-rect of `img` at
 * (dstX,dstY); key>=0 skips matching source pixels (sprite transparency,
 * conventionally 0), key<0 is opaque. On a GL flight frame the submission records
 * into the ordered overlay stream for a textured-quad replay; otherwise the
 * software backend realizes it as a clipped blit into the back buffer. Same seam
 * as the lines, so sprites and vectors stay ordered. */
void r2d_submitImage(R2DImage *img, int srcX, int srcY, int w, int h,
                     int dstX, int dstY, int key);

/* The software backend registers how it rasterizes a submitted image into the
 * back buffer (r2d need not own the page surface). */
void r2d_registerSoftwareImage(void (*image)(R2DImage *img, int srcX, int srcY,
                                             int w, int h, int dstX, int dstY, int key));

/* The recorded overlay primitives (lines, points, images) for the current frame,
 * in submission order, for the GL backend to replay. Count via *count. */
const R2DOverlayPrim *r2d_overlayPrims(int *count);

/* The interleaved x,y vertex pool the R2D_PRIM_POLY prims index into (srcX =
 * first index, srcY = count), for the GL backend to replay filled polygons. */
const short *r2d_overlayPolyVerts(void);

/* Called by the backend after replaying the layer at present: clears it so each
 * frame composes a fresh layer (a frame that submits nothing — e.g. after a view
 * change — then correctly shows none, with no stale carry-over). */
void r2d_vectorMarkPresented(void);

/* Per-image backend texture cache: the GL backend stashes the INDEX8->RGBA
 * texture it built for an image plus the palette generation it was built for, so
 * a static sprite sheet uploads once and re-uploads only when the palette changes.
 * The handle is opaque to r2d (a GLuint on the GL backend); 0 means "none". */
unsigned int r2d_imageCacheTex(R2DImage *img);
int r2d_imageCacheGen(R2DImage *img);
void r2d_imageSetCache(R2DImage *img, unsigned int tex, int gen);

/* Register a hook run when an image is released, so the backend can drop the
 * cached texture it created for it. */
void r2d_registerImageDestroy(void (*hook)(R2DImage *img));

/* Present the virtual overlay page (its w/h are the virtual size) to the window
 * through the active backend. shakeOffset is the explosion screen-shake in
 * virtual pixels (0-3), applied as a horizontal present offset. */
void r2d_present(struct SDL_Surface *page, int shakeOffset);

/* Name of the active 2D backend ("opengl1" or "software"); follows the 3D
 * backend. */
const char *r2d_backendName(void);

/* The software path's SDL_Renderer present lives in gfx_impl.c (it owns the
 * renderer); gfx_impl registers it here at video init so r2d_present can dispatch
 * to it without the renderer leaking into this module. */
void r2d_registerSoftwarePresent(void (*present)(struct SDL_Surface *page, int shakeOffset));

#endif /* R2D_H */
