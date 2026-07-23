#include "game_options.h"

/*
 * Process-local settings deliberately live outside the legacy communication
 * blocks and save files. This avoids changing reconstructed data layouts while
 * keeping the selected options active across front-end and flight phases.
 */
static bool g_options[GAME_OPTION_COUNT];

/* Return whether one optional gameplay assist is currently enabled. */
bool gameOptionsEnabled(enum GameOption option) {
    return option >= 0 && option < GAME_OPTION_COUNT && g_options[option];
}

/* Set one optional gameplay assist without changing the other settings. */
void gameOptionsSet(enum GameOption option, bool enabled) {
    if (option >= 0 && option < GAME_OPTION_COUNT) g_options[option] = enabled;
}

/* Toggle one optional gameplay assist and return its new state. */
bool gameOptionsToggle(enum GameOption option) {
    bool enabled = !gameOptionsEnabled(option);
    gameOptionsSet(option, enabled);
    return enabled;
}

/* Restore original gameplay by disabling every optional assist. */
void gameOptionsReset(void) {
    int option;
    for (option = 0; option < GAME_OPTION_COUNT; option++) {
        g_options[option] = false;
    }
}
