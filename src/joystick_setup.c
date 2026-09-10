/* Startup controls editor in the original bitmap style. Input.c remains the
 * sole SDL event pump; flight assignments never change menu navigation. */
#include "joystick.h"
#include "controls.h"
#include "input.h"
#include "gfx.h"
#include "const.h"
#include "shared/common.h"

static const int visibleRows = 8;
static const int continueRow = RAW_ACTION_COUNT;
static const int resetRow = RAW_ACTION_COUNT + 1;
static const int rowCount = resetRow + 1;
static const int stickNavigationThreshold = 16000;
static const Uint64 firstNavigationRepeatMs = 400;
static const Uint64 navigationRepeatMs = 200;
static const Uint32 setupPollMs = 20;

/* Scroll the action table, keeping Continue and Reset visible on every page. */
static void drawJoystickSetup(int selected, int first, bool keyboard, bool saveFailed) {
    int pitch = 0;
    uint8 *pixels = gfx_pagePixels(0, &pitch);
    if (!pixels) return;
    for (int y = 0; y < LOGICAL_HEIGHT; ++y)
        SDL_memset(pixels + y * pitch, COLOR_BLACK, LOGICAL_WIDTH);
    int16 page[12] = {};
    page[2] = COLOR_LIGHTRED;
    page[6] = 1;
    gfx_setDrawColor(COLOR_LIGHTRED);
    gfx_drawLine(8, 8, 311, 8);
    gfx_drawLine(311, 8, 311, 191);
    gfx_drawLine(311, 191, 8, 191);
    gfx_drawLine(8, 191, 8, 8);
    drawStringCentered(page, "CONTROLS SETUP", 0, 18, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTGRAY;
    drawStringAt(page, keyboard ? "> Keyboard" : "Keyboard", 170, 35);
    drawStringAt(page, keyboard ? "Joystick" : "> Joystick", 258, 35);
    for (int row = 0; row < visibleRows && first + row < RAW_ACTION_COUNT; ++row) {
        const RawAction action = (RawAction)(first + row);
        const int y = 49 + row * 11;
        page[2] = action == selected ? COLOR_WHITE : COLOR_LIGHTGRAY;
        drawStringAt(page, action == selected ? ">" : " ", 14, y);
        drawStringAt(page, controls_action(action).label, 23, y);
        /* Keep the columns bounded with the readable font. The selected
         * binding is also shown in full below the list, including modifiers. */
        const std::string key = controls_keyName(action);
        const std::string shortKey = key.size() > 11 ? key.substr(0, 10) + ">" : key;
        drawStringAt(page, shortKey.c_str(), 170, y);
        char button[16] = {};
        const int binding = joy_rawBinding(action);
        if (binding < 0) SDL_strlcpy(button, "-", sizeof(button));
        else SDL_snprintf(button, sizeof(button), "%d", binding + 1);
        drawStringAt(page, button, 270, y);
    }
    if (selected < RAW_ACTION_COUNT) {
        page[2] = COLOR_WHITE;
        const std::string binding = "Key: " + controls_keyName((RawAction)selected);
        drawStringCentered(page, binding.c_str(), 0, 138, LOGICAL_WIDTH);
    }
    page[2] = selected == continueRow ? COLOR_WHITE : COLOR_LIGHTGRAY;
    drawStringCentered(page, selected == continueRow ? "> CONTINUE <" : "CONTINUE", 0, 151, LOGICAL_WIDTH);
    page[2] = selected == resetRow ? COLOR_WHITE : COLOR_LIGHTGRAY;
    drawStringCentered(page, selected == resetRow ? "> RESET DEFAULTS <" : "RESET DEFAULTS", 0, 162, LOGICAL_WIDTH);
    page[2] = COLOR_LIGHTRED;
    drawStringCentered(page, saveFailed ? "Save failed. Continue to play unsaved." :
                       controls_capturing() ? "Press key / chord to assign." :
                       "ENTER: edit  DEL: clear  ESC: finish", 0, 177, LOGICAL_WIDTH);
    gfx_commitPage();
}

/* Only transient screen state lives here; saved bindings belong to controls. */
struct SetupState {
    int selected = continueRow;
    int first = 0;
    bool keyboard = true;
    bool released = false;
    bool deleteHeld = false;
    bool saveFailed = false;
    int stickZone = 0;
    Uint64 repeatAt = 0;
};

/* Either stick axis navigates with the same dead zone and repeat timing. */
static void navigateWithStick(SetupState &state) {
    const int x = joy_rawMenuAxis(0), y = joy_rawMenuAxis(1);
    const int axis = SDL_abs(y) >= SDL_abs(x) ? y : x;
    const int zone = axis < -stickNavigationThreshold ? -1 :
                     axis > stickNavigationThreshold ? 1 : 0;
    const Uint64 now = SDL_GetTicks();
    if (zone && (zone != state.stickZone || now >= state.repeatAt)) {
        state.selected = (state.selected + zone + rowCount) % rowCount;
        state.repeatAt = now + (zone != state.stickZone ? firstNavigationRepeatMs : navigationRepeatMs);
    }
    state.stickZone = zone;
}

/* Consume menu keys, never flight bindings. Escape requests save-and-play. */
static bool navigateWithKeyboard(SetupState &state, bool &activate) {
    bool finish = false;
    while (input_keyWaiting()) {
        const uint16 key = input_readKey();
        if ((key & 255) == KEYCODE_ESC) finish = true;
        else if ((key & 255) == KEYCODE_ENTER) activate = true;
        else if (key == KEYCODE_UPARROW) state.selected = (state.selected + rowCount - 1) % rowCount;
        else if (key == KEYCODE_DNARROW) state.selected = (state.selected + 1) % rowCount;
        else if (key == KEYCODE_LEFTARROW) state.keyboard = true;
        else if (key == KEYCODE_RIGHTARROW) state.keyboard = false;
    }
    return finish;
}

/* A fresh button assigns the row, or activates Continue/Reset. */
static void assignJoystickButton(SetupState &state, bool &activate) {
    const int pressed = joy_rawPressedButton();
    if (pressed < 0) state.released = true;
    else if (state.released) {
        if (state.selected >= RAW_ACTION_COUNT) activate = true;
        else {
            state.keyboard = false;
            const RawAction action = (RawAction)state.selected;
            joy_bindRawButton(action, joy_rawBinding(action) == pressed ? -1 : pressed);
        }
        state.released = false;
    }
}

/* Start capture or restore defaults; only Continue asks the caller to finish. */
static bool activateSelection(SetupState &state) {
    if (state.selected == continueRow) return true;
    if (state.selected == resetRow) {
        controls_resetKeyboard();
        joy_resetRawMapping();
        state.saveFailed = false;
    } else if (state.keyboard) {
        controls_beginCapture((RawAction)state.selected);
        input_ringReset();
    }
    return false;
}

/* A failed save leaves the screen open; a second Continue permits playing
 * unsaved. Both independent profiles are attempted even if one fails. */
static bool saveAndContinue(SetupState &state, const std::string &keyboardPath) {
    const bool keysSaved = state.saveFailed || controls_saveKeyboard(keyboardPath);
    const bool stickSaved = state.saveFailed || !joy_rawDeviceId() || joy_saveRawMapping();
    state.saveFailed = !(keysSaved && stickSaved);
    state.selected = continueRow;
    return !state.saveFailed;
}

/* Keyboard setup is available without a joystick. A hotplugged device keeps
 * its own profile; losing a stick does not prevent continuing with keys. */
void joy_showSetup(void) {
    controls_resetKeyboard();
    const std::string keyboardPath = controls_keyboardPath();
    controls_loadKeyboard(keyboardPath);
    const InputMode previousMode = input_getMode();
    input_setMode(INPUT_MODE_MENU);
    input_setJoystickSetup(true);
    gfx_setTextInputEnabled(false);
    input_ringReset();
    /* Pilot selection and mission menus use palette 1. Palette 0 gives these
     * same color indices different RGB values, unlike the surrounding menus. */
    gfx_setDac(1);
    SDL_JoystickID device = joy_rawDeviceId();
    SetupState state;
    state.released = joy_rawPressedButton() < 0;
    bool finished = false;
    drawJoystickSetup(state.selected, state.first, state.keyboard, state.saveFailed);
    while (!finished && !input_quitRequested()) {
        const bool wasCapturing = controls_capturing();
        input_pumpEvents();
        if (joy_rawDeviceId() != device) {
            device = joy_rawDeviceId();
            state.released = false;
        }
        if (!input_hasFocus()) {
            controls_beginCapture((RawAction)-1);
            input_ringReset();
            state.released = false;
            state.stickZone = 0;
            SDL_Delay(setupPollMs);
            continue;
        }
        bool activate = false;
        bool continueRequested = false;
        const bool deleteNow = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_DELETE];
        if (!wasCapturing && !controls_capturing()) {
            if (deleteNow && !state.deleteHeld && state.selected < RAW_ACTION_COUNT) {
                if (state.keyboard) controls_bindKey((RawAction)state.selected, SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE);
                else joy_bindRawButton((RawAction)state.selected, -1);
            }
            navigateWithStick(state);
            continueRequested = navigateWithKeyboard(state, activate);
            assignJoystickButton(state, activate);
            if (activate && activateSelection(state)) continueRequested = true;
        } else {
            /* A captured Enter or arrow must not also activate or move a row. */
            input_ringReset();
            state.released = joy_rawPressedButton() < 0;
        }
        state.deleteHeld = deleteNow;
        if (continueRequested) finished = saveAndContinue(state, keyboardPath);
        if (state.selected < RAW_ACTION_COUNT) {
            if (state.selected < state.first) state.first = state.selected;
            if (state.selected >= state.first + visibleRows) state.first = state.selected - visibleRows + 1;
        }
        drawJoystickSetup(state.selected, state.first, state.keyboard, state.saveFailed);
        SDL_Delay(setupPollMs);
    }
    controls_beginCapture((RawAction)-1);
    input_setJoystickSetup(false);
    input_setMode(previousMode);
    gfx_setTextInputEnabled(previousMode == INPUT_MODE_MENU);
    joy_resetFlightInput();
}
