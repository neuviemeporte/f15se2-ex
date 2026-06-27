#ifndef R3D_H
#define R3D_H

/*
 * 3D mesh-submission renderer interface (see docs/render-3d-backend.md).
 *
 * The whole in-flight 3D scene funnels through one submission API so the
 * software rasterizer becomes *one* backend and a GPU backend (OpenGL 1.x ->
 * VR -> exotic ports) can be dropped in alongside it. Backends are registered
 * in preference order; each init() probes the environment and either claims it
 * or declines, and the software backend always claims last.
 *
 * Step 1 (this file's current state) is the "confine it" step: the software
 * backend is a thin pass-through to the existing eg3drast/eg3dmap functions and
 * the scene is pixel-identical. The richer, fully backend-agnostic scene
 * description (decomposed view matrix, projection gates, decoded meshes) lands
 * with the GPU backend; see the doc for the target shape of R3DScene/R3DSubmit.
 */

#include "inttype.h"

/* Opaque mesh handle. Step 1: identity wrapper over the raw display-list model
 * pointer (g_world3dData + offset). Step 2 decodes it into a Mesh for upload. */
typedef void *R3DMesh;

/* Per-scene state. Step 1 mirrors setup3DTransform's inputs verbatim: the
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
 * position. Step 1 leaves shade / render-mode / LOD where the existing code
 * already computes them (object globals + the display-list stream); they move
 * into this struct when the backend stops sharing those globals. */
typedef struct {
    R3DMesh mesh;
    int yaw, pitch, roll;
    int posX, posY, posZ;
} R3DSubmit;

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
    void (*endScene)(void);
} R3DBackend;

/* Active-backend dispatch. */
void r3d_init(void);
const char *r3d_backendName(void);
R3DMesh r3d_registerMesh(R3DMesh raw);
void r3d_releaseMesh(R3DMesh mesh);
void r3d_beginScene(const R3DScene *scene);
void r3d_submit(const R3DSubmit *sub);
void r3d_endScene(void);

/* Registered backends (preference order is defined in r3d.c). */
extern const R3DBackend r3d_glBackend;
extern const R3DBackend r3d_softwareBackend;

#endif /* R3D_H */
