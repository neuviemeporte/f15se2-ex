#include "game_options.h"

#include <cstdlib>
#include <iostream>

namespace {

/* Stop immediately with a useful message when an option-state contract fails. */
void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    gameOptionsReset();
    for (int option = 0; option < GAME_OPTION_COUNT; option++) {
        require(!gameOptionsEnabled((enum GameOption)option),
                "all gameplay options preserve current behavior by default");
    }

    require(gameOptionsToggle(GAME_OPTION_INFINITE_WEAPONS),
            "toggle enables infinite weapons");
    require(gameOptionsEnabled(GAME_OPTION_INFINITE_WEAPONS),
            "infinite weapons remains enabled");
    require(!gameOptionsEnabled(GAME_OPTION_INFINITE_FUEL),
            "changing weapons does not change fuel");
    require(!gameOptionsToggle(GAME_OPTION_INFINITE_WEAPONS),
            "second toggle disables infinite weapons");

    gameOptionsSet(GAME_OPTION_INFINITE_FUEL, true);
    gameOptionsSet(GAME_OPTION_NO_DAMAGE, true);
    require(gameOptionsEnabled(GAME_OPTION_INFINITE_FUEL) &&
                gameOptionsEnabled(GAME_OPTION_NO_DAMAGE),
            "fuel and damage assists can be enabled independently");

    gameOptionsReset();
    require(!gameOptionsEnabled(GAME_OPTION_INFINITE_FUEL) &&
                !gameOptionsEnabled(GAME_OPTION_NO_DAMAGE),
            "reset restores original gameplay settings");
    require(!gameOptionsEnabled((enum GameOption)-1) &&
                !gameOptionsEnabled(GAME_OPTION_COUNT),
            "out-of-range option queries are safely disabled");

    std::cout << "game_options_behavior_tests passed\n";
    return 0;
}
