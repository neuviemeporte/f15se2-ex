/* 3D renderer backend dispatch + registry (see docs/render-3d-backend.md).
 * Backends are probed in preference order; the first whose init() claims the
 * environment wins. Software is last and always claims. */
#include "r3d.h"
#include "log.h"

static const R3DBackend *const g_backendList[] = {
    /* GPU backends register ahead of software; each init() probes and claims the
     * environment or declines (GL claims only when F15_RENDER=gl brought a context
     * up). Software is last and always claims. */
    &r3d_glBackend,
    &r3d_softwareBackend,
};

static const R3DBackend *g_r3d = 0;

void r3d_init(void) {
    int i;
    for (i = 0; i < (int)(sizeof g_backendList / sizeof *g_backendList); i++) {
        if (g_backendList[i]->init()) {
            g_r3d = g_backendList[i];
            LogInfo(("3D backend: %s", g_r3d->name()));
            return;
        }
    }
    /* Unreachable: software always claims. */
    g_r3d = &r3d_softwareBackend;
}

const char *r3d_backendName(void) { return g_r3d ? g_r3d->name() : "none"; }
R3DMesh r3d_registerMesh(R3DMesh raw) { return g_r3d->registerMesh(raw); }
void r3d_releaseMesh(R3DMesh mesh) { g_r3d->releaseMesh(mesh); }
void r3d_beginScene(const R3DScene *scene) { g_r3d->beginScene(scene); }
void r3d_submit(const R3DSubmit *sub) { g_r3d->submit(sub); }
void r3d_endScene(void) { g_r3d->endScene(); }
