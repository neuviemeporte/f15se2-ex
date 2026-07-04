#include "common.h"

#include <dos.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

uint8 timerHandlerInstalled = 0;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SharedRuntimeOriginalConstant : int {
    kDrawX = 106,
    kDrawY = 1,
    kDrawPageSlotX = 4,
    kDrawPageSlotY = 5,
    kTimerNotInstalled = 0,
    kTimerInstalled = 1,
    kExpectedNoCalls = 0,
    kExpectedOneCall = 1,
    kVideoInterrupt = IRQ_VIDEO,
    kVideoSetModeFunction = 0,
    kTextMode80x25 = 3,
    kTestFailureExitCode = 1,
};

int g_drawStringCalls = 0;
int16 *g_lastDrawPage = nullptr;
const char *g_lastDrawString = nullptr;
int g_restoreTimerCalls = 0;
int g_clearKeyFlagsCalls = 0;
int g_intDispatchCalls = 0;
int g_lastInterrupt = 0;
uint8 g_lastInputRegs[0xe] = {};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetRuntimeState() {
    g_drawStringCalls = 0;
    g_lastDrawPage = nullptr;
    g_lastDrawString = nullptr;
    g_restoreTimerCalls = 0;
    g_clearKeyFlagsCalls = 0;
    g_intDispatchCalls = 0;
    g_lastInterrupt = 0;
    std::memset(g_lastInputRegs, 0, sizeof(g_lastInputRegs));
    timerHandlerInstalled = 0;
}

} // namespace

void far gfx_drawString(int16 *pageNum, const char *string) {
    ++g_drawStringCalls;
    g_lastDrawPage = pageNum;
    g_lastDrawString = string;
}

void far misc_clearKeyFlags(void) {
    ++g_clearKeyFlagsCalls;
}

void restoreTimerIrqHandler(void) {
    ++g_restoreTimerCalls;
    timerHandlerInstalled = 0;
}

void intDispatch(int intNum, uint8 *inRegs, uint8 *outRegs) {
    ++g_intDispatchCalls;
    g_lastInterrupt = intNum;
    std::memcpy(g_lastInputRegs, inRegs, sizeof(g_lastInputRegs));
    if (outRegs != inRegs) {
        std::memcpy(outRegs, inRegs, sizeof(g_lastInputRegs));
    }
}

int main() {
    int16 page[8] = {};
    const char *title = "  MISSION DEBRIEFING";

    resetRuntimeState();
    drawStringAt(page, title, kDrawX, kDrawY);
    require(page[kDrawPageSlotX] == kDrawX &&
                page[kDrawPageSlotY] == kDrawY &&
                g_drawStringCalls == kExpectedOneCall &&
                g_lastDrawPage == page &&
                g_lastDrawString == title,
            "drawStringAt stores the original x/y slots before forwarding to gfx_drawString");

    resetRuntimeState();
    timerHandlerInstalled = kTimerNotInstalled;
    cleanup();
    require(g_restoreTimerCalls == kExpectedNoCalls,
            "cleanup leaves the timer IRQ alone when the original installed flag is clear");
    require(g_intDispatchCalls == kExpectedOneCall &&
                g_lastInterrupt == kVideoInterrupt &&
                g_lastInputRegs[1] == kVideoSetModeFunction &&
                g_lastInputRegs[0] == kTextMode80x25,
            "cleanup restores original DOS text mode through INT 10h AH=0 AL=3");
    require(g_clearKeyFlagsCalls == kExpectedOneCall,
            "cleanup clears the original keyboard flags after video cleanup");

    resetRuntimeState();
    timerHandlerInstalled = kTimerInstalled;
    cleanup();
    require(g_restoreTimerCalls == kExpectedOneCall &&
                timerHandlerInstalled == kTimerNotInstalled,
            "cleanup restores the timer IRQ only when the original installed flag is set");
    require(g_intDispatchCalls == kExpectedOneCall &&
                g_clearKeyFlagsCalls == kExpectedOneCall,
            "cleanup still restores video mode and clears keys after restoring the timer");

    std::cout << "shared_runtime_behavior_tests passed\n";
    return 0;
}
