#include "slot.h"
#include "const.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SharedOverlayOriginalConstant : int {
    kNoJoystickMovement = 0,
    kNoAudioError = 0,
    kJoystickAxisX = 0,
    kJoystickAxisY = 1,
    kAudioSampleSegment = 0x1234,
    kAudioVariantSelector = 3,
    kKeyAvailable = 0,
    kKeyBufferEmpty = 0xFFFF,
    kExpectedOnePump = 1,
    kExpectedTwoPumps = 2,
    kExpectedBlockingGetKeyPumps = 3,
    kBiosEnterScanWord = 0x1c00,
    kBiosEscapeScanWord = 0x0100,
    kBiosBackspaceScanWord = 0x0e00,
    kCtrlXAscii = 0x18,
    kPrintableTextA = 'A',
    kTestFailureExitCode = 1,
};

int g_timerPumpCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

} // namespace

void timerPump(void) {
    ++g_timerPumpCalls;
}

int main() {
    require(SDL_Init(SDL_INIT_EVENTS),
            "SDL event subsystem initializes for overlay keyboard behavior tests");

    require(misc_readJoystick(kJoystickAxisX) == kNoJoystickMovement &&
                misc_readJoystick(kJoystickAxisY) == kNoJoystickMovement,
            "misc_readJoystick preserves the original no-joystick overlay behavior");
    require(audio_setup(kAudioSampleSegment, kAudioVariantSelector) == kNoAudioError,
            "audio_setup preserves the original no-op audio overlay return");
    require(audio_shutdown() == kNoAudioError,
            "audio_shutdown preserves the original no-op audio overlay return");
    require(audio_playIntro() == kNoAudioError,
            "audio_playIntro preserves the original no-op audio overlay return");

    g_timerPumpCalls = 0;
    misc_clearKeyFlags();
    require(g_timerPumpCalls == kExpectedOnePump,
            "misc_clearKeyFlags pumps input once before clearing the original BIOS-style key buffer");

    g_timerPumpCalls = 0;
    require(misc_checkKeyBuf() == kKeyBufferEmpty &&
                g_timerPumpCalls == kExpectedOnePump,
            "misc_checkKeyBuf reports the original empty-buffer sentinel and advances the timer pump");

    SDL_Event upEvent = {};
    upEvent.type = SDL_EVENT_KEY_DOWN;
    upEvent.key.scancode = SDL_SCANCODE_UP;
    upEvent.key.key = SDLK_UP;
    SDL_PushEvent(&upEvent);
    g_timerPumpCalls = 0;
    require(misc_checkKeyBuf() == kKeyAvailable &&
                g_timerPumpCalls == kExpectedOnePump,
            "misc_checkKeyBuf reports a queued original BIOS arrow key");
    require(misc_getKey() == KEYCODE_UPARROW &&
                g_timerPumpCalls == kExpectedTwoPumps,
            "misc_getKey returns the original scan-code word for an up-arrow event");

    SDL_Event downEvent = {};
    downEvent.type = SDL_EVENT_KEY_DOWN;
    downEvent.key.scancode = SDL_SCANCODE_DOWN;
    downEvent.key.key = SDLK_DOWN;
    SDL_PushEvent(&downEvent);
    require(misc_getKey() == KEYCODE_DNARROW,
            "misc_getKey returns the original scan-code word for a down-arrow event");

    SDL_Event leftEvent = {};
    leftEvent.type = SDL_EVENT_KEY_DOWN;
    leftEvent.key.scancode = SDL_SCANCODE_LEFT;
    leftEvent.key.key = SDLK_LEFT;
    SDL_PushEvent(&leftEvent);
    require(misc_getKey() == KEYCODE_LEFTARROW,
            "misc_getKey returns the original scan-code word for a left-arrow event");

    SDL_Event rightEvent = {};
    rightEvent.type = SDL_EVENT_KEY_DOWN;
    rightEvent.key.scancode = SDL_SCANCODE_RIGHT;
    rightEvent.key.key = SDLK_RIGHT;
    SDL_PushEvent(&rightEvent);
    require(misc_getKey() == KEYCODE_RIGHTARROW,
            "misc_getKey returns the original scan-code word for a right-arrow event");

    SDL_Event quitEvent = {};
    quitEvent.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quitEvent);
    require(misc_getKey() == KEYCODE_ALTQ,
            "misc_getKey maps SDL quit to the original Alt-Q quit key word");

    SDL_Event enterEvent = {};
    enterEvent.type = SDL_EVENT_KEY_DOWN;
    enterEvent.key.scancode = SDL_SCANCODE_RETURN;
    enterEvent.key.key = SDLK_RETURN;
    SDL_PushEvent(&enterEvent);
    require(misc_getKey() == (kBiosEnterScanWord | KEYCODE_ENTER),
            "misc_getKey returns the original BIOS word for enter");

    SDL_Event keypadEnterEvent = {};
    keypadEnterEvent.type = SDL_EVENT_KEY_DOWN;
    keypadEnterEvent.key.scancode = SDL_SCANCODE_KP_ENTER;
    keypadEnterEvent.key.key = SDLK_KP_ENTER;
    SDL_PushEvent(&keypadEnterEvent);
    require(misc_getKey() == (kBiosEnterScanWord | KEYCODE_ENTER),
            "misc_getKey maps keypad enter to the same original BIOS enter word");

    SDL_Event escapeEvent = {};
    escapeEvent.type = SDL_EVENT_KEY_DOWN;
    escapeEvent.key.scancode = SDL_SCANCODE_ESCAPE;
    escapeEvent.key.key = SDLK_ESCAPE;
    SDL_PushEvent(&escapeEvent);
    require(misc_getKey() == (kBiosEscapeScanWord | KEYCODE_ESC),
            "misc_getKey returns the original BIOS word for escape");

    SDL_Event backspaceEvent = {};
    backspaceEvent.type = SDL_EVENT_KEY_DOWN;
    backspaceEvent.key.scancode = SDL_SCANCODE_BACKSPACE;
    backspaceEvent.key.key = SDLK_BACKSPACE;
    SDL_PushEvent(&backspaceEvent);
    require(misc_getKey() == (kBiosBackspaceScanWord | KEYCODE_BACKSPACE),
            "misc_getKey returns the original BIOS word for backspace");

    SDL_Event ctrlXEvent = {};
    ctrlXEvent.type = SDL_EVENT_KEY_DOWN;
    ctrlXEvent.key.scancode = SDL_SCANCODE_X;
    ctrlXEvent.key.key = SDLK_X;
    ctrlXEvent.key.mod = SDL_KMOD_CTRL;
    SDL_PushEvent(&ctrlXEvent);
    require(misc_getKey() == kCtrlXAscii,
            "misc_getKey synthesizes the original Ctrl-letter ASCII code");

    SDL_Event altQEvent = {};
    altQEvent.type = SDL_EVENT_KEY_DOWN;
    altQEvent.key.scancode = SDL_SCANCODE_Q;
    altQEvent.key.key = SDLK_Q;
    altQEvent.key.mod = SDL_KMOD_ALT;
    SDL_PushEvent(&altQEvent);
    require(misc_getKey() == KEYCODE_ALTQ,
            "misc_getKey returns the original Alt-Q quit key word");

    SDL_Event textEvent = {};
    textEvent.type = SDL_EVENT_TEXT_INPUT;
    const char textInput[] = "A\n";
    textEvent.text.text = textInput;
    SDL_PushEvent(&textEvent);
    require(misc_getKey() == kPrintableTextA,
            "misc_getKey queues printable SDL text input bytes in the original ASCII slot");

    SDL_Event ignoredEvent = {};
    ignoredEvent.type = SDL_EVENT_MOUSE_MOTION;
    SDL_PushEvent(&ignoredEvent);
    require(misc_checkKeyBuf() == kKeyBufferEmpty,
            "misc_checkKeyBuf ignores unrelated SDL events like the original empty BIOS buffer");

    g_timerPumpCalls = 0;
    std::thread delayedKey([] {
        SDL_Delay(5);
        SDL_Event delayedEscape = {};
        delayedEscape.type = SDL_EVENT_KEY_DOWN;
        delayedEscape.key.scancode = SDL_SCANCODE_ESCAPE;
        delayedEscape.key.key = SDLK_ESCAPE;
        SDL_PushEvent(&delayedEscape);
    });
    require(misc_getKey() == (kBiosEscapeScanWord | KEYCODE_ESC) &&
                g_timerPumpCalls >= kExpectedBlockingGetKeyPumps,
            "misc_getKey preserves the original blocking wait until a BIOS-style key arrives");
    delayedKey.join();

    SDL_QuitSubSystem(SDL_INIT_EVENTS);

    std::cout << "shared_overlay_behavior_tests passed\n";
    return 0;
}
