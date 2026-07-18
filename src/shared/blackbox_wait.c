#include "blackbox_wait.h"

#include "blackbox.h"

#include <SDL3/SDL.h>

int blackbox_virtualWait(uint32 durationTicks, int stopOnInput,
                         BlackboxPollInputFn pollInput) {
    uint32 startTick;
    if (!blackbox_enabled()) return 0;

    startTick = blackbox_tick();
    while (blackbox_tick() - startTick < durationTicks) {
        if (pollInput && pollInput() && stopOnInput) break;
        /* Keep the host responsive without using elapsed host time as state. */
        if (!blackbox_fastForwarding()) SDL_DelayNS(2 * SDL_NS_PER_MS);
    }
    return 1;
}
