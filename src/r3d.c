/* Render backend dispatch + registry.
 * Backends are probed in preference order; the first whose init() claims the
 * environment wins. Software is last and always claims. */
#include "r3d.h"
#include "log.h"

#include <SDL3/SDL.h> /* SDL_getenv */

/* Preference order. A new backend (VR, 3DS, PSP, Glide, ...) is an optional CMake
 * module that compiles its source and defines F15_RENDER_<NAME>; it gets one entry
 * here, ahead of software. The software rasterizer is built on every platform, is
 * probed last and always claims, so the list is never empty-handed. */
static const R3DBackend *const g_backendList[] = {
#ifdef F15_RENDER_OPENGL1
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

/* F15_RENDER normalized to a backend name to force; NULL for "auto"/unset. */
static const char *requestedBackend(void) {
    /* SDL_getenv (not the CRT getenv): on Windows SDL keeps its own environment
     * block, and callers/tests set F15_RENDER via SDL_setenv_unsafe. Reading the
     * CRT environment there would miss it and mis-select the backend. */
    const char *e = SDL_getenv("F15_RENDER");
    if (!e || !*e || ieq(e, "auto")) return 0;
    if (ieq(e, "gl") || ieq(e, "opengl") || ieq(e, "opengl1")) return "opengl1";
    if (ieq(e, "sw") || ieq(e, "soft") || ieq(e, "software")) return "software";
    return e; /* unknown name: probe loop will warn and fall back */
}

/* Probe the list and adopt the first backend that claims: only `forced` when
 * `onlyForced`, else every backend except `forced` (already tried). Returns nonzero
 * once g_r3d is set. */
static int probe(const char *forced, int onlyForced) {
    int i;
    for (i = 0; i < R3D_BACKEND_COUNT; i++) {
        int isForced = forced && ieq(g_backendList[i]->name(), forced);
        if (isForced != onlyForced) continue;
        if (g_backendList[i]->init()) {
            g_r3d = g_backendList[i];
            LogInfo(("render backend: %s%s", g_r3d->name(), onlyForced ? " (forced)" : ""));
            return 1;
        }
    }
    return 0;
}

void r3d_init(void) {
    const char *forced = requestedBackend();
    if (forced && probe(forced, 1)) return;
    if (forced)
        LogWarn(("F15_RENDER backend '%s' unavailable; probing in preference order",
                 forced));
    if (probe(forced, 0)) return;
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

void r3d_present(struct SDL_Surface *page, int shakeOffset) {
    if (g_r3d) g_r3d->present(page, shakeOffset);
}
int r3d_frameLive(void) { return g_r3d && g_r3d->frameLive && g_r3d->frameLive(); }
const struct R2DOverlay *r3d_overlay(void) { return g_r3d ? g_r3d->overlay : 0; }
