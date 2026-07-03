#include "r3d.h"
#include "r3d_gl.h"

#if defined(__GNUC__) || defined(__clang__)
void projectSceneObject(char *, int, int, int, int, int, int) __attribute__((weak));
#else
extern void projectSceneObject(char *, int, int, int, int, int, int);
#endif

int r3dgl_wantGL(void) { return 0; }
void r3dgl_setGLAttributes(void) {}
int r3dgl_initContext(struct SDL_Window *win) {
    (void)win;
    return 0;
}
int r3dgl_active(void) { return 0; }
void r3dgl_present(struct SDL_Surface *page, int shakeOffset) {
    (void)page;
    (void)shakeOffset;
}

void r3d_init(void) {}
void r3d_shutdown(void) {}
const char *r3d_backendName(void) { return "stub"; }
const char *r3d_requestedBackend(void) { return 0; }
R3DMesh r3d_registerMesh(R3DMesh raw) { return raw; }
void r3d_releaseMesh(R3DMesh mesh) { (void)mesh; }
void r3d_beginScene(const R3DScene *scene) { (void)scene; }
void r3d_submit(const R3DSubmit *sub) {
    if (sub == nullptr) {
        return;
    }

#if defined(__GNUC__) || defined(__clang__)
    if (projectSceneObject != nullptr) {
        projectSceneObject(reinterpret_cast<char *>(sub->mesh),
                          sub->yaw, sub->pitch, sub->roll,
                          sub->posX, sub->posY, sub->posZ);
    }
#else
    projectSceneObject(reinterpret_cast<char *>(sub->mesh), sub->yaw, sub->pitch, sub->roll,
                      sub->posX, sub->posY, sub->posZ);
#endif
}
void r3d_endScene(void) {}
