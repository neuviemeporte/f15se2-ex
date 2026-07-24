#ifndef F15_SE2_BLACKBOX_GL_H
#define F15_SE2_BLACKBOX_GL_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*BlackboxGlSetupFn)(void);

/* Draws the timer after the flight compositor, where the indexed-page overlay
 * would otherwise be covered by native OpenGL geometry. */
void blackbox_drawGlOverlay(SDL_Surface *page, int shakeOffset, SDL_Window *window,
                            SDL_Palette *palette,
                            BlackboxGlSetupFn setupOverlayState);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_GL_H */
