/* Flight bindings translate to existing game commands, not new game logic. */
#include "controls.h"
#include "egkeys.h"
#include "egtypes.h"
#include "egdata.h"
#include "controls_mapping.h"

static const ControlAction actions[RAW_ACTION_COUNT] = {
    {"cannon", "Fire cannon", SDL_SCANCODE_BACKSPACE, SDL_KMOD_NONE, SCAN_BACKSPACE, 0, 0},
    {"missile", "Fire missile", SDL_SCANCODE_RETURN, SDL_KMOD_NONE, SCAN_ENTER, 0, 0},
    {"countermeasure", "Chaff / flare", SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE, 0, 0, 0},
    {"weapon", "Cycle weapon", SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE, 0, 0, 0},
    {"thrust_up", "Increase thrust", SDL_SCANCODE_EQUALS, SDL_KMOD_NONE, SCAN_EQUAL, 0, 0},
    {"thrust_down", "Decrease thrust", SDL_SCANCODE_MINUS, SDL_KMOD_NONE, SCAN_MINUS, 0, 0},
    {"gear", "Landing gear", SDL_SCANCODE_L, SDL_KMOD_NONE, SCAN_L, 0, 0},
    {"autopilot", "Autopilot", SDL_SCANCODE_P, SDL_KMOD_NONE, SCAN_P, 0, 0},
    {"target", "Next target", SDL_SCANCODE_T, SDL_KMOD_NONE, SCAN_T, 0, 0},
    {"view", "Cycle 3D view", SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE, 0, 0, 0},
    {"chaff", "Chaff", SDL_SCANCODE_C, SDL_KMOD_NONE, SCAN_C, 0, 0},
    {"flare", "Flare", SDL_SCANCODE_F, SDL_KMOD_NONE, SCAN_F, 0, 0},
    {"sidewinder", "Sidewinder", SDL_SCANCODE_S, SDL_KMOD_NONE, SCAN_S, 0, 0},
    {"medium_missile", "Medium-range missile", SDL_SCANCODE_M, SDL_KMOD_NONE, SCAN_M, 0, 0},
    {"maverick", "Maverick", SDL_SCANCODE_G, SDL_KMOD_NONE, SCAN_G, 0, 0},
    {"afterburner", "Afterburner", SDL_SCANCODE_A, SDL_KMOD_NONE, SCAN_A, 0, 0},
    {"full_thrust", "Full thrust", SDL_SCANCODE_EQUALS, SDL_KMOD_SHIFT, SCAN_SHIFT_EQUAL, 0, 0},
    {"idle", "Idle thrust", SDL_SCANCODE_MINUS, SDL_KMOD_SHIFT, SCAN_SHIFT_MINUS, 0, 0},
    {"brake", "Brake", SDL_SCANCODE_B, SDL_KMOD_NONE, SCAN_B, 0, 0},
    {"radar", "Radar range", SDL_SCANCODE_R, SDL_KMOD_NONE, SCAN_R, 0, 0},
    {"zoom_in", "Map zoom in", SDL_SCANCODE_Z, SDL_KMOD_NONE, SCAN_Z, 0, 0},
    {"zoom_out", "Map zoom out", SDL_SCANCODE_X, SDL_KMOD_NONE, SCAN_X, 0, 0},
    {"director", "Flight director", SDL_SCANCODE_D, SDL_KMOD_NONE, SCAN_D, 0, 0},
    {"waypoint", "Next waypoint", SDL_SCANCODE_W, SDL_KMOD_NONE, SCAN_W, 0, 0},
    {"cockpit", "Cockpit view", SDL_SCANCODE_SPACE, SDL_KMOD_NONE, SCAN_SPACEBAR, 0, 0},
    {"forward", "Forward view", SDL_SCANCODE_F1, SDL_KMOD_NONE, SCAN_F1, 0, 0},
    {"left_view", "Left view", SDL_SCANCODE_F2, SDL_KMOD_NONE, SCAN_F2, 0, 0},
    {"right_view", "Right view", SDL_SCANCODE_F3, SDL_KMOD_NONE, SCAN_F3, 0, 0},
    {"rear", "Rear view", SDL_SCANCODE_F4, SDL_KMOD_NONE, SCAN_F4, 0, 0},
    {"follow", "External follow", SDL_SCANCODE_F5, SDL_KMOD_NONE, SCAN_F5, 0, 0},
    {"dynamic", "External dynamic", SDL_SCANCODE_F6, SDL_KMOD_NONE, SCAN_F6, 0, 0},
    {"side_view", "External side", SDL_SCANCODE_F7, SDL_KMOD_NONE, SCAN_F7, 0, 0},
    {"missile_view", "Missile view", SDL_SCANCODE_F8, SDL_KMOD_NONE, SCAN_F8, 0, 0},
    {"external_target", "External target", SDL_SCANCODE_F9, SDL_KMOD_NONE, SCAN_F9, 0, 0},
    {"target_view", "Target view", SDL_SCANCODE_F10, SDL_KMOD_NONE, SCAN_F10, 0, 0},
    {"eject", "Eject (press twice)", SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE, SCAN_ESCAPE, 0, 0},
    {"pause", "Pause", SDL_SCANCODE_P, SDL_KMOD_ALT, SCAN_ALT_P, 0, 0},
    {"accel", "Time acceleration", SDL_SCANCODE_A, SDL_KMOD_ALT, SCAN_ALT_A, 0, 0},
    {"sound", "Sound level", SDL_SCANCODE_V, SDL_KMOD_ALT, SCAN_ALT_V, 0, 0},
    {"detail", "Detail level", SDL_SCANCODE_D, SDL_KMOD_ALT, SCAN_ALT_D, 0, 0},
    {"sensitivity", "Keyboard sensitivity", SDL_SCANCODE_K, SDL_KMOD_ALT, SCAN_ALT_K, 0, 0},
    {"night", "Night palette", SDL_SCANCODE_N, SDL_KMOD_ALT, SCAN_ALT_N, 0, 0},
    {"training", "Training mode", SDL_SCANCODE_T, SDL_KMOD_ALT, SCAN_ALT_T, 0, 0},
    {"end_mission", "End mission", SDL_SCANCODE_Q, SDL_KMOD_ALT, SCAN_ALT_Q, 0, 0},
    {"boss", "Hide game / pause", SDL_SCANCODE_B, SDL_KMOD_ALT, SCAN_ALT_B, 0, 0},
    {"calibrate", "Legacy calibration", SDL_SCANCODE_J, SDL_KMOD_ALT, SCAN_ALT_J, 0, 0},
    {"memory", "Memory diagnostic", SDL_SCANCODE_M, SDL_KMOD_ALT, SCAN_ALT_M, 0, 0},
    {"frame_time", "Frame time diagnostic", SDL_SCANCODE_F, SDL_KMOD_ALT, SCAN_ALT_F, 0, 0},
    {"rearm", "Training: rearm", SDL_SCANCODE_R, SDL_KMOD_ALT, 0x1300, 0, 0},
    {"map_north", "Training: map north", SDL_SCANCODE_S, SDL_KMOD_ALT, 0x1f00, 0, 0},
    {"map_south", "Training: map south", SDL_SCANCODE_X, SDL_KMOD_ALT, 0x2d00, 0, 0},
    {"map_west", "Training: map west", SDL_SCANCODE_Z, SDL_KMOD_ALT, 0x2c00, 0, 0},
    {"map_east", "Training: map east", SDL_SCANCODE_C, SDL_KMOD_ALT, 0x2e00, 0, 0},
    {"pitch_down", "Pitch down", SDL_SCANCODE_UP, SDL_KMOD_NONE, 0, 0, -1},
    {"pitch_up", "Pitch up", SDL_SCANCODE_DOWN, SDL_KMOD_NONE, 0, 0, 1},
    {"roll_left", "Roll left", SDL_SCANCODE_LEFT, SDL_KMOD_NONE, 0, -1, 0},
    {"roll_right", "Roll right", SDL_SCANCODE_RIGHT, SDL_KMOD_NONE, 0, 1, 0},
    {"kp_up", "Pitch down (keypad)", SDL_SCANCODE_KP_8, SDL_KMOD_NONE, 0, 0, -1},
    {"kp_down", "Pitch up (keypad)", SDL_SCANCODE_KP_2, SDL_KMOD_NONE, 0, 0, 1},
    {"kp_left", "Roll left (keypad)", SDL_SCANCODE_KP_4, SDL_KMOD_NONE, 0, -1, 0},
    {"kp_right", "Roll right (keypad)", SDL_SCANCODE_KP_6, SDL_KMOD_NONE, 0, 1, 0},
    {"kp_up_left", "Pitch down + left", SDL_SCANCODE_KP_7, SDL_KMOD_NONE, 0, -1, -1},
    {"kp_up_right", "Pitch down + right", SDL_SCANCODE_KP_9, SDL_KMOD_NONE, 0, 1, -1},
    {"kp_down_left", "Pitch up + left", SDL_SCANCODE_KP_1, SDL_KMOD_NONE, 0, -1, 1},
    {"kp_down_right", "Pitch up + right", SDL_SCANCODE_KP_3, SDL_KMOD_NONE, 0, 1, 1}
};

static ControlBinding bindings[RAW_ACTION_COUNT] = {};
static bool initialized = false;
static int capture = -1;
static bool nextFlare = false;

/* Ignore lock keys and collapse left/right modifier variants. */
static SDL_Keymod modifiers(SDL_Keymod mod) {
    return (SDL_Keymod)((mod & SDL_KMOD_ALT ? SDL_KMOD_ALT : 0) |
                       (mod & SDL_KMOD_CTRL ? SDL_KMOD_CTRL : 0) |
                       (mod & SDL_KMOD_SHIFT ? SDL_KMOD_SHIFT : 0));
}

/* Keypad Enter is the upstream alias for Return, including saved mappings. */
static SDL_Scancode canonical(SDL_Scancode key) {
    return key == SDL_SCANCODE_KP_ENTER ? SDL_SCANCODE_RETURN : key;
}

/* Reject modifier-only bindings and the window-level fullscreen shortcut. */
static bool validKey(SDL_Scancode key, SDL_Keymod mod) {
    return key >= SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT &&
           !(key >= SDL_SCANCODE_LCTRL && key <= SDL_SCANCODE_RGUI) &&
           !(key == SDL_SCANCODE_RETURN && (mod & SDL_KMOD_ALT));
}

/* Defaults are also lazy-initialized for runtime isolation tests. */
void controls_resetKeyboard(void) {
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) bindings[i] = {actions[i].key, actions[i].modifiers};
    initialized = true;
    capture = -1;
    nextFlare = false;
}

/* Supply metadata without coupling storage or UI to flight globals. */
const ControlAction &controls_action(RawAction action) { return actions[action]; }

/* Only the two cycle actions depend on current game state. */
Uint16 controls_command(RawAction action, int weapon, int view, bool *countermeasureState) {
    if (action == RAW_COUNTERMEASURE) {
        bool &state = countermeasureState ? *countermeasureState : nextFlare;
        const Uint16 command = state ? SCAN_F : SCAN_C;
        state = !state;
        return command;
    }
    if (action == RAW_WEAPON) return weapon == 0 ? SCAN_M : weapon == 1 ? SCAN_G : SCAN_S;
    if (action == RAW_VIEW) {
        if (view == VIEW_COCKPIT) return SCAN_F5;
        if (view == VIEW_EXT_FOLLOW) return SCAN_F6;
        if (view == VIEW_EXT_DYNAMIC) return SCAN_F7;
        return SCAN_SPACEBAR;
    }
    return actions[action].command;
}

/* Swap conflicts, as joystick assignments do, so one key has one action. */
bool controls_bindKey(RawAction action, SDL_Scancode key, SDL_Keymod mod) {
    if (!initialized) controls_resetKeyboard();
    key = canonical(key);
    mod = key == SDL_SCANCODE_UNKNOWN ? SDL_KMOD_NONE : modifiers(mod);
    if (action < 0 || action >= RAW_ACTION_COUNT || !validKey(key, mod)) return false;
    for (int i = 0; i < RAW_ACTION_COUNT; ++i)
        if (i != action && key != SDL_SCANCODE_UNKNOWN && bindings[i].key == key && bindings[i].mod == mod)
            bindings[i] = bindings[action];
    bindings[action] = {key, mod};
    return true;
}

/* Render physical key names with normalized modifiers, independent of layout. */
std::string controls_keyName(RawAction action) {
    if (!initialized) controls_resetKeyboard();
    const ControlBinding b = bindings[action];
    if (!b.key) return "-";
    const char *name = SDL_GetScancodeName(b.key);
    const std::string keyName = name && *name ? name : "Key " + std::to_string(b.key);
    return std::string(b.mod & SDL_KMOD_ALT ? "Alt+" : "") +
           (b.mod & SDL_KMOD_CTRL ? "Ctrl+" : "") +
           (b.mod & SDL_KMOD_SHIFT ? "Shift+" : "") + keyName;
}

/* Translate only flight keys. Suppress displaced defaults instead of leaving
 * them active as invisible extra bindings; unknown legacy keys still pass. */
Uint16 controls_translateKey(SDL_Scancode key, SDL_Keymod mod, Uint16 fallback) {
    if (!initialized) controls_resetKeyboard();
    key = canonical(key);
    mod = modifiers(mod);
    for (int i = 0; i < RAW_ACTION_COUNT; ++i)
        if (key && bindings[i].key == key && bindings[i].mod == mod)
            return controls_command((RawAction)i, missileSpecIndex, g_viewMode);
    for (int i = 0; i < RAW_ACTION_COUNT; ++i)
        if (actions[i].key == key && actions[i].modifiers == mod) return 0;
    return fallback;
}

/* Preserve the legacy ordering of arrow/keypad diagonals and held axes. */
void controls_applyAxes(Uint8 *x, Uint8 *y, bool joystick) {
    if (!initialized) controls_resetKeyboard();
    const bool *keys = SDL_GetKeyboardState(nullptr);
    const SDL_Keymod mod = modifiers(SDL_GetModState());
    for (int i = RAW_PITCH_DOWN; i < RAW_ACTION_COUNT; ++i) {
        const bool held = joystick ? joy_actionHeld((RawAction)i) :
            bindings[i].key && keys[bindings[i].key] &&
            (bindings[i].mod == mod || (bindings[i].key == actions[i].key && bindings[i].mod == SDL_KMOD_NONE));
        if (!held) continue;
        if (actions[i].x) *x = actions[i].x < 0 ? 0x26 : 0xda;
        if (actions[i].y) *y = actions[i].y < 0 ? 0x26 : 0xda;
    }
}

/* Export a value snapshot; persistence never accesses live arrays directly. */
ControlBinding controls_keyboardBinding(RawAction action) {
    if (!initialized) controls_resetKeyboard();
    return bindings[action];
}

/* Reject a malformed profile as a unit, retaining current bindings on failure. */
bool controls_replaceKeyboard(const ControlBinding *candidate) {
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) {
        const ControlBinding b = candidate[i];
        if (!validKey(b.key, b.mod) || canonical(b.key) != b.key ||
            modifiers(b.mod) != b.mod || (!b.key && b.mod)) return false;
        for (int j = 0; j < i; ++j)
            if (b.key && candidate[j].key == b.key && candidate[j].mod == b.mod) return false;
    }
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) bindings[i] = candidate[i];
    initialized = true;
    return true;
}

/* Capture uses the existing event pump, never a competing SDL event loop. */
void controls_beginCapture(RawAction action) { capture = action; }
bool controls_capturing(void) { return capture >= 0; }

/* Remotes can report Escape as a physical button, so capture it like any key.
 * UNKNOWN is the stored unbound sentinel, not a usable input event. */
bool controls_captureKey(const SDL_KeyboardEvent &event) {
    if (capture < 0) return false;
    if (event.repeat || event.scancode == SDL_SCANCODE_UNKNOWN) return true;
    if (controls_bindKey((RawAction)capture, event.scancode, event.mod)) capture = -1;
    return true;
}
