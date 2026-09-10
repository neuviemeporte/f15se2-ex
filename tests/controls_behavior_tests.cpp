#include "controls.h"
#include "controls_mapping.h"
#include "input.h"
#include "egkeys.h"
#include "egtypes.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include "shared/common.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdlib>

static void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << "\n"; std::exit(1); }
}

static void pressEnter(const char *text, int, int, int, int) {
    if (SDL_strcmp(text, "> CONTINUE <")) return;
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_RETURN;
    event.key.key = SDLK_RETURN;
    SDL_PushEvent(&event);
    g_textRecorder = nullptr;
}

static int setupStep = 0;

static void setupKey(SDL_Scancode sc, SDL_Keycode key) {
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = sc;
    event.key.key = key;
    require(SDL_PushEvent(&event), "queue setup navigation");
}

static void editThenReset(const char *text, int, int, int, int) {
    if (!SDL_strstr(text, "ENTER: edit") && !SDL_strstr(text, "Press key / chord")) return;
    switch (setupStep++) {
    case 0: setupKey(SDL_SCANCODE_UP, SDLK_UP); break;
    case 1:
        setupKey(SDL_SCANCODE_LEFT, SDLK_LEFT);
        setupKey(SDL_SCANCODE_RETURN, SDLK_RETURN);
        break;
    case 2:
        require(controls_capturing(), "Enter enters actual setup capture");
        setupKey(SDL_SCANCODE_F12, SDLK_F12);
        break;
    case 3:
        require(controls_keyboardBinding(RAW_KP_DOWN_RIGHT).key == SDL_SCANCODE_F12,
                "setup assigns selected action");
        setupKey(SDL_SCANCODE_DOWN, SDLK_DOWN);
        setupKey(SDL_SCANCODE_DOWN, SDLK_DOWN);
        setupKey(SDL_SCANCODE_RETURN, SDLK_RETURN);
        break;
    case 4:
        require(controls_keyboardBinding(RAW_KP_DOWN_RIGHT).key == SDL_SCANCODE_KP_3,
                "Reset defaults restores edited action");
        setupKey(SDL_SCANCODE_ESCAPE, SDLK_ESCAPE);
        g_textRecorder = nullptr;
        break;
    default: require(false, "unexpected setup loop");
    }
}

static void profileValidation() {
    ControlBinding profile[RAW_ACTION_COUNT] = {};
    controls_resetKeyboard();
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) profile[i] = controls_keyboardBinding((RawAction)i);
    const ControlBinding original = profile[RAW_GEAR];
    for (const ControlBinding invalid : {
            ControlBinding{SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE},
            ControlBinding{SDL_SCANCODE_RETURN, SDL_KMOD_ALT},
            ControlBinding{SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE},
            ControlBinding{SDL_SCANCODE_L, SDL_KMOD_LCTRL},
            ControlBinding{SDL_SCANCODE_UNKNOWN, SDL_KMOD_CTRL},
            profile[RAW_MISSILE]}) {
        profile[RAW_GEAR] = invalid;
        require(!controls_replaceKeyboard(profile), "reject invalid complete profile");
        require(controls_keyboardBinding(RAW_GEAR).key == original.key, "atomic rejection");
    }
    profile[RAW_GEAR] = original;
    require(controls_replaceKeyboard(profile), "valid profile accepted");
    require(!controls_bindKey((RawAction)-1, SDL_SCANCODE_F12, SDL_KMOD_NONE), "invalid action rejected");
    bool sequence = false;
    require(controls_command(RAW_COUNTERMEASURE, 0, 0, &sequence) == SCAN_C &&
            controls_command(RAW_COUNTERMEASURE, 0, 0, &sequence) == SCAN_F, "device-local sequence");
    for (int weapon = 0; weapon < 3; ++weapon) {
        const Uint16 commands[] = {SCAN_M, SCAN_G, SCAN_S};
        require(controls_command(RAW_WEAPON, weapon, 0) == commands[weapon], "weapon cycle");
    }
    const int views[] = {VIEW_COCKPIT, VIEW_EXT_FOLLOW, VIEW_EXT_DYNAMIC, VIEW_TARGET};
    const Uint16 commands[] = {SCAN_F5, SCAN_F6, SCAN_F7, SCAN_SPACEBAR};
    for (int i = 0; i < 4; ++i)
        require(controls_command(RAW_VIEW, 0, views[i]) == commands[i], "view cycle follows game state");
}

static void remoteKeys(const std::string &path) {
    controls_resetKeyboard();
    input_setMode(INPUT_MODE_MENU);
    input_ringReset();
    setupKey(SDL_SCANCODE_SELECT, SDLK_SELECT);
    require(input_keyWaiting() && input_readKey() == SCAN_ENTER, "remote Select confirms menus");
    setupKey(SDL_SCANCODE_AC_BACK, SDLK_AC_BACK);
    require(input_keyWaiting() && input_readKey() == SCAN_ESCAPE, "remote Back exits menus");

    const struct {
        RawAction action;
        SDL_Scancode scancode;
        SDL_Keycode key;
        Uint16 command;
    } buttons[] = {
        {RAW_MISSILE, SDL_SCANCODE_SELECT, SDLK_SELECT, SCAN_ENTER},
        {RAW_GEAR, SDL_SCANCODE_AC_BACK, SDLK_AC_BACK, SCAN_L},
        {RAW_AUTOPILOT, SDL_SCANCODE_VOLUMEUP, SDLK_VOLUMEUP, SCAN_P},
        {RAW_FLARE, SDL_SCANCODE_ESCAPE, SDLK_ESCAPE, SCAN_F}
    };
    for (const auto &button : buttons) {
        controls_beginCapture(button.action);
        setupKey(button.scancode, button.key);
        input_pumpEvents();
        require(!controls_capturing(), "remote key completes capture");
        require(!input_keyWaiting(), "captured remote key does not navigate menu");
        require(controls_keyboardBinding(button.action).key == button.scancode, "remote scancode captured");
        require(!controls_keyName(button.action).empty(), "remote binding has a visible name");
    }
    controls_beginCapture(RAW_GEAR);
    SDL_KeyboardEvent unknown = {};
    require(controls_captureKey(unknown) && controls_capturing(), "unknown key does not end capture");
    require(controls_keyboardBinding(RAW_GEAR).key == SDL_SCANCODE_AC_BACK, "unknown key does not erase binding");
    unknown.scancode = SDL_SCANCODE_SELECT;
    unknown.repeat = true;
    require(controls_captureKey(unknown) && controls_capturing(), "repeat does not assign a remote key");
    controls_beginCapture((RawAction)-1);

    require(controls_saveKeyboard(path), "save remote bindings");
    controls_resetKeyboard();
    require(controls_loadKeyboard(path), "reload remote bindings");
    input_setMode(INPUT_MODE_FLIGHT);
    for (const auto &button : buttons) {
        setupKey(button.scancode, button.key);
        require(input_keyWaiting() && input_readKey() == button.command, "saved remote key reaches flight command");
    }
    controls_resetKeyboard();
    require(controls_saveKeyboard(path), "restore defaults for subsequent setup tests");
    input_setMode(INPUT_MODE_MENU);
}

static void directionalButtons() {
    SDL_VirtualJoystickDesc desc = {};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
    desc.naxes = 2;
    desc.nbuttons = 2;
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    require(id != 0, "attach directional fixture");
    SDL_Joystick *stick = SDL_OpenJoystick(id);
    require(stick != nullptr, "open directional fixture");
    SDL_Event event = {};
    event.type = SDL_EVENT_JOYSTICK_ADDED;
    event.jdevice.which = id;
    joy_handleEvent(&event);
    joy_bindRawButton(RAW_ROLL_LEFT, 0);
    joy_bindRawButton(RAW_KP_DOWN_RIGHT, 1);
    require(SDL_SetJoystickVirtualButton(stick, 0, true), "hold left");
    SDL_UpdateJoysticks();
    Uint8 x = 128, y = 128;
    controls_applyAxes(&x, &y, true);
    require(x == 0x26 && y == 128, "directional button sets roll");
    require(SDL_SetJoystickVirtualButton(stick, 1, true), "hold diagonal");
    SDL_UpdateJoysticks();
    controls_applyAxes(&x, &y, true);
    require(x == 0xda && y == 0xda, "diagonal overrides single direction");
    joy_resetRawMapping();
    require(joy_rawBinding(RAW_CANNON) == 0 && joy_rawBinding(RAW_MISSILE) == 1,
            "reset restores physical fire buttons");
    SDL_CloseJoystick(stick);
    require(SDL_DetachVirtualJoystick(id), "detach fixture");
    joy_shutdown();
}

int main() {
    test_headless_init();
    require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD), "SDL init");
    const auto dir = std::filesystem::temp_directory_path() /
        ("f15-controls-" + std::to_string(SDL_GetPerformanceCounter()));
    std::filesystem::create_directories(dir);
    SDL_setenv_unsafe("F15_JOY_CONFIG_DIR", dir.string().c_str(), 1);
    controls_resetKeyboard();
    for (int i = 0; i < RAW_ACTION_COUNT; ++i) {
        const RawAction action = (RawAction)i;
        const ControlAction &info = controls_action(action);
        require(info.name && *info.name && info.label && *info.label, "every action has metadata");
        if (info.key)
            require(controls_translateKey(info.key, info.modifiers, 0) == info.command, "original keyboard defaults");
    }
    require(controls_translateKey(SDL_SCANCODE_KP_ENTER, SDL_KMOD_NONE, 0) == SCAN_ENTER, "keypad Enter alias");
    require(controls_bindKey(RAW_GEAR, SDL_SCANCODE_P, SDL_KMOD_NONE), "swap keys");
    require(controls_translateKey(SDL_SCANCODE_P, SDL_KMOD_NONE, 0) == SCAN_L, "P is gear");
    require(controls_translateKey(SDL_SCANCODE_L, SDL_KMOD_NONE, 0) == SCAN_P, "L is autopilot");
    require(controls_bindKey(RAW_GEAR, SDL_SCANCODE_UNKNOWN, SDL_KMOD_NONE), "clear key");
    require(controls_translateKey(SDL_SCANCODE_P, SDL_KMOD_NONE, SCAN_P) == 0, "displaced default suppressed");
    require(!controls_bindKey(RAW_GEAR, SDL_SCANCODE_RETURN, SDL_KMOD_ALT), "fullscreen reserved");
    require(!controls_bindKey(RAW_GEAR, SDL_SCANCODE_LSHIFT, SDL_KMOD_NONE), "modifier alone rejected");
    require(controls_bindKey(RAW_GEAR, SDL_SCANCODE_F12, SDL_KMOD_CTRL), "arbitrary physical key");
    require(controls_translateKey(SDL_SCANCODE_F12, SDL_KMOD_RCTRL, 0) == SCAN_L, "normalize right modifier");
    input_setMode(INPUT_MODE_FLIGHT);
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = SDL_SCANCODE_F12;
    event.key.mod = SDL_KMOD_LCTRL;
    SDL_PushEvent(&event);
    require(input_keyWaiting() && input_readKey() == SCAN_L, "real input pump applies mapping");
    controls_beginCapture(RAW_GEAR);
    event.key.scancode = SDL_SCANCODE_LSHIFT;
    event.key.mod = SDL_KMOD_LSHIFT;
    require(controls_captureKey(event.key) && controls_capturing(), "capture waits for key");
    event.key.scancode = SDL_SCANCODE_F11;
    require(controls_captureKey(event.key) && !controls_capturing(), "capture assigns chord");
    require(controls_translateKey(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, 0) == SCAN_L, "captured chord acts");
    controls_beginCapture(RAW_MISSILE);
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.mod = SDL_KMOD_NONE;
    controls_captureKey(event.key);
    require(!controls_capturing() && controls_keyboardBinding(RAW_MISSILE).key == SDL_SCANCODE_ESCAPE,
            "Escape can be assigned like other remote keys");
    const std::string path = controls_keyboardPath();
    require(controls_saveKeyboard(path), "save keyboard");
    controls_resetKeyboard();
    require(controls_translateKey(SDL_SCANCODE_L, SDL_KMOD_NONE, 0) == SCAN_L, "reset restores defaults");
    require(controls_loadKeyboard(path), "load keyboard");
    require(controls_translateKey(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, 0) == SCAN_L, "roundtrip");
    std::ofstream(path) << "F15_KEYBOARD 1\ncannon 999999 0\n";
    require(!controls_loadKeyboard(path), "malformed file rejected");
    require(controls_translateKey(SDL_SCANCODE_F11, SDL_KMOD_SHIFT, 0) == SCAN_L, "failed load preserves mapping");
    require(!controls_saveKeyboard((dir / "missing" / "keyboard.txt").string()), "failed save reported");
    gfx_videoInit();
    gfx_initState();
    gfx_setMode13();
    input_setMode(INPUT_MODE_MENU);
    g_textRecorder = pressEnter;
    joy_showSetup();
    require(g_textRecorder == nullptr, "keyboard-only setup defaults to Continue");
    require(controls_loadKeyboard(path), "setup saves valid defaults after invalid profile");
    profileValidation();
    remoteKeys(path);
    directionalButtons();
    g_textRecorder = editThenReset;
    joy_showSetup();
    require(setupStep == 5 && !controls_capturing(), "navigate, capture, reset and exit real setup");
    gfx_videoShutdown();
    SDL_Quit();
    SDL_unsetenv_unsafe("F15_JOY_CONFIG_DIR");
    std::filesystem::remove_all(dir);
    std::cout << "controls_behavior_tests passed\n";
}
