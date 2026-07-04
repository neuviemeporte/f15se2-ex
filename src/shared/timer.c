/*
 * timer.c - Game tick clock for the native SDL build.
 *
 * The game keeps time in four byte counters that callers reset to 0 and then
 * poll until they reach some target ("wait N ticks"), plus an optional per-tick
 * hook egame uses for frame pacing and palette cycling. The tick rate is 60 Hz.
 *
 * Rather than a background thread, the counters advance on the main thread from
 * a monotonic clock: timerPump() adds however many 60 Hz ticks have elapsed
 * since it last ran and fires the hook once per tick. This keeps the hook's work
 * (palette cycle, frame-sync flags) synchronized with rendering instead of
 * racing it. timerPump() is called from the input pump and from timerYield(),
 * so every wait loop that polls a counter advances time as it spins.
 */

#include "inttype.h"
#include <dos.h>
#include <SDL3/SDL.h>

extern uint8 timerCounter;
extern uint8 timerCounter2;
extern uint8 timerCounter3;
extern uint8 timerCounter4;
extern uint8 timerHandlerInstalled;

#define TIMER_HZ 60
#define TICK_NS (SDL_NS_PER_SECOND / TIMER_HZ)
/* If the loop falls this far behind (e.g. the window was blocked), snap forward
 * instead of firing a burst of catch-up ticks. */
#define MAX_CATCHUP_NS (SDL_NS_PER_SECOND / 4)

/* Optional per-tick game callback. egame registers egAdvanceFrameTick here (the
 * frame-sync + DAC colour-cycle tick); NULL for start/end, which need no extra
 * per-tick work. */
static void(far *gameTickHook)(void) = 0;

static Uint64 nextTickNs; /* clock time the next pending tick is due */
static bool timerRunning = false;

void setTimerTickHook(void(far *fn)(void)) {
    gameTickHook = fn;
}

/* Advance the tick counters to match elapsed real time, firing the hook once
 * per 1/60 s tick. Safe to call as often as a spin loop likes. */
void timerPump(void) {
    Uint64 now;
    if (!timerRunning) return;
    now = SDL_GetTicksNS();
    if (now > nextTickNs + MAX_CATCHUP_NS)
        nextTickNs = now; /* fell too far behind: resync, don't burst */
    while (now >= nextTickNs) {
        nextTickNs += TICK_NS;
        timerCounter++;
        timerCounter2++;
        timerCounter3++;
        timerCounter4++;
        if (gameTickHook != 0)
            gameTickHook();
    }
}

/* Monotonic clock in nanoseconds, for the render loop's fixed-timestep sim
 * pacing and interpolation alpha (same clock timerPump advances the 60 Hz ticks
 * from, so sim cadence and tick counters stay coherent). */
uint64 timerNowNs(void) {
    return SDL_GetTicksNS();
}

/* Body for the game's "wait until a counter reaches N" spin loops: advance the
 * clock and yield the CPU so the wait doesn't peg a core. */
void timerYield(void) {
    timerPump();
    SDL_DelayNS(SDL_NS_PER_MS);
}

void setTimerIrqHandler(void) {
    nextTickNs = SDL_GetTicksNS() + TICK_NS;
    timerRunning = true;
    timerHandlerInstalled = 1;
}

void restoreTimerIrqHandler(void) {
    timerRunning = false;
    timerHandlerInstalled = 0;
}
