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

/* A renderer backend. Selected once at startup by probe order. */
typedef struct R3DBackend {
    const char *(*name)(void);
    int (*init)(void); /* probe; nonzero = claims this environment */
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
} R3DBackend;

/* Active-backend dispatch. */
void r3d_init(void);
void r3d_shutdown(void);
const char *r3d_backendName(void);

/* Backend selection from the F15_RENDER env var, normalized to a backend name to
 * force ("opengl1", "software", or a future backend's name). Returns NULL for
 * "auto"/unset, meaning probe the list in preference order. A forced backend that
 * is unavailable (declines or unknown name) falls back to the probe. Shared by
 * r3d_init() and the GL window decision (r3dgl_wantGL) so the two agree. */
const char *r3d_requestedBackend(void);
R3DMesh r3d_registerMesh(R3DMesh raw);
void r3d_releaseMesh(R3DMesh mesh);
void r3d_beginScene(const R3DScene *scene);
void r3d_submit(const R3DSubmit *sub);
void r3d_submitLine(const R3DLine *line);
void r3d_endScene(void);

/* Registered backends (preference order is defined in r3d.c). */
extern const R3DBackend r3d_glBackend;
extern const R3DBackend r3d_softwareBackend;

#endif /* R3D_H */
