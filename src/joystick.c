/*
 * joystick.c - SDL3 gamepad / joystick input.
 *
 * The original game read a 2-axis, 2-button PC joystick straight off the game
 * port (0x201): an RC-discharge timing loop in egseg2.asm (routine_246) counted
 * the decay on each axis line and routine_247 auto-calibrated those raw counts
 * into the 0..255 byte pair the game uses (joyAxes[0] = X/roll, joyAxes[1] =
 * Y/pitch, 0x80 = centre). The two fire buttons were read via the MISC overlay
 * slot misc_readJoystick(button). None of that survives the move off DOS, so the
 * whole subsystem was stubbed.
 *
 * This backs the same game-facing API with SDL instead, so the existing wiring
 * (flight stick in egflight.c, menu navigation in stmissn.c/enbrief.c, the MFD
 * stick indicator in egtacmap.c) works unchanged:
 *
 *   - readCalibratedJoystick / pollJoystick fill joyAxes[0]/[1] from the stick.
 *   - misc_readJoystick(n) reports fire button n (0 = guns, 1 = missiles).
 *
 * Device selection follows SDL's layering: prefer the Gamepad API (a mapped
 * controller, so face buttons and triggers have known meanings), and fall back
 * to the raw Joystick API for a stick SDL has no gamepad mapping for. SDL
 * pre-calibrates both, so the original's runtime calibration (routine_247,
 * initJoystickCalibration) and its cross-EXE save/restore (copy/restoreJoystick
 * Data) have nothing to do here.
 *
 * commData->setupUseJoy is the game's master "use the stick" flag; we raise it
 * while a device is connected and clear it on the last unplug, which is what
 * routes the game through the joystick paths above.
 */
#include "joystick.h"
#include "input.h"
#include "inttype.h"
#include "comm.h"
#include "log.h"
#include "pointers.h"
#include <dos.h>

/* joyAxes[0] = X (roll), joyAxes[1] = Y (pitch); 0x80 = centred. Defined in
 * stdata.c, shared by all three former programs. */
extern uint8 joyAxes[];

/* At most one device is active at a time. A mapped controller opens as a
 * gamepad; anything else opens as a raw joystick (g_joy). */
static SDL_Gamepad *g_pad = NULL;
static SDL_Joystick *g_joy = NULL;
static SDL_JoystickID g_devId = 0;

/* Idle slop on a raw stick can drift the menu cursor (the menus treat
 * joyAxes outside 78..178 as a held direction), so snap a small band around
 * centre back to exact centre. ~24% of full scale stays well inside that. */
#define JOY_AXIS_DEADZONE 8000
/* A trigger past this (of SDL's 0..32767) counts as a button press. */
#define JOY_TRIGGER_THRESHOLD 16000

/* Map an SDL axis value (-32768..32767) to the game's 0..255 byte, 0x80 centre. */
static uint8 axisByte(int16 raw) {
    if (raw > -JOY_AXIS_DEADZONE && raw < JOY_AXIS_DEADZONE) return 0x80;
    int v = 0x80 + (raw * 127) / 32768;
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8)v;
}

static void joy_close(void) {
    if (g_pad) SDL_CloseGamepad(g_pad);
    if (g_joy) SDL_CloseJoystick(g_joy);
    g_pad = NULL;
    g_joy = NULL;
    g_devId = 0;
    if (commData) commData->setupUseJoy = 0;
}

/* Open id as a gamepad if SDL has a mapping for it, otherwise as a raw
 * joystick. No-op when a device is already active. */
static void joy_open(SDL_JoystickID id) {
    if (g_devId) return;
    if (SDL_IsGamepad(id)) {
        g_pad = SDL_OpenGamepad(id);
        if (g_pad) {
            g_devId = id;
            LogInfo(("joystick: using gamepad '%s'", SDL_GetGamepadName(g_pad)));
        }
    } else {
        g_joy = SDL_OpenJoystick(id);
        if (g_joy) {
            g_devId = id;
            LogInfo(("joystick: using joystick '%s'", SDL_GetJoystickName(g_joy)));
        }
    }
    if (g_devId && commData) commData->setupUseJoy = 1;
}

/* Pick a device when none is active: a mapped gamepad first, then any other
 * stick. Used at startup and after an unplug. */
static void joy_rescan(void) {
    int count;
    if (g_devId) return;

    SDL_JoystickID *pads = SDL_GetGamepads(&count);
    if (pads) {
        if (count > 0) joy_open(pads[0]);
        SDL_free(pads);
    }
    if (g_devId) return;

    SDL_JoystickID *sticks = SDL_GetJoysticks(&count);
    if (sticks) {
        for (int i = 0; i < count && !g_devId; i++)
            if (!SDL_IsGamepad(sticks[i])) joy_open(sticks[i]);
        SDL_free(sticks);
    }
}

void joy_init(void) {
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        LogInfo(("joystick: SDL_INIT_GAMEPAD failed: %s", SDL_GetError()));
        return;
    }
    joy_rescan();
}

void joy_shutdown(void) {
    joy_close();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

/* Hotplug handling, called for every SDL event by both event pumps. SDL emits
 * JOYSTICK_ADDED for every device and GAMEPAD_ADDED only for mapped ones, so we
 * open mapped devices on the gamepad event and skip the joystick event for them
 * (SDL_IsGamepad), leaving the raw path for sticks with no mapping. */
void joy_handleEvent(const SDL_Event *ev) {
    switch (ev->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
        joy_open(ev->gdevice.which);
        break;
    case SDL_EVENT_JOYSTICK_ADDED:
        if (!SDL_IsGamepad(ev->jdevice.which)) joy_open(ev->jdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        if (ev->jdevice.which == g_devId) {
            joy_close();
            joy_rescan();
        }
        break;
    default:
        break;
    }
}

/* Refresh joyAxes[0]/[1] from the active device's primary 2-axis stick. */
static void updateAxes(void) {
    if (g_pad) {
        joyAxes[0] = axisByte(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTX));
        joyAxes[1] = axisByte(SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFTY));
    } else if (g_joy && SDL_GetNumJoystickAxes(g_joy) >= 2) {
        joyAxes[0] = axisByte(SDL_GetJoystickAxis(g_joy, 0));
        joyAxes[1] = axisByte(SDL_GetJoystickAxis(g_joy, 1));
    } else {
        joyAxes[0] = joyAxes[1] = 0x80;
    }
}

/* True while fire button n is held. 0 = guns (right trigger, also the menus'
 * confirm button); 1 = missiles (left trigger). The face buttons A/B drive
 * other cockpit actions (brake / designate target) via eginput.c, so they are
 * deliberately not fire buttons. The raw fallback maps straight to physical
 * buttons 0 and 1. */
static int buttonDown(int n) {
    if (g_pad) {
        if (n == 0)
            return SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > JOY_TRIGGER_THRESHOLD;
        if (n == 1)
            return SDL_GetGamepadAxis(g_pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > JOY_TRIGGER_THRESHOLD;
        return 0;
    }
    if (g_joy && n < SDL_GetNumJoystickButtons(g_joy))
        return SDL_GetJoystickButton(g_joy, n);
    return 0;
}

/* eginput.c queries these to drive the richer flight bindings (weapons, views,
 * thrust, gear, eject, ...) off the named gamepad buttons. A raw joystick has
 * no mapped button meanings, so those bindings apply only to a real gamepad;
 * the raw fallback keeps just the stick and the two fire buttons. */
bool joy_isGamepad(void) { return g_pad != NULL; }
bool joy_connected(void) { return g_devId != 0; }
bool joy_button(SDL_GamepadButton b) { return g_pad && SDL_GetGamepadButton(g_pad, b); }
Sint16 joy_axisRaw(SDL_GamepadAxis a) { return g_pad ? SDL_GetGamepadAxis(g_pad, a) : 0; }

/* === game-facing joystick API (declared in egcode.h / stcode.h / slot.h) === */

/* egflight.c reads the stick each frame through this; returns the axis pair as
 * a word for parity with the original (the caller only uses the side effect). */
int far readCalibratedJoystick(void) {
    updateAxes();
    return joyAxes[0] | (joyAxes[1] << 8);
}

/* Menu loops call this to refresh the stick before sampling joyAxes. In menu
 * mode the pad is read through the event pump's menu navigation (input.c), so
 * keep joyAxes centred here to leave the menus' own stick-nav branches inert and
 * avoid double-moving the cursor. */
void far pollJoystick(void) {
    if (input_getMode() == INPUT_MODE_MENU) {
        joyAxes[0] = joyAxes[1] = 0x80;
        return;
    }
    updateAxes();
}

/* MISC overlay slot 0x5d/0x5e: fire button n, nonzero while held. Flight uses
 * this for the fire triggers; in menu mode the triggers are handled (edge, one
 * press = one action) by the event pump's menu navigation, so report nothing
 * here to keep the menus' own level-triggered accept paths inert. */
int FAR CDECL misc_readJoystick(int16 axis) {
    if (input_getMode() == INPUT_MODE_MENU) return 0;
    return buttonDown(axis);
}

/* SDL gamepads/joysticks arrive pre-calibrated, so the original's runtime
 * calibration and its cross-EXE save/restore have nothing to do. Kept as the
 * no-ops the existing call sites (Alt+J, per-program startup) expect. */
int far initJoystickCalibration(void) { return 0; }
void seedJoystickBaseline(void) {}
void readJoystickHardware(void) {}
void computeJoystickAxis(void) {}
int far restoreJoystickData(uint8 FAR *ptr) { return 0; }
void far copyJoystickData(uint8 FAR *ptr) {}
