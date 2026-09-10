#ifndef F15_CONTROLS_MAPPING_H
#define F15_CONTROLS_MAPPING_H

#include "controls.h"

/* The storage boundary contains values only, not SDL events or game state.
 * Replacement validates the entire profile before changing any live binding. */
struct ControlBinding {
    SDL_Scancode key;
    SDL_Keymod mod;
};
ControlBinding controls_keyboardBinding(RawAction action);
bool controls_replaceKeyboard(const ControlBinding *candidate);

#endif
