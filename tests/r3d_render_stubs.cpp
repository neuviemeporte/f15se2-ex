#include "gfx.h"
#include "inttype.h"
#include "r2d.h"
#include "r3d.h"
#include "r3d_gl.h"

#if defined(__GNUC__) || defined(__clang__)
void projectSceneObject(char *, int, int, int, int, int, int) __attribute__((weak));
#define TEST_WEAK_STUB __attribute__((weak))
#else
extern void projectSceneObject(char *, int, int, int, int, int, int);
#define TEST_WEAK_STUB
#endif

// Test-side page descriptor pointers used by behavior tests that inspect page state.
static int16 g_pageBackStorage[11] = {0, 2, 2, 0, 0, 0, 1, 0, 0x60, 0, 0x13F};
static int16 g_pageOffscreenStorage[11] = {0, 2, 2, 0, 0, 0, 1, 0, 0x60, 0, 0x13F};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) int16 *g_pageBack = g_pageBackStorage;
__attribute__((weak)) int16 *g_pageOffscreen = g_pageOffscreenStorage;
#else
int16 *g_pageBack = g_pageBackStorage;
int16 *g_pageOffscreen = g_pageOffscreenStorage;
#endif

int r3dgl_wantGL(void) { return 0; }
void r3dgl_setGLAttributes(int msaaSamples) { (void)msaaSamples; }
int r3dgl_msaaSamples(void) { return 0; }
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
void r2d_submitLine(int x1, int y1, int x2, int y2, int color) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)color;
}
void r2d_submitPoint(int x, int y, int color) {
    (void)x;
    (void)y;
    (void)color;
}
void r2d_setForceRaster(int on) {
    (void)on;
}
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
void r2d_blit(struct SDL_Surface *src, int srcX, int srcY,
              struct SDL_Surface *dst, int dstX, int dstY,
              int w, int h, int key) {
    (void)src;
    (void)srcX;
    (void)srcY;
    (void)dst;
    (void)dstX;
    (void)dstY;
    (void)w;
    (void)h;
    (void)key;
}
R2DImage *r2d_registerImage(int w, int h) {
    (void)w;
    (void)h;
    return nullptr;
}
R2DImage *r2d_captureImage(struct SDL_Surface *src, int x, int y, int w, int h) {
    (void)src;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return nullptr;
}
SDL_Surface *r2d_imageSurface(R2DImage *img) {
    (void)img;
    return nullptr;
}
void r2d_releaseImage(R2DImage *img) { (void)img; }
void r2d_registerSoftwarePresent(void (*present)(SDL_Surface *, int)) { (void)present; }
void r2d_drawImage(R2DImage *img, int srcX, int srcY, int w, int h,
                   SDL_Surface *dst, int dstX, int dstY, int key) {
    (void)img; (void)srcX; (void)srcY; (void)w; (void)h; (void)dst;
    (void)dstX; (void)dstY; (void)key;
}
void r2d_computeMapping(int virtW, int virtH, int winW, int winH, R2DMapping *out) {
    (void)virtW;
    (void)virtH;
    if (out != nullptr) {
        out->virtW = 0;
        out->virtH = 0;
        out->winW = winW;
        out->winH = winH;
        out->scale = 1.0f;
        out->offX = 0;
        out->offY = 0;
    }
}
void r2d_registerSoftwarePrims(void (*line)(int, int, int, int, int), void (*point)(int, int, int)) {
    (void)line;
    (void)point;
}
void r2d_present(SDL_Surface *page, int shakeOffset) {
    (void)page;
    (void)shakeOffset;
}
void r2d_vectorMarkPresented(void) {}
void r2d_vectorBeginFrame(void) {}
int r2d_vectorActive(void) { return 0; }

const R2DVectorPrim *r2d_vectorPrims(int *count) {
    if (count != nullptr) *count = 0;
    return nullptr;
}
TEST_WEAK_STUB void gfx_captureToImage(struct R2DImage *img, int srcPage, int srcX, int srcY,
                       int dstX, int dstY, int width, int height) {
    (void)img;
    (void)srcPage;
    (void)srcX;
    (void)srcY;
    (void)dstX;
    (void)dstY;
    (void)width;
    (void)height;
}
TEST_WEAK_STUB void gfx_restoreFromImage(struct R2DImage *img, int dstPage, int srcX, int srcY,
                         int dstX, int dstY, int width, int height) {
    (void)img;
    (void)dstPage;
    (void)srcX;
    (void)srcY;
    (void)dstX;
    (void)dstY;
    (void)width;
    (void)height;
}
TEST_WEAK_STUB R2DImage *gfx_allocImage(int w, int h) {
    (void)w;
    (void)h;
    return nullptr;
}
