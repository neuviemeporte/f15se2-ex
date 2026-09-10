#include "comm.h"
#include "game_options.h"
#include "gfx.h"
#include "headless.h"
#include "input.h"
#include "stdata.h"
#include "stoptions.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <thread>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void pushKey(SDL_Scancode scancode) {
    SDL_Event event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = SDL_EVENT_KEY_DOWN;
    event.key.scancode = scancode;
    event.key.key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, true);
    SDL_PushEvent(&event);
}

void pushSpaceText(void) {
    SDL_Event event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.type = SDL_EVENT_TEXT_INPUT;
    event.text.text = " ";
    SDL_PushEvent(&event);
}

void pushExpose(void) {
    SDL_Event event = {};
    event.type = SDL_EVENT_WINDOW_EXPOSED;
    event.window.type = SDL_EVENT_WINDOW_EXPOSED;
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

} // namespace

int main() {
    struct GameComm comm = {};

    test_headless_init();
    commData = &comm;
    gfx_videoInit();
    gfx_setMode13();
    input_setMode(INPUT_MODE_MENU);

    require(stOptionsGearHit(302, 183) && stOptionsGearHit(318, 198),
            "gear hit target includes both visible corners");
    require(!stOptionsGearHit(301, 183) && !stOptionsGearHit(318, 199),
            "gear hit target excludes points outside the icon");
    stOptionsDrawGear(screenBuf);

    gameOptionsReset();
    std::thread keyboardInput([] {
        const SDL_Scancode keys[] = {
            SDL_SCANCODE_SPACE, SDL_SCANCODE_DOWN, SDL_SCANCODE_RIGHT,
            SDL_SCANCODE_DOWN, SDL_SCANCODE_SPACE, SDL_SCANCODE_UP,
            SDL_SCANCODE_LEFT, SDL_SCANCODE_ESCAPE};

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        pushExpose();
        for (SDL_Scancode key : keys) {
            if (key == SDL_SCANCODE_SPACE)
                pushSpaceText();
            else
                pushKey(key);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    stOptionsShow("TEST PILOT");
    keyboardInput.join();
    require(gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS),
            "Space toggles the selected weapons option");
    require(!gameOptionsEnabled(GAME_OPTION_INFINITE_FUEL),
            "left arrow disables the selected fuel option");
    require(gameOptionsEnabled(GAME_OPTION_NO_DAMAGE),
            "Space toggles the selected no-damage option");

    SDL_Window *mouseWindow = SDL_CreateWindow(
        "options mouse behavior", 640, 400, SDL_WINDOW_HIDDEN);
    require(mouseWindow != nullptr, "mouse behavior test creates a hidden window");
    gameOptionsReset();
    input_ringReset();
    std::thread mouseInput([mouseWindow] {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        pushMouseClick(mouseWindow, 300.0f, 140.0f);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        pushKey(SDL_SCANCODE_ESCAPE);
    });
    stOptionsShow("MOUSE PILOT");
    mouseInput.join();
    require(gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS),
            "clicking an option row toggles it");
    SDL_DestroyWindow(mouseWindow);

    gfx_videoShutdown();
    std::cout << "stoptions_behavior_tests passed\n";
    return 0;
}
