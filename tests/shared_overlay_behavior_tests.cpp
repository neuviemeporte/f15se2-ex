// LINK_CORE + headless. Exercises the real MISC/audio "overlay slot" wrappers
// (slot.h): the menu keyboard slots now forward to the single SDL event pump in
// input.c, the joystick fire-button slot lives in joystick.c, and audio_* front
// the SDL/OPL backend. No display/audio hardware is needed - the dummy drivers
// and a driverless event queue are enough.
#include "slot.h"
#include "const.h"
#include "input.h"
#include "headless.h"
#include "egcode.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SharedOverlayOriginalConstant : int {
    kNoJoystickButton = 0,
    kNoAudioError = 0,
    kFireButtonGuns = 0,
    kFireButtonMissiles = 1,
    kAudioSampleSegment = 0x1234,
    kAudioVariantSelector = 3,
    kKeyAvailable = 0,
    kKeyBufferEmpty = 0xFFFF,
    kBiosEnterScanWord = 0x1c00,
    kBiosEscapeScanWord = 0x0100,
    kBiosBackspaceScanWord = 0x0e00,
    kCtrlXAscii = 0x18,
    kPrintableTextA = 'A',
    kBlockingPushDelayMs = 5,
    kReplacementCueSpanBytes = 0x7d9e,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void writePcm8Wav(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    const unsigned sampleRate = 7850;
    const unsigned dataSize = 4;
    const unsigned riffSize = 36 + dataSize;
    std::ofstream out(path, std::ios::binary);
    auto put = [&](unsigned value, int bytes) {
        for (int i = 0; i < bytes; i++) out.put(static_cast<char>((value >> (8 * i)) & 0xff));
    };
    out.write("RIFF", 4);
    put(riffSize, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);          // PCM fmt chunk size
    put(1, 2);           // PCM
    put(1, 2);           // mono
    put(sampleRate, 4);
    put(sampleRate, 4);  // byte rate: mono 8-bit
    put(1, 2);           // block align
    put(8, 2);           // bits per sample
    out.write("data", 4);
    put(dataSize, 4);
    const char samples[4] = {static_cast<char>(0x80), static_cast<char>(0x90), static_cast<char>(0x70), static_cast<char>(0x80)};
    out.write(samples, sizeof(samples));
}

} // namespace

int main() {
    const auto replacementRoot = std::filesystem::temp_directory_path() / "f15se2-ex-audio-replacements";
    std::filesystem::remove_all(replacementRoot);
    writePcm8Wav(replacementRoot / "converted_assets_all" / "sounds" / "voice_cue_000_sample0.wav");
#if !defined(_WIN32)
    setenv("F15_REPLACEMENT_ROOT", replacementRoot.string().c_str(), 1);
#endif

    test_headless_init();
    require(SDL_Init(SDL_INIT_EVENTS),
            "SDL event subsystem initializes for overlay keyboard behavior tests");

    // Joystick fire-button slot with no controller attached (dummy driver): in
    // flight the buttons read as not-pressed, and in menu mode the slot is inert
    // (menu accept/cancel is edge-handled by the event pump, not this slot).
    input_setMode(INPUT_MODE_FLIGHT);
    require(misc_readJoystick(kFireButtonGuns) == kNoJoystickButton &&
                misc_readJoystick(kFireButtonMissiles) == kNoJoystickButton,
            "misc_readJoystick reports no fire buttons with no joystick attached");
    input_setMode(INPUT_MODE_MENU);
    require(misc_readJoystick(kFireButtonGuns) == kNoJoystickButton,
            "misc_readJoystick stays inert in menu mode");

    require(audio_setup(kAudioSampleSegment, kAudioVariantSelector) == kNoAudioError,
            "audio_setup returns the slot-ABI success code");
    require(audio_shutdown() == kNoAudioError,
            "audio_shutdown returns the slot-ABI success code");
    require(loadF15DgtlBin() == kReplacementCueSpanBytes,
            "loadF15DgtlBin accepts separate WAV cue replacements without F15DGTL.BIN");

    misc_clearKeyFlags();
    require(misc_checkKeyBuf() == kKeyBufferEmpty,
            "misc_checkKeyBuf reports the empty-buffer sentinel after a ring clear");

    // clearKeyFlags empties an already-queued key.
    SDL_Event probeEvent = {};
    probeEvent.type = SDL_EVENT_KEY_DOWN;
    probeEvent.key.scancode = SDL_SCANCODE_UP;
    probeEvent.key.key = SDLK_UP;
    SDL_PushEvent(&probeEvent);
    require(misc_checkKeyBuf() == kKeyAvailable,
            "misc_checkKeyBuf reports a queued BIOS arrow key");
    misc_clearKeyFlags();
    require(misc_checkKeyBuf() == kKeyBufferEmpty,
            "misc_clearKeyFlags drops the queued key from the ring");

    SDL_Event upEvent = {};
    upEvent.type = SDL_EVENT_KEY_DOWN;
    upEvent.key.scancode = SDL_SCANCODE_UP;
    upEvent.key.key = SDLK_UP;
    SDL_PushEvent(&upEvent);
    require(misc_getKey() == KEYCODE_UPARROW,
            "misc_getKey returns the scan-code word for an up-arrow event");

    SDL_Event downEvent = {};
    downEvent.type = SDL_EVENT_KEY_DOWN;
    downEvent.key.scancode = SDL_SCANCODE_DOWN;
    downEvent.key.key = SDLK_DOWN;
    SDL_PushEvent(&downEvent);
    require(misc_getKey() == KEYCODE_DNARROW,
            "misc_getKey returns the scan-code word for a down-arrow event");

    SDL_Event leftEvent = {};
    leftEvent.type = SDL_EVENT_KEY_DOWN;
    leftEvent.key.scancode = SDL_SCANCODE_LEFT;
    leftEvent.key.key = SDLK_LEFT;
    SDL_PushEvent(&leftEvent);
    require(misc_getKey() == KEYCODE_LEFTARROW,
            "misc_getKey returns the scan-code word for a left-arrow event");

    SDL_Event rightEvent = {};
    rightEvent.type = SDL_EVENT_KEY_DOWN;
    rightEvent.key.scancode = SDL_SCANCODE_RIGHT;
    rightEvent.key.key = SDLK_RIGHT;
    SDL_PushEvent(&rightEvent);
    require(misc_getKey() == KEYCODE_RIGHTARROW,
            "misc_getKey returns the scan-code word for a right-arrow event");

    SDL_Event enterEvent = {};
    enterEvent.type = SDL_EVENT_KEY_DOWN;
    enterEvent.key.scancode = SDL_SCANCODE_RETURN;
    enterEvent.key.key = SDLK_RETURN;
    SDL_PushEvent(&enterEvent);
    require(misc_getKey() == (kBiosEnterScanWord | KEYCODE_ENTER),
            "misc_getKey returns the BIOS word for enter");

    SDL_Event keypadEnterEvent = {};
    keypadEnterEvent.type = SDL_EVENT_KEY_DOWN;
    keypadEnterEvent.key.scancode = SDL_SCANCODE_KP_ENTER;
    keypadEnterEvent.key.key = SDLK_KP_ENTER;
    SDL_PushEvent(&keypadEnterEvent);
    require(misc_getKey() == (kBiosEnterScanWord | KEYCODE_ENTER),
            "misc_getKey maps keypad enter to the same BIOS enter word");

    SDL_Event escapeEvent = {};
    escapeEvent.type = SDL_EVENT_KEY_DOWN;
    escapeEvent.key.scancode = SDL_SCANCODE_ESCAPE;
    escapeEvent.key.key = SDLK_ESCAPE;
    SDL_PushEvent(&escapeEvent);
    require(misc_getKey() == (kBiosEscapeScanWord | KEYCODE_ESC),
            "misc_getKey returns the BIOS word for escape");

    SDL_Event backspaceEvent = {};
    backspaceEvent.type = SDL_EVENT_KEY_DOWN;
    backspaceEvent.key.scancode = SDL_SCANCODE_BACKSPACE;
    backspaceEvent.key.key = SDLK_BACKSPACE;
    SDL_PushEvent(&backspaceEvent);
    require(misc_getKey() == (kBiosBackspaceScanWord | KEYCODE_BACKSPACE),
            "misc_getKey returns the BIOS word for backspace");

    SDL_Event ctrlXEvent = {};
    ctrlXEvent.type = SDL_EVENT_KEY_DOWN;
    ctrlXEvent.key.scancode = SDL_SCANCODE_X;
    ctrlXEvent.key.key = SDLK_X;
    ctrlXEvent.key.mod = SDL_KMOD_CTRL;
    SDL_PushEvent(&ctrlXEvent);
    require(misc_getKey() == kCtrlXAscii,
            "misc_getKey synthesizes the Ctrl-letter control code");

    SDL_Event altQEvent = {};
    altQEvent.type = SDL_EVENT_KEY_DOWN;
    altQEvent.key.scancode = SDL_SCANCODE_Q;
    altQEvent.key.key = SDLK_Q;
    altQEvent.key.mod = SDL_KMOD_ALT;
    SDL_PushEvent(&altQEvent);
    require(misc_getKey() == KEYCODE_ALTQ,
            "misc_getKey returns the Alt-Q quit key word");

    SDL_Event textEvent = {};
    textEvent.type = SDL_EVENT_TEXT_INPUT;
    const char textInput[] = "A\n";
    textEvent.text.text = textInput;
    SDL_PushEvent(&textEvent);
    require(misc_getKey() == kPrintableTextA,
            "misc_getKey queues printable SDL text input bytes in the ASCII slot");

    SDL_Event ignoredEvent = {};
    ignoredEvent.type = SDL_EVENT_MOUSE_MOTION;
    SDL_PushEvent(&ignoredEvent);
    require(misc_checkKeyBuf() == kKeyBufferEmpty,
            "misc_checkKeyBuf ignores unrelated SDL events");

    std::thread delayedKey([] {
        SDL_Delay(kBlockingPushDelayMs);
        SDL_Event delayedEscape = {};
        delayedEscape.type = SDL_EVENT_KEY_DOWN;
        delayedEscape.key.scancode = SDL_SCANCODE_ESCAPE;
        delayedEscape.key.key = SDLK_ESCAPE;
        SDL_PushEvent(&delayedEscape);
    });
    require(misc_getKey() == (kBiosEscapeScanWord | KEYCODE_ESC),
            "misc_getKey blocks until a key arrives");
    delayedKey.join();

    SDL_QuitSubSystem(SDL_INIT_EVENTS);

#if !defined(_WIN32)
    unsetenv("F15_REPLACEMENT_ROOT");
#endif
    std::filesystem::remove_all(replacementRoot);

    std::cout << "shared_overlay_behavior_tests passed\n";
    return 0;
}
