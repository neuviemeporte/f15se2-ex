#include "egcode.h"
#include "inttype.h"
#include "shared/blackbox.h"

#include <SDL3/SDL_timer.h>

#include <cstdlib>
#include <iostream>

/* The LINK_CORE test uses the live timer globals defined by stdata.c. Keep
 * declarations here because the translated data headers expose only the subset
 * used by original modules. */
extern uint8 timerCounter;
extern uint8 timerCounter2;
extern uint8 timerCounter3;
extern uint8 timerCounter4;
extern uint8 timerHandlerInstalled;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SharedTimerOriginalConstant : int {
    kTimerInstalled = 1,
    kTimerRestored = 0,
    kTargetTicks = 2,
    kMaxYieldPolls = 300,
    kNowNondecreasingSleepNs = 1,
    kCatchupResyncSleepMs = 300,
    kTestFailureExitCode = 1,
};

int g_hookCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetTimerState() {
    timerCounter = 0;
    timerCounter2 = 0;
    timerCounter3 = 0;
    timerCounter4 = 0;
    timerHandlerInstalled = 0;
    g_hookCalls = 0;
    setTimerTickHook(nullptr);
}

} // namespace

void far testTickHook(void) {
    ++g_hookCalls;
}

int main() {
    resetTimerState();
    setTimerTickHook(testTickHook);
    setTimerIrqHandler();
    require(timerHandlerInstalled == kTimerInstalled,
            "setTimerIrqHandler marks the original timer handler as installed");

    for (int polls = 0; timerCounter < kTargetTicks && polls < kMaxYieldPolls; ++polls) {
        timerYield();
    }
    require(timerCounter >= kTargetTicks &&
                timerCounter2 == timerCounter &&
                timerCounter3 == timerCounter &&
                timerCounter4 == timerCounter,
            "timerYield/timerPump advance all original timer counters together");
    require(g_hookCalls == timerCounter,
            "timerPump invokes the original per-tick hook once per advanced tick");

    resetTimerState();
    require(blackbox_startDebug(BLACKBOX_DEFAULT_SEED) != 0,
            "timer test can enable deterministic blackbox time");
    setTimerTickHook(testTickHook);
    setTimerIrqHandler();
    timerPump();
    require(blackbox_tick() == 1 && timerCounter == 1 && timerCounter2 == 1 &&
                timerCounter3 == 1 && timerCounter4 == 1 && g_hookCalls == 1,
            "blackbox timer pump advances exactly one deterministic tick and hook");
    blackbox_shutdown();

    resetTimerState();
    setTimerTickHook(testTickHook);
    setTimerIrqHandler();
    SDL_DelayNS(static_cast<Uint64>(kCatchupResyncSleepMs) * SDL_NS_PER_MS);
    timerPump();
    require(timerCounter >= 1 && g_hookCalls == timerCounter,
            "timerPump preserves original catch-up resync behavior after a long stall");
    restoreTimerIrqHandler();

    {
        const uint64 before = timerNowNs();
        SDL_DelayNS(kNowNondecreasingSleepNs);
        const uint64 after = timerNowNs();
        require(after >= before,
                "timerNowNs preserves the original monotonic native timer source");
    }

    restoreTimerIrqHandler();
    require(timerHandlerInstalled == kTimerRestored,
            "restoreTimerIrqHandler clears the original installed flag");

    std::cout << "shared_timer_behavior_tests passed\n";
    return 0;
}
