/* Software render backend — the fallback every platform gets, and the low-end /
 * DOS path. Window bring-up and the page present live in gfx_impl.c.
 *
 * beginScene/submit/endScene wrap the eg3drast/eg3dmap entry points, so the scene
 * is pixel-identical while the rest of the game routes through the backend seam
 * rather than calling them directly. The painter's depth sort, shared-vertex
 * precompute and raw-display-list rasterizer are software-backend internals
 * (integer Q15, no FPU) for the 386+ target. */
#include "r3d.h"
#include "eg3dmap.h"
#include "egcode.h"
#include "egtypes.h"
#include "gfx_impl.h"

void far r3d_submitLineFar(long baseXA, long camXA, long camYA,
                           long baseXB, long camXB, long camYB, int color);

static const char *sw_name(void) { return "software"; }

/* The software rasterizer runs anywhere SDL can open a window; it always claims. */
static int sw_init(void) { return gfx_initSoftwareVideo(); }
static void sw_shutdown(void) { gfx_shutdownSoftwareVideo(); }

/* The raw display-list pointer is its own handle; nothing to upload or free. */
static R3DMesh sw_registerMesh(R3DMesh raw) { return raw; }
static void sw_releaseMesh(R3DMesh mesh) { (void)mesh; }

static void sw_beginScene(const R3DScene *s) {
    setup3DTransform(s->viewport, s->angleX, s->angleY, s->angleZ,
                     s->posX, s->posY, s->posZ, s->renderScene);
}

static void sw_submit(const R3DSubmit *o) {
    if (o->shadow) {
        /* Draw the aircraft's own model level and flat black — the low-end shadow
         * baseline (no ground projection; the attitude carried in the submit is used
         * only by the GPU backend's flattened shadow). */
        projectSceneShadow((char far *)o->mesh, o->yaw, o->posX, o->posY, o->posZ);
        return;
    }
    projectSceneObject((char far *)o->mesh, o->yaw, o->pitch, o->roll,
                       o->posX, o->posY, o->posZ);
}

static void sw_submitLine(const R3DLine *l) {
    r3d_submitLineFar(l->baseXA, l->camXA, l->camYA,
                      l->baseXB, l->camXB, l->camYB, l->color);
}

static void sw_endScene(void) {
    rasterize3DWorld();
}

const R3DBackend r3d_softwareBackend = {
    sw_name,
    sw_init,
    sw_shutdown,
    sw_registerMesh,
    sw_releaseMesh,
    sw_beginScene,
    sw_submit,
    sw_submitLine,
    sw_endScene,
    gfx_presentSoftware,
    NULL, /* the page holds the whole frame */
    NULL, /* no native overlay: 2D bakes into the page */
};
