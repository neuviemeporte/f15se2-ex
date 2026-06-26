/*
 * joystick.h - SDL3 gamepad / joystick lifecycle (see joystick.c).
 *
 * The game-facing readers (readCalibratedJoystick, pollJoystick,
 * misc_readJoystick) are declared with the rest of the game API in
 * egcode.h / stcode.h / slot.h; these three are the native plumbing only.
 */
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <SDL3/SDL.h>

/* Open SDL's gamepad subsystem and bind the first connected device. */
void joy_init(void);
void joy_shutdown(void);

/* Process device hotplug; called for every event by both SDL event pumps. */
void joy_handleEvent(const SDL_Event *ev);

/* True when a mapped gamepad (not a raw joystick) is the active device, so the
 * named-button flight bindings in eginput.c have known button meanings. */
bool joy_isGamepad(void);

/* True when any device (gamepad or raw joystick) is currently bound. */
bool joy_connected(void);

/* Current state of a gamepad button (false unless a gamepad is active). */
bool joy_button(SDL_GamepadButton b);

/* Current gamepad axis value, -32768..32767 (0 unless a gamepad is active). */
Sint16 joy_axisRaw(SDL_GamepadAxis a);

#endif /* JOYSTICK_H */
