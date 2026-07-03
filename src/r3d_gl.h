#ifndef R3D_GL_H
#define R3D_GL_H

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

/* Composite the software 2D page over the rendered GL 3D and swap the window.
 * `shakeOffset` is the explosion screen-shake (pixels, 320-space). */
void r3dgl_present(struct SDL_Surface *page, int shakeOffset);

#endif /* R3D_GL_H */
