#include "menu_pointer.h"
#include "input.h"
#include "stdata.h"
#include "const.h"
#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>

static void require(bool ok, const char *message) {
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}

static int click(int x, int y) {
    SDL_Event event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = (float)x;
    event.button.y = (float)y;
    require(SDL_PushEvent(&event), "queue pointer release");
    require(input_keyWaiting(), "pointer enters real input ring");
    return input_readKey();
}

int main() {
    test_headless_init();
    require(SDL_Init(SDL_INIT_VIDEO), "SDL init");
    gfx_videoInit();
    gfx_initState();
    gfx_setMode13();
    input_setMode(INPUT_MODE_MENU);
    input_ringReset();
    int16 page[12] = {};
    screenBuf = page;
    int pending = -1;
    selectedPilotIdx = 0;
    const int top = PILOT_TOP_MARGIN;
    require(menu_pilotPointerInput(click(PILOT_COL_RIGHT, top), &pending) == 0,
            "first click selects without confirmation");
    require(selectedPilotIdx == PILOTS_PER_COLUMN && pending == selectedPilotIdx,
            "right column selects matching pilot");
    require(menu_pilotPointerInput(click(PILOT_COL_RIGHT, top), &pending) == KEYCODE_ENTER,
            "second click confirms pilot");
    require(menu_pilotPointerInput(KEYCODE_UPARROW, &pending) == KEYCODE_UPARROW && pending == -1,
            "keyboard passes through and cancels pending click");
    require(menu_pilotPointerInput(click(0, top), &pending) == 0 && pending == -1,
            "outside pilot columns ignored");
    require(menu_pilotPointerInput(click(PILOT_COL_LEFT, 0), &pending) == 0,
            "above pilot rows ignored");
    require(menu_pilotPointerInput(click(PILOT_COL_LEFT, 199), &pending) == 0,
            "below pilot rows ignored");
    require(menu_pilotPointerInput(click(PILOT_COL_LEFT, top), &pending) == 0 && selectedPilotIdx == 0,
            "left column selection uses original highlight path");
    require(menu_pilotPointerInput(INPUT_KEY_MENU_POINTER, &pending) == 0,
            "coordinates cannot be consumed twice");

    int selection = 0;
    pending = -1;
    scenarioFoundArr[0] = 0;
    scenarioFoundArr[1] = 1;
    require(menu_missionPointerInput(click(105, 24), &selection, &pending) == 0 && pending == 0,
            "mission first click selects");
    require(menu_missionPointerInput(click(105, 24), &selection, &pending) == KEYCODE_ENTER,
            "mission second click confirms");
    require(menu_missionPointerInput(KEYCODE_DNARROW, &selection, &pending) == KEYCODE_DNARROW && pending == -1,
            "mission keyboard handling unchanged");
    require(menu_missionPointerInput(click(105, 45), &selection, &pending) == 0 && selection == 0,
            "disabled scenario ignored");
    require(menu_missionPointerInput(click(104, 24), &selection, &pending) == 0,
            "left of mission hitbox ignored");
    require(menu_missionPointerInput(click(105, 23), &selection, &pending) == 0,
            "above mission hitbox ignored");
    require(menu_missionPointerInput(click(105, 129), &selection, &pending) == 0,
            "exclusive bottom of mission hitbox ignored");
    require(menu_missionPointerInput(INPUT_KEY_MENU_POINTER, &selection, &pending) == 0,
            "missing mission coordinates ignored");
    gfx_videoShutdown();
    SDL_Quit();
    std::cout << "menu_pointer_behavior_tests passed\n";
}
