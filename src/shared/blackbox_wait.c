#include "blackbox_wait.h"

#include "blackbox.h"

#include <SDL3/SDL.h>

int blackbox_virtualWait(uint32 durationTicks, int stopOnInput,
                         BlackboxPollInputFn pollInput) {
    uint32 startTick;
    /* Record and replay must execute the same polling path. Record-mode timer
     * pumps are native 60 Hz, so this remains real-time without making the
     * number of input polls depend on a separate host-clock wait loop. */
    if (!blackbox_enabled()) return 0;

    startTick = blackbox_tick();
    while (blackbox_tick() - startTick < durationTicks) {
        if (pollInput && pollInput() && stopOnInput) break;
        /* Keep the host responsive without using elapsed host time as state. */
        if (!blackbox_fastForwarding()) SDL_DelayNS(2 * SDL_NS_PER_MS);
    }
    return 1;
}
