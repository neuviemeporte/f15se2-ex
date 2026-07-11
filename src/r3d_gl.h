#ifndef R3D_GL_H
#define R3D_GL_H

#include <stddef.h>

/*
 * OpenGL 1.x backend support hooks consumed by the graphics layer (gfx_impl.c).
 *
 * The GL backend (r3d_gl.c) implements the R3DBackend 3D vtable AND owns the GL
 * context + the 2D-overlay composite: the 3D viewport renders through GL, and the
 * existing software 2D page is drawn
 * over it as a single flat textured quad with the show-through key transparent.
 * gfx_impl.c keeps owning the window; it asks here whether to bring GL up instead
 * of an SDL_Renderer, and routes its present through here when GL is active.
 */

struct SDL_Window;
struct SDL_Surface;
typedef struct R2DImage R2DImage;

/* Whether to bring a GL window/context up (GL is the auto preference, or forced
 * via F15_RENDER=gl). Checked by gfx_impl.c before window creation so it can
 * request a GL window + skip the SDL_Renderer; the r3d_init probe then claims GL
 * only if the context actually came up, else falls back to software. */
int r3dgl_wantGL(void);

/* Set the GL framebuffer attributes (depth buffer, double-buffer, and msaaSamples-x
 * multisampling when msaaSamples > 0). Must run before the window is created. */
void r3dgl_setGLAttributes(int msaaSamples);

/* Preferred MSAA sample count for the GL framebuffer (0 = off). gfx_impl.c requests
 * this, and retries with 0 if the GL context won't come up with it. */
int r3dgl_msaaSamples(void);

/* Create the GL context on `win` and make it current. Returns nonzero on success
 * (the R3DBackend init() probe then claims the environment). */
int r3dgl_initContext(struct SDL_Window *win);

/* Nonzero once the GL context is live. */
int r3dgl_active(void);

/* Nonzero when the last present carried a live 3D flight frame. The GL 3D lives in
 * the framebuffer (not the retained page), so a bare re-present of the page
 * (gfx_repaint on a window expose/resize) would blank it; the caller skips the
 * re-present in that case and lets the next flight frame redraw. */
int r3dgl_flightLive(void);

/* Composite the software 2D page over the rendered GL 3D and swap the window.
 * `shakeOffset` is the explosion screen-shake (pixels, 320-space). */
void r3dgl_present(struct SDL_Surface *page, int shakeOffset);

/*
 * Immediate 2D overlay primitives.
 *
 * Each draws one HUD/MFD element straight onto the active GL framebuffer at native
 * window resolution, in true call order — a cached texture bind + quad for images,
 * a handful of vector verts otherwise — mapped through the shared page letterbox.
 * Coordinates are absolute 320-space; `color` is a VGA palette index. Cheap enough
 * to issue every frame; only the image *decode* is cached (per-image texture).
 */
/* Open the immediate 2D overlay: sets the shared virtual->window letterbox +
 * screen-shake context the r3dgl_draw* primitives read. `composePage` nonzero (a
 * pure-2D screen with no 3D pass) also lays the page backdrop down NOW, under the
 * immediate overlay drawn next; the present then skips its own composite. Called
 * from r2d_vectorBeginFrame. */
void r3dgl_beginOverlay(int composePage);

void r3dgl_drawLine(int x1, int y1, int x2, int y2, int color);
void r3dgl_drawPoint(int x, int y, int color);
/* Filled axis-aligned rect, INCLUSIVE 320-space bounds (matches fillSpanRect). */
void r3dgl_drawRect(int x0, int y0, int x1, int y1, int color);
/* Filled convex polygon: `n` (x,y) pairs, scissored to the half-open MFD clip rect. */
void r3dgl_drawPoly(const short *xy, int n, int color,
                    int clipX0, int clipY0, int clipX1, int clipY1);
/* Filled quad from 4 float 320-space corners (interleaved x,y), scissored to the
 * half-open clip rect. Cell-corner convention (no +0.5), matching r3dgl_drawPoint's
 * footprint — used for rotated, sub-grid HUD label texels. */
void r3dgl_drawQuadF(const float *xy, int color, int cx0, int cy0, int cx1, int cy1);
/* Sub-pixel radar/MFD line: fractional endpoints, ends cut by a scissor at the
 * half-open clip rect (cx0,cy0)-(cx1,cy1). */
void r3dgl_drawScopeLine(float x1, float y1, float x2, float y2, int color,
                         int cx0, int cy0, int cx1, int cy1, float widthScale);
/* Sprite/HD image: source sub-rect (srcX,srcY,imgW,imgH) into destination footprint
 * (dstW,dstH) at (dstX,dstY); key<0 opaque, key>=0 transparent on index 0. */
void r3dgl_drawImage(R2DImage *img, int srcX, int srcY, int imgW, int imgH,
                     int dstX, int dstY, int dstW, int dstH, int key);
/* Fractional-destination variant: sub-pixel quad (no whole-pixel snap) for a
 * smoothly-moving sprite such as a radar blip. GL only. */
void r3dgl_drawImageF(R2DImage *img, int srcX, int srcY, int imgW, int imgH,
                      float dstX, float dstY, float dstW, float dstH, int key);
/* Rotated variant: the quad turns by angleRad (clockwise on screen) about the
 * 320-space centre (cx,cy). GL only; used for the HD radar contact icons. */
void r3dgl_drawImageRot(R2DImage *img, int srcX, int srcY, int imgW, int imgH,
                        float cx, float cy, float dstW, float dstH,
                        float angleRad, int key);
/* Whole image scaled to the window height (aspect-preserved) — window-filling
 * widescreen 2D art outside the 320-space box, centred horizontally (the briefing
 * room; sides cropped). GL only. */
void r3dgl_drawImageWindow(R2DImage *img);
/* Window-height scale like r3dgl_drawImageWindow, but the image's LEFT edge sits at
 * 320-space boxLeftX (mapped through the 4:3 overlay letterbox) — the briefing arm
 * cels, positioned in the menu box they point into. GL only. */
void r3dgl_drawImageWindowBoxX(R2DImage *img, float boxLeftX);

#ifdef DEBUG
/* Test-only seam for the GL replacement path. It exercises the same GLB/GLMESH
 * runtime mesh loader used by drawSub(), without requiring a live GL context. */
int r3dgl_testLoadReplacementMesh(const char *containerLegacyName, int shapeId,
                                  int *outPrimitiveCount);
/* Returns non-zero when a legacy .3D3 shape slot decodes to something the
 * renderer would draw. Validation uses this to require replacements for real
 * shapes while still allowing known empty/truncated table slots to be skipped. */
int r3dgl_testLegacyShapeRenderable(const unsigned char *legacyModel,
                                    size_t legacyModelSize);
/* Test-only seam for the replacement comparison path. Copies a caller-provided
 * legacy display-list into the world model buffer, loads the matching modern
 * replacement mesh, then runs the same old-vs-new topology/color comparison
 * that drawSub() uses before drawing a GLB replacement. */
int r3dgl_testCompareReplacementMesh(const char *containerLegacyName, int shapeId,
                                     const unsigned char *legacyModel,
                                     size_t legacyModelSize);
#endif

#endif /* R3D_GL_H */
