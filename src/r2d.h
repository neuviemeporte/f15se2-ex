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

/* Adopt an already-decoded SDL surface as an image (takes ownership; freed by
 * r2d_releaseImage). Unlike r2d_registerImage/r2d_captureImage — which make INDEX8
 * paletted images — the surface may be any format, e.g. an RGBA HD sprite loaded
 * from PNG; the GL backend uploads a non-INDEX8 surface directly (no palette). NULL
 * on NULL input. */
R2DImage *r2d_imageFromSurface(struct SDL_Surface *surf);

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
 * Native 2D overlay layer.
 *
 * The HUD/MFD primitives (lines, pitch-ladder, symbology) and sprites are
 * *submitted* to the renderer rather than rasterized into the 320x200 page. The
 * software backend realizes a submission by rasterizing into the page (the
 * low-end/DOS path, via the registered callbacks below). The GL backend draws each
 * submission IMMEDIATELY, directly onto the active framebuffer at its call site
 * (r3dgl_draw*), at **native window resolution** — a crisp vector HUD and sharp
 * sprites in true call order (a sprite drawn after a line lands over it), no
 * recording and no present-time replay. Same call sites, the realization is the
 * backend's.
 */

/* Marks the start of a vector 2D overlay: between this and the present, 2D
 * submissions draw immediately at native resolution on GL (else bake into the
 * page). A flight frame calls it from the 3D backend once the main 3D view begins,
 * passing composePageFirst=0 (its 3D pass composites the page backdrop mid-frame).
 * A pure-2D screen (debrief) passes composePageFirst=1 so the page backdrop is laid
 * down under the immediate overlay now, rather than over it at present. */
void r2d_vectorBeginFrame(int composePageFirst);

/* Whether 2D primitive submissions draw immediately to the framebuffer this frame
 * (set between r2d_vectorBeginFrame and the present — i.e. a GL flight/vector frame)
 * rather than into the retained 320x200 page. The gfx submission points branch on
 * this: true → draw immediately through the renderer (GL, redrawn every frame);
 * false → write the page (software, retained across frames; also pure-2D screens
 * with no 3D pass). This IS the "framebuffer retained?" question, inverted. */
int r2d_vectorActive(void);

/* Whether the active backend can replay a submitted overlay at native resolution
 * (the GL backend) as opposed to rasterizing it into the page (software). Distinct
 * from r2d_vectorActive: this is the backend capability, testable BEFORE a vector
 * frame is opened. A pure-2D screen gates r2d_vectorBeginFrame() on this (on
 * software there is no native replay, so entering a vector frame would record
 * primitives that never draw). */
int r2d_hasNativeOverlay(void);

/* Submit a clipped 2D line / point in absolute 320-space with a palette colour
 * index. Draws immediately on a GL vector frame, else hands off to the registered
 * software rasterizer. */
void r2d_submitLine(int x1, int y1, int x2, int y2, int color);
void r2d_submitPoint(int x, int y, int color);

/* Submit a filled axis-aligned rect (INCLUSIVE 320-space bounds) for the per-frame
 * MFD/HUD backdrops that must draw in true call order rather than bake into the
 * page. GL-only (immediate); a no-op off a vector frame, where the caller bakes it
 * with fillSpanRect instead (see fillSpanRectImmediate). */
void r2d_submitRect(int x0, int y0, int x1, int y1, int color);

/* Submit a sub-pixel radar/MFD line: fractional 320-space endpoints, scissored
 * to (clipX0,clipY0)-(clipX1,clipY1) (absolute 320-space, half-open) at replay.
 * On a GL vector frame records for a native-res replay whose ends are cut by a GL
 * scissor at the true MFD edge (not a geometry clip snapped to whole pixels, which
 * wobbles and leaves gaps); off a vector frame the software backend rounds to int,
 * clips the segment to the rect and rasterizes it into the page (unchanged look).
 * widthScale multiplies the base line width (0.5 = thin MFD/radar, 1.0 = full-weight
 * HUD); ignored on the software backend, which is always one page pixel. */
void r2d_submitScopeLine(float x1, float y1, float x2, float y2, int color,
                         int clipX0, int clipY0, int clipX1, int clipY1, float widthScale);

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

/* Submit an image drawn into a destination footprint (dstW,dstH in 320-space) that
 * may differ from the source sub-rect size — i.e. a scaled sprite. Used for HD art:
 * a large RGBA source drawn into the small 320-space footprint of the legacy sprite
 * it replaces, sharp at native window resolution. GL-only: records into the ordered
 * overlay stream (a no-op when vector recording is inactive, since the software
 * backend has no scaled-blit path and HD art is gated to GPU backends). */
void r2d_submitImageScaled(R2DImage *img, int srcX, int srcY, int srcW, int srcH,
                           int dstX, int dstY, int dstW, int dstH, int key);

/* Fractional-destination scaled image: like r2d_submitImageScaled but the 320-space
 * destination is float, so a sprite that moves sub-pixel per frame (a radar blip
 * gliding with the scope grid) slides smoothly instead of snapping to whole pixels.
 * GL-only; returns 1 if it drew (active vector frame) and 0 otherwise, so callers
 * can fall back to the whole-pixel software path. */
int r2d_submitImageF(R2DImage *img, int srcX, int srcY, int srcW, int srcH,
                     float dstX, float dstY, float dstW, float dstH, int key);

/* Rotated sprite: the base frame turns by angleRad (clockwise on screen) about the
 * 320-space centre (cx,cy), so the HD radar can spin one icon smoothly instead of
 * picking a hand-drawn rotation frame. GL-only; returns 1 if it drew, 0 otherwise
 * so callers fall back to the pre-rotated software atlas frame. */
int r2d_submitImageRot(R2DImage *img, int srcX, int srcY, int srcW, int srcH,
                       float cx, float cy, float dstW, float dstH,
                       float angleRad, int key);

/* The software backend registers how it rasterizes a submitted image into the
 * back buffer (r2d need not own the page surface). */
void r2d_registerSoftwareImage(void (*image)(R2DImage *img, int srcX, int srcY,
                                             int w, int h, int dstX, int dstY, int key));

/* Called by the backend at present to close the vector frame: subsequent 2D
 * submissions (until the next r2d_vectorBeginFrame) bake into the page again. */
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
