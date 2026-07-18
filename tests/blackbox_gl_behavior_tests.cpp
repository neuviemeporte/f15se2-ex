#include "shared/blackbox.h"
#include "shared/blackbox_gl.h"
#include "headless.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <cstdlib>
#include <iostream>

namespace {

bool gEnabled = false;
uint32 gTick = 123;
int gSetupCalls = 0;
int gBeginCalls = 0;
int gEndCalls = 0;
int gVertexCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setupOverlayState() { ++gSetupCalls; }

} // namespace

/* The overlay is immediate-mode geometry. Test-owned GL entry points let the
 * behavior run headlessly while preserving the production vertex generation. */
extern "C" void APIENTRY glVertex2f(GLfloat, GLfloat) { ++gVertexCalls; }
extern "C" void APIENTRY glDisable(GLenum) {}
extern "C" void APIENTRY glColor3ub(GLubyte, GLubyte, GLubyte) {}
extern "C" void APIENTRY glBegin(GLenum) { ++gBeginCalls; }
extern "C" void APIENTRY glEnd(void) { ++gEndCalls; }

extern "C" int blackbox_enabled(void) { return gEnabled; }
extern "C" uint32 blackbox_tick(void) { return gTick; }

int main() {
    test_headless_init();
    require(SDL_Init(SDL_INIT_VIDEO), "SDL initializes its headless video driver");
    SDL_Window *window = SDL_CreateWindow("blackbox overlay", 640, 400, SDL_WINDOW_HIDDEN);
    SDL_Surface *page = SDL_CreateSurface(320, 200, SDL_PIXELFORMAT_INDEX8);
    SDL_Palette *palette = SDL_CreatePalette(256);
    require(window && page && palette, "overlay fixtures allocate successfully");
    SDL_Color white = {255, 255, 255, 255};
    require(SDL_SetPaletteColors(palette, &white, 15, 1),
            "overlay foreground palette entry is configured");

    blackbox_drawGlOverlay(page, 0, window, palette, setupOverlayState);
    require(gSetupCalls == 0, "disabled blackbox does not draw an overlay");
    gEnabled = true;
    blackbox_drawGlOverlay(nullptr, 0, window, palette, setupOverlayState);
    blackbox_drawGlOverlay(page, 0, nullptr, palette, setupOverlayState);
    blackbox_drawGlOverlay(page, 0, window, nullptr, setupOverlayState);
    blackbox_drawGlOverlay(page, 0, window, palette, nullptr);
    require(gSetupCalls == 0, "incomplete overlay inputs are rejected");

    blackbox_drawGlOverlay(page, 2, window, palette, setupOverlayState);
    require(gSetupCalls == 1 && gBeginCalls == 2 && gEndCalls == 2,
            "overlay establishes state and emits background and digit batches");
    require(gVertexCalls > 4, "displayed tick produces background and digit geometry");

    SDL_DestroyPalette(palette);
    SDL_DestroySurface(page);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::cout << "blackbox_gl_behavior_tests passed\n";
    return 0;
}
