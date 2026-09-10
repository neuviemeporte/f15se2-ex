#ifndef F15_CONTROLS_H
#define F15_CONTROLS_H

#include "joystick.h"
#include <string>

/* One catalog is shared by setup, persistence and both input devices. Menu
 * navigation, text entry and the global fullscreen shortcut are not remapped. */
struct ControlAction {
    const char *name;
    const char *label;
    SDL_Scancode key;
    SDL_Keymod modifiers;
    Uint16 command;
    int x, y;
};
const ControlAction &controls_action(RawAction action);
/* Supply device-local state to preserve independent countermeasure sequences. */
Uint16 controls_command(RawAction action, int weapon, int view, bool *nextFlare = nullptr);
void controls_resetKeyboard(void);
bool controls_bindKey(RawAction action, SDL_Scancode key, SDL_Keymod modifiers);
std::string controls_keyName(RawAction action);
Uint16 controls_translateKey(SDL_Scancode key, SDL_Keymod modifiers, Uint16 fallback);
void controls_applyAxes(Uint8 *x, Uint8 *y, bool joystick);
std::string controls_keyboardPath(void);
bool controls_loadKeyboard(const std::string &path);
bool controls_saveKeyboard(const std::string &path);
void controls_beginCapture(RawAction action);
bool controls_capturing(void);
bool controls_captureKey(const SDL_KeyboardEvent &event);

#endif
