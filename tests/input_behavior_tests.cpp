#include "egcode.h"
#include "egdata.h"
#include "eginput.h"
#include "headless.h"
#include "input.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

extern int kbhit(void);
extern void waitForKeyPress(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum InputOriginalConstant : int {
    kStickCenter = 0x80,
    kBiosR = 0x1372,
    kBiosShiftR = 0x1352,
    kBiosCtrlR = 0x1312,
    kBiosAltQ = 0x1000,
    kPauseTimingValue = 1234,
    kBiosA = 0x1E61,
    kBiosB = 0x3062,
    kBiosC = 0x2E63,
    kBiosD = 0x2064,
    kBiosE = 0x1265,
    kBiosF = 0x2166,
    kBiosG = 0x2267,
    kBiosH = 0x2368,
    kBiosI = 0x1769,
    kBiosJ = 0x246A,
    kBiosK = 0x256B,
    kBiosL = 0x266C,
    kBiosM = 0x326D,
    kBiosN = 0x316E,
    kBiosO = 0x186F,
    kBiosP = 0x1970,
    kBiosQ = 0x1071,
    kBiosS = 0x1F73,
    kBiosT = 0x1474,
    kBiosU = 0x1675,
    kBiosV = 0x2F76,
    kBiosW = 0x1177,
    kBiosX = 0x2D78,
    kBiosY = 0x1579,
    kBiosZ = 0x2C7A,
    kBios1 = 0x0231,
    kBios2 = 0x0332,
    kBios3 = 0x0433,
    kBios4 = 0x0534,
    kBios5 = 0x0635,
    kBios6 = 0x0736,
    kBios7 = 0x0837,
    kBios8 = 0x0938,
    kBios9 = 0x0A39,
    kBios0 = 0x0B30,
    kBiosShift1 = 0x0221,
    kBiosMinus = 0x0C2D,
    kBiosEquals = 0x0D3D,
    kBiosLeftBracket = 0x1A5B,
    kBiosRightBracket = 0x1B5D,
    kBiosSemicolon = 0x273B,
    kBiosApostrophe = 0x2827,
    kBiosGrave = 0x2960,
    kBiosBackslash = 0x2B5C,
    kBiosComma = 0x332C,
    kBiosPeriod = 0x342E,
    kBiosSlash = 0x352F,
    kBiosSpace = 0x3920,
    kBiosEnter = 0x1C0D,
    kBiosBackspace = 0x0E08,
    kBiosTab = 0x0F09,
    kBiosF1 = 0x3B00,
    kBiosF2 = 0x3C00,
    kBiosF3 = 0x3D00,
    kBiosF4 = 0x3E00,
    kBiosF5 = 0x3F00,
    kBiosF6 = 0x4000,
    kBiosF7 = 0x4100,
    kBiosF8 = 0x4200,
    kBiosF9 = 0x4300,
    kBiosF10 = 0x4400,
    kBiosEscape = 0x011B,
    kRingStoredCapacity = 31,
    kRingOverflowAttempts = 40,
    kBlockingPushDelayMs = 5,
    kTestFailureExitCode = 1,
    kMenuMouseClick = INPUT_MENU_MOUSE_CLICK,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void drainSdlEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
    }
}

void resetInputState() {
    drainSdlEvents();
    setInt9Handler(); /* flight mode + empty ring + centred virtual stick */
}

void pushKey(SDL_Scancode scancode, SDL_Keymod modifiers = SDL_KMOD_NONE) {
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    event.key.mod = modifiers;
    SDL_PushEvent(&event);
}

void pushMouseMotion() {
    SDL_Event event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    SDL_PushEvent(&event);
}

void pushMouseClick(SDL_Window *window, float x, float y) {
    SDL_Event event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = SDL_GetWindowID(window);
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = x;
    event.button.y = y;
    SDL_PushEvent(&event);
}

void expectKey(SDL_Scancode scancode, SDL_Keymod modifiers, int expectedWord,
               const char *message) {
    resetInputState();
    pushKey(scancode, modifiers);
    require(kbhit() != 0 && egReadKey() == expectedWord, message);
}

} // namespace

int main() {
    test_headless_init();
    // Dummy video gives a valid (empty) keyboard-state array for the virtual
    // stick and pulls in the event subsystem the pump drains.
    require(SDL_Init(SDL_INIT_VIDEO), "SDL initializes headless for input behavior tests");

    resetInputState();
    require(g_joyRawX == kStickCenter && g_joyRawY == kStickCenter,
            "setInt9Handler recenters the raw keyboard-stick axes");

    pushKey(SDL_SCANCODE_R);
    require(kbhit() != 0, "kbhit reports a queued BIOS-style letter key");
    require(egReadKey() == kBiosR,
            "egReadKey returns the BIOS word for lowercase r");

    resetInputState();
    pushKey(SDL_SCANCODE_R, SDL_KMOD_SHIFT);
    require(kbhit() != 0 && egReadKey() == kBiosShiftR,
            "egReadKey returns the shifted BIOS word for R");

    resetInputState();
    pushKey(SDL_SCANCODE_R, SDL_KMOD_CTRL);
    require(kbhit() != 0 && egReadKey() == kBiosCtrlR,
            "egReadKey maps Ctrl+letter to the control-code low byte");

    resetInputState();
    pushKey(SDL_SCANCODE_Q, SDL_KMOD_ALT);
    require(kbhit() != 0 && egReadKey() == kBiosAltQ,
            "egReadKey maps Alt+Q to the scan-only BIOS word");

    const struct {
        SDL_Scancode scancode;
        int expectedWord;
    } letterCases[] = {
        {SDL_SCANCODE_A, kBiosA}, {SDL_SCANCODE_B, kBiosB}, {SDL_SCANCODE_C, kBiosC},
        {SDL_SCANCODE_D, kBiosD}, {SDL_SCANCODE_E, kBiosE}, {SDL_SCANCODE_F, kBiosF},
        {SDL_SCANCODE_G, kBiosG}, {SDL_SCANCODE_H, kBiosH}, {SDL_SCANCODE_I, kBiosI},
        {SDL_SCANCODE_J, kBiosJ}, {SDL_SCANCODE_K, kBiosK}, {SDL_SCANCODE_L, kBiosL},
        {SDL_SCANCODE_M, kBiosM}, {SDL_SCANCODE_N, kBiosN}, {SDL_SCANCODE_O, kBiosO},
        {SDL_SCANCODE_P, kBiosP}, {SDL_SCANCODE_Q, kBiosQ}, {SDL_SCANCODE_S, kBiosS},
        {SDL_SCANCODE_T, kBiosT}, {SDL_SCANCODE_U, kBiosU}, {SDL_SCANCODE_V, kBiosV},
        {SDL_SCANCODE_W, kBiosW}, {SDL_SCANCODE_X, kBiosX}, {SDL_SCANCODE_Y, kBiosY},
        {SDL_SCANCODE_Z, kBiosZ},
    };
    for (const auto &entry : letterCases) {
        expectKey(entry.scancode, SDL_KMOD_NONE, entry.expectedWord,
                  "egReadKey maps alphabet keys to the BIOS scan/ASCII word");
    }

    const struct {
        SDL_Scancode scancode;
        int expectedWord;
    } numberCases[] = {
        {SDL_SCANCODE_1, kBios1}, {SDL_SCANCODE_2, kBios2}, {SDL_SCANCODE_3, kBios3},
        {SDL_SCANCODE_4, kBios4}, {SDL_SCANCODE_5, kBios5}, {SDL_SCANCODE_6, kBios6},
        {SDL_SCANCODE_7, kBios7}, {SDL_SCANCODE_8, kBios8}, {SDL_SCANCODE_9, kBios9},
        {SDL_SCANCODE_0, kBios0},
    };
    for (const auto &entry : numberCases) {
        expectKey(entry.scancode, SDL_KMOD_NONE, entry.expectedWord,
                  "egReadKey maps number-row keys to the BIOS scan/ASCII word");
    }
    expectKey(SDL_SCANCODE_1, SDL_KMOD_SHIFT, kBiosShift1,
              "egReadKey maps shifted number-row punctuation to the BIOS word");

    const struct {
        SDL_Scancode scancode;
        int expectedWord;
    } punctuationCases[] = {
        {SDL_SCANCODE_MINUS, kBiosMinus},       {SDL_SCANCODE_EQUALS, kBiosEquals},
        {SDL_SCANCODE_LEFTBRACKET, kBiosLeftBracket},
        {SDL_SCANCODE_RIGHTBRACKET, kBiosRightBracket},
        {SDL_SCANCODE_SEMICOLON, kBiosSemicolon},
        {SDL_SCANCODE_APOSTROPHE, kBiosApostrophe},
        {SDL_SCANCODE_GRAVE, kBiosGrave},       {SDL_SCANCODE_BACKSLASH, kBiosBackslash},
        {SDL_SCANCODE_COMMA, kBiosComma},       {SDL_SCANCODE_PERIOD, kBiosPeriod},
        {SDL_SCANCODE_SLASH, kBiosSlash},       {SDL_SCANCODE_SPACE, kBiosSpace},
        {SDL_SCANCODE_RETURN, kBiosEnter},      {SDL_SCANCODE_BACKSPACE, kBiosBackspace},
        {SDL_SCANCODE_TAB, kBiosTab},           {SDL_SCANCODE_ESCAPE, kBiosEscape},
    };
    for (const auto &entry : punctuationCases) {
        expectKey(entry.scancode, SDL_KMOD_NONE, entry.expectedWord,
                  "egReadKey maps punctuation/control keys to the BIOS word");
    }

    const struct {
        SDL_Scancode scancode;
        int expectedWord;
    } functionCases[] = {
        {SDL_SCANCODE_F1, kBiosF1},   {SDL_SCANCODE_F2, kBiosF2},
        {SDL_SCANCODE_F3, kBiosF3},   {SDL_SCANCODE_F4, kBiosF4},
        {SDL_SCANCODE_F5, kBiosF5},   {SDL_SCANCODE_F6, kBiosF6},
        {SDL_SCANCODE_F7, kBiosF7},   {SDL_SCANCODE_F8, kBiosF8},
        {SDL_SCANCODE_F9, kBiosF9},   {SDL_SCANCODE_F10, kBiosF10},
    };
    for (const auto &entry : functionCases) {
        expectKey(entry.scancode, SDL_KMOD_NONE, entry.expectedWord,
                  "egReadKey maps function keys to the scan-only BIOS word");
    }

    resetInputState();
    pushKey(SDL_SCANCODE_PRINTSCREEN);
    require(kbhit() == 0,
            "egReadKey ignores keys that carry no egame command meaning");

    resetInputState();
    pushMouseMotion();
    require(kbhit() == 0,
            "egReadKey ignores non-key SDL events");

    SDL_Window *menuWindow = SDL_CreateWindow(
        "input behavior menu", 640, 400, SDL_WINDOW_HIDDEN);
    require(menuWindow != nullptr, "menu mouse test creates a hidden window");
    input_setMode(INPUT_MODE_MENU);
    input_ringReset();
    pushMouseClick(menuWindow, 620.0f, 380.0f);
    require(input_keyWaiting() && input_readKey() == kMenuMouseClick,
            "menu click wakes a legacy key-driven menu loop");
    int mouseX = 0;
    int mouseY = 0;
    require(input_takeMenuClick(&mouseX, &mouseY) &&
                mouseX == 310 && mouseY == 190,
            "menu click is exposed in logical 320x200 coordinates");
    require(!input_takeMenuClick(&mouseX, &mouseY),
            "menu click is consumed exactly once");
    SDL_DestroyWindow(menuWindow);
    resetInputState();

    resetInputState();
    std::thread delayedKey([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(kBlockingPushDelayMs));
        pushKey(SDL_SCANCODE_ESCAPE);
    });
    require(egReadKey() == kBiosEscape,
            "egReadKey blocks until a key is ready, BIOS-read style");
    delayedKey.join();

    // The flight pause loop must ignore another Alt-P and block for a real
    // resume key without advancing simulation timing while paused.
    resetInputState();
    g_frameTimingAccum = kPauseTimingValue;
    pushKey(SDL_SCANCODE_P, SDL_KMOD_ALT);
    pushKey(SDL_SCANCODE_ESCAPE);
    waitForKeyPress();
    require(g_frameTimingAccum == kPauseTimingValue,
            "waitForKeyPress ignores repeated Alt-P and restores frame timing");
    require(kbhit() == 0,
            "waitForKeyPress consumes the resume key after repeated Alt-P");

    resetInputState();
    pushKey(SDL_SCANCODE_RETURN, SDL_KMOD_ALT);
    require(kbhit() == 0,
            "Alt+Enter is swallowed as the fullscreen toggle before the key ring");

    resetInputState();
    for (int idx = 0; idx < kRingOverflowAttempts; ++idx) {
        pushKey(SDL_SCANCODE_ESCAPE);
    }
    require(kbhit() != 0, "kbhit drains a burst of events into the key ring");
    int readCount = 0;
    while (kbhit() != 0) {
        require(egReadKey() == kBiosEscape,
                "overflow burst preserves the queued ESC BIOS word");
        ++readCount;
    }
    require(readCount == kRingStoredCapacity,
            "key ring drops overflow after its one-empty-slot capacity");

    restoreInt9Handler();
    SDL_Quit();
    std::cout << "input_behavior_tests passed\n";
    return 0;
}
