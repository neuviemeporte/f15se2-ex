#ifndef R3D_H
#define R3D_H

/*
 * 3D mesh-submission renderer interface.
 *
 * The whole in-flight 3D scene funnels through one submission API so the
 * software rasterizer is *one* backend and a GPU backend (OpenGL 1.x -> VR ->
 * exotic ports) can be dropped in alongside it. Backends are registered in
 * preference order; each init() probes the environment and either claims it or
 * declines, and the software backend always claims last.
 *
 * The software backend is a thin pass-through to the eg3drast/eg3dmap functions,
 * pixel-identical to the standalone rasterizer. The GPU backend consumes the
 * fuller, backend-agnostic scene description (decomposed view matrix, projection
 * gates, decoded meshes).
 */

#include "inttype.h"

/* Opaque mesh handle: identity wrapper over the raw display-list model pointer
 * (g_world3dData + offset). r3dmesh decodes it into a Mesh for upload. */
typedef void *R3DMesh;

/* Per-scene state — mirrors setup3DTransform's inputs: the
 * viewport descriptor (g_viewParams / g_targetViewParams), the view orientation
 * angles + position, and the renderScene flag (1 = main scene with the
 * background sphere + shared-vertex precompute, 0 = MFD/target sub-view). */
typedef struct {
    const int16 *viewport;
    int angleX, angleY, angleZ;
    int posX, posY, posZ;
    int renderScene;
} R3DScene;

/* One object submitted to the current scene: a mesh plus its orientation and
 * position. Shade / render-mode / LOD stay where the existing code computes them
 * (object globals + the display-list stream); they move into this struct when the
 * backend stops sharing those globals.
 *
 * Aircraft shadow: an aircraft near the ground submits its own model with
 * `shadow` set and its true attitude (yaw/pitch/roll). The GPU backend flattens
 * that oriented model onto the ground plane and draws it translucent, so a banking
 * plane casts a foreshortened outline of its own shape. The software backend draws
 * the same model level (it ignores the shadow attitude) in flat black — its low-end
 * baseline: correct silhouette, no ground projection. `shadow` is 0 for every
 * normal object. */
typedef struct {
    R3DMesh mesh;
    int yaw, pitch, roll;
    int posX, posY, posZ;
    int shadow;
} R3DSubmit;

/* One world-space line segment submitted to the current scene (cannon tracers,
 * explosion sparks) — real 3D geometry, drawn like a model "line" primitive:
 * depth-sorted/occluded (software) and z-tested + fogged (GL). Endpoints are in
 * the scene's integer camera space — the (screen-X axis, screen-Y axis, depth)
 * triple transformAndCullObject produces (g_camBaseX / g_camTransX / g_camTransY),
 * so a line consumes the exact coords models do. `color` is a final VGA palette
 * index (effect colours are palette indices, not colorLut bytes). */
typedef struct {
    long baseXA, camXA, camYA; /* endpoint A: screen-X axis, screen-Y axis, depth */
    long baseXB, camXB, camYB; /* endpoint B */
    int color;
} R3DLine;

struct SDL_Surface;
struct R2DOverlay;

/* A render backend: window bring-up, the in-flight 3D, the page present and
 * (optionally) a native-resolution 2D overlay. Selected once at video init by
 * probe order. */
typedef struct R3DBackend {
    const char *(*name)(void);
    /* Probe: create this backend's window (gfx_createWindow) and whatever context it
     * presents through, and claim the environment (nonzero). A backend that can't
     * run here destroys anything it created and returns 0. */
    int (*init)(void);
    void (*shutdown)(void);

    /* Mesh registry: decode/upload once, reference by handle. Software returns
     * the raw pointer unchanged; a GPU backend builds a VBO/IBO. */
    R3DMesh (*registerMesh)(R3DMesh raw);
    void (*releaseMesh)(R3DMesh mesh);

    /* Frame. submit() may be called in any order; endScene() orders + flushes. */
    void (*beginScene)(const R3DScene *scene);
    void (*submit)(const R3DSubmit *sub);
    void (*submitLine)(const R3DLine *line);
    void (*endScene)(void);

    /* Put the 320x200 page (composited over the 3D when the backend draws 3D off
     * the page) on screen. shakeOffset is the explosion screen-shake, 320-space. */
    void (*present)(struct SDL_Surface *page, int shakeOffset);
    /* Optional: nonzero when the last present carried 3D the page doesn't hold, so a
     * bare page re-present (window expose) would blank it. NULL = never. */
    int (*frameLive)(void);
    /* Optional native-resolution 2D overlay (r2d.h); NULL = every 2D submission
     * bakes into the page. */
    const struct R2DOverlay *overlay;
} R3DBackend;

/* Probe the backend list and bring up the winner's window. Backend selection
 * honours the F15_RENDER env var ("opengl1"/"gl", "software"/"sw", or a future
 * backend's name; unset/"auto" = probe in preference order). Called from
 * gfx_videoInit. */
void r3d_init(void);
void r3d_shutdown(void);
const char *r3d_backendName(void);
R3DMesh r3d_registerMesh(R3DMesh raw);
void r3d_releaseMesh(R3DMesh mesh);
void r3d_beginScene(const R3DScene *scene);
void r3d_submit(const R3DSubmit *sub);
void r3d_submitLine(const R3DLine *line);
void r3d_endScene(void);
void r3d_present(struct SDL_Surface *page, int shakeOffset);
int r3d_frameLive(void);
const struct R2DOverlay *r3d_overlay(void);

/* Backends; the preference-ordered list lives in r3d.c. */
extern const R3DBackend r3d_glBackend;
extern const R3DBackend r3d_softwareBackend;

#endif /* R3D_H */
