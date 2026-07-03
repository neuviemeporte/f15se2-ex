#include "egcode.h"
#include "egdata.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern void far egAdvanceFrameTick(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum TickOriginalConstant : int {
    kFrameSyncPendingBeforeTick = 1,
    kTimerTickBefore = 0x7E,
    kTimerTickAfter = 0x7F,
    kFrameTimingBefore = 0x1234,
    kFrameTimingAfter = 0x1235,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetTickState() {
    std::memset(g_timerTickByte, 0, sizeof(g_timerTickByte));
    g_frameSyncPending = kFrameSyncPendingBeforeTick;
    g_timerTickByte[0] = kTimerTickBefore;
    g_frameTimingAccum = kFrameTimingBefore;
}

} // namespace

int main() {
    resetTickState();
    egAdvanceFrameTick();

    require(g_frameSyncPending == 0,
            "egAdvanceFrameTick clears the original per-frame sync flag");
    require(g_timerTickByte[0] == kTimerTickAfter,
            "egAdvanceFrameTick increments the original waitFrameSync tick byte");
    require(g_frameTimingAccum == kFrameTimingAfter,
            "egAdvanceFrameTick increments the original frame timing accumulator");

    std::cout << "tick_behavior_tests passed\n";
    return 0;
}
