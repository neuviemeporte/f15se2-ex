#ifndef F15_SE2_GAME_OPTIONS
#define F15_SE2_GAME_OPTIONS

#include <stdbool.h>

/*
 * Optional gameplay assists. All values default to disabled so an untouched
 * installation behaves exactly like the original game.
 */
enum GameOption {
    GAME_OPTION_INFINITE_WEAPONS,
    GAME_OPTION_INFINITE_FUEL,
    GAME_OPTION_NO_DAMAGE,
    GAME_OPTION_COUNT
};

/* Return whether one optional gameplay assist is currently enabled. */
bool gameOptionsEnabled(enum GameOption option);

/* Set one optional gameplay assist without changing the other settings. */
void gameOptionsSet(enum GameOption option, bool enabled);

/* Toggle one optional gameplay assist and return its new state. */
bool gameOptionsToggle(enum GameOption option);

/* Restore original gameplay by disabling every optional assist. */
void gameOptionsReset(void);

#endif /* F15_SE2_GAME_OPTIONS */
