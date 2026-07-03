// Headless test support for LINK_CORE behavior tests.
//
// Tests that link the whole f15se2_core library exercise the *real* game code.
// Call test_headless_init() once at the top of main(), before touching any
// SDL-backed subsystem, so the test runs without a display, GPU, or audio
// device (CI-safe): SDL uses its dummy video/audio drivers and the game's 3D
// path selects the software backend (no GL context needed).
#ifndef F15_TEST_HEADLESS_H
#define F15_TEST_HEADLESS_H

#include <SDL3/SDL.h>

static inline void test_headless_init(void) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    // The game reads F15_RENDER to pick a 3D backend; force the software path
    // so no OpenGL context is created. SDL_setenv_unsafe is portable (incl. MSVC).
    SDL_setenv_unsafe("F15_RENDER", "software", 1);
}

#endif // F15_TEST_HEADLESS_H
