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

/* Raw-stick priority bindings; command polling is once per flight step. */
enum RawAction { RAW_CANNON, RAW_MISSILE, RAW_COUNTERMEASURE, RAW_WEAPON,
                 RAW_THRUST_UP, RAW_THRUST_DOWN, RAW_GEAR, RAW_AUTOPILOT,
                 RAW_TARGET, RAW_VIEW,
                 RAW_CHAFF, RAW_FLARE, RAW_SIDEWINDER, RAW_AMRAAM, RAW_MAVERICK,
                 RAW_AFTERBURNER, RAW_FULL_THRUST, RAW_IDLE, RAW_BRAKE,
                 RAW_RADAR, RAW_ZOOM_IN, RAW_ZOOM_OUT, RAW_DIRECTOR, RAW_WAYPOINT,
                 RAW_COCKPIT, RAW_FORWARD, RAW_LEFT_VIEW, RAW_RIGHT_VIEW, RAW_REAR,
                 RAW_FOLLOW, RAW_DYNAMIC, RAW_SIDE_VIEW, RAW_MISSILE_VIEW,
                 RAW_EXTERNAL_TARGET, RAW_TARGET_VIEW, RAW_EJECT, RAW_PAUSE,
                 RAW_ACCEL, RAW_SOUND, RAW_DETAIL, RAW_SENSITIVITY, RAW_NIGHT,
                 RAW_TRAINING, RAW_END_MISSION, RAW_BOSS, RAW_CALIBRATE,
                 RAW_MEMORY, RAW_FRAME_TIME, RAW_REARM, RAW_MAP_NORTH,
                 RAW_MAP_SOUTH, RAW_MAP_WEST, RAW_MAP_EAST,
                 RAW_PITCH_DOWN, RAW_PITCH_UP, RAW_ROLL_LEFT, RAW_ROLL_RIGHT,
                 RAW_KP_UP, RAW_KP_DOWN, RAW_KP_LEFT, RAW_KP_RIGHT,
                 RAW_KP_UP_LEFT, RAW_KP_UP_RIGHT, RAW_KP_DOWN_LEFT, RAW_KP_DOWN_RIGHT,
                 RAW_ACTION_COUNT };
/* Reset the active raw device, without loading its saved profile or overrides. */
void joy_resetRawMapping(void);
/* Held directional buttons supplement the physical stick axes. */
bool joy_actionHeld(RawAction action);
/* Setup accesses only the active raw device, never a mapped gamepad. */
int joy_rawButtonCount(void);
int joy_rawPressedButton(void);
/* Calibrated primary axes for setup navigation; zero if unavailable. */
Sint16 joy_rawMenuAxis(int axis);
int joy_rawBinding(RawAction action);
void joy_bindRawButton(RawAction action, int button);
void joy_showSetup(void);
/* Persist the current raw device; false leaves the previous saved file intact. */
bool joy_saveRawMapping(void);
SDL_JoystickID joy_rawDeviceId(void);
bool joy_rawActive(void);
void joy_resetFlightInput(void);
Uint16 joy_flightCommand(int selectedWeapon, int viewMode);
/* Changed thrust percentage (0..100), or -1 when no lever update is pending. */
int joy_throttleChange(void);

#endif /* JOYSTICK_H */
