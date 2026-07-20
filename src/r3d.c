/* 3D renderer backend dispatch + registry.
 * Backends are probed in preference order; the first whose init() claims the
 * environment wins. Software is last and always claims. */
#include "r3d.h"
#include "log.h"

#include <SDL3/SDL.h> /* SDL_getenv */

/* Preference order. New backends (VR, Metal, exotic ports) register here ahead of
 * software, implement the R3DBackend vtable, and probe the environment in their
 * init(); the first that claims wins. Software is last and always claims, so the
 * list is never empty-handed. init() must be idempotent — it may be probed more
 * than once (forced-but-unavailable falls through to the full probe). */
static const R3DBackend *const g_backendList[] = {
#ifdef ENABLE_OPENGL1
    &r3d_glBackend,
#endif
    &r3d_softwareBackend,
};
#define R3D_BACKEND_COUNT ((int)(sizeof g_backendList / sizeof *g_backendList))

static const R3DBackend *g_r3d = 0;

/* Case-insensitive ASCII equality (avoids pulling strcasecmp's <strings.h>). */
static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z') cb += 'a' - 'A';
        if (ca != cb) return 0;
    }
    return *a == *b;
}

const char *r3d_requestedBackend(void) {
    /* SDL_getenv (not the CRT getenv): on Windows SDL keeps its own environment
     * block, and callers/tests set F15_RENDER via SDL_setenv_unsafe. Reading the
     * CRT environment there would miss it and mis-select the backend. */
    const char *e = SDL_getenv("F15_RENDER");
    if (!e || !*e || ieq(e, "auto")) return 0;
    if (ieq(e, "gl") || ieq(e, "opengl") || ieq(e, "opengl1")) return "opengl1";
    if (ieq(e, "sw") || ieq(e, "soft") || ieq(e, "software")) return "software";
    return e; /* unknown name: probe loop will warn and fall back */
}

/* Probe the list and adopt the first backend that claims, optionally restricted
 * to `forced` (a normalized backend name). Returns nonzero once g_r3d is set. */
static int probe(const char *forced) {
    int i;
    for (i = 0; i < R3D_BACKEND_COUNT; i++) {
        if (forced && !ieq(g_backendList[i]->name(), forced)) continue;
        if (g_backendList[i]->init()) {
            g_r3d = g_backendList[i];
            LogInfo(("3D backend: %s%s", g_r3d->name(), forced ? " (forced)" : ""));
            return 1;
        }
    }
    return 0;
}

void r3d_init(void) {
    const char *forced = r3d_requestedBackend();
    if (forced && probe(forced)) return;
    if (forced)
        LogWarn(("F15_RENDER backend '%s' unavailable; probing in preference order",
                 forced));
    if (probe(0)) return;
    g_r3d = &r3d_softwareBackend; /* unreachable: software always claims */
}

void r3d_shutdown(void) {
    if (g_r3d) g_r3d->shutdown();
    g_r3d = 0;
}

const char *r3d_backendName(void) { return g_r3d ? g_r3d->name() : "none"; }
R3DMesh r3d_registerMesh(R3DMesh raw) { return g_r3d->registerMesh(raw); }
void r3d_releaseMesh(R3DMesh mesh) { g_r3d->releaseMesh(mesh); }
void r3d_beginScene(const R3DScene *scene) { g_r3d->beginScene(scene); }
void r3d_submit(const R3DSubmit *sub) { g_r3d->submit(sub); }
void r3d_submitLine(const R3DLine *line) { g_r3d->submitLine(line); }
void r3d_endScene(void) { g_r3d->endScene(); }
