#ifndef R3D_GL_H
#define R3D_GL_H

/*
 * OpenGL 1.x backend support hooks consumed by the graphics layer (gfx_impl.c).
 *
 * The GL backend (r3d_gl.c) implements the R3DBackend 3D vtable AND owns the GL
 * context + the interim 2D-overlay composite (docs/render-3d-backend.md, Step 3):
 * the 3D viewport renders through GL, and the existing software 2D page is drawn
 * over it as a single flat textured quad with the show-through key transparent.
 * gfx_impl.c keeps owning the window; it asks here whether to bring GL up instead
 * of an SDL_Renderer, and routes its present through here when GL is active.
 */

struct SDL_Window;
struct SDL_Surface;

/* Read the manual backend selection (env F15_RENDER=gl). Checked by gfx_impl.c
 * before window creation so it can request a GL window + skip the SDL_Renderer. */
int r3dgl_wantGL(void);

/* Set the GL framebuffer attributes (depth buffer, double-buffer). Must run
 * before the window is created. */
void r3dgl_setGLAttributes(void);

/* Create the GL context on `win` and make it current. Returns nonzero on success
 * (the R3DBackend init() probe then claims the environment). */
int r3dgl_initContext(struct SDL_Window *win);

/* Nonzero once the GL context is live. */
int r3dgl_active(void);

/* Composite the software 2D page over the rendered GL 3D and swap the window.
 * `shakeOffset` is the explosion screen-shake (pixels, 320-space). */
void r3dgl_present(struct SDL_Surface *page, int shakeOffset);

#endif /* R3D_GL_H */
