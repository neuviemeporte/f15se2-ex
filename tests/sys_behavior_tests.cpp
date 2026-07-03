#include "egcode.h"
#include "egdata.h"
#include "egtypes.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SysOriginalConstant : int {
    kDacFirstRangeStart = 0x10,
    kDacFirstRangeCount = 0x50,
    kDacSecondRangeStart = 0x60,
    kDacSecondRangeCount = 0xA0,
    kGroundRampOffset = 0x30,
    kGroundRampBytes = 0x30,
    kGroundRampCopyColor = 6,
    kGroundRampSkipColor = 2,
    kPoisonByte = 0x7E,
    kNightModeOff = 0,
    kNightModeOn = 1,
    kExpectedTwoCalls = 2,
    kTestFailureExitCode = 1,
};

struct DacCall {
    uint16 startReg;
    uint16 count;
    const uint8 *data;
};

DacCall g_dacCalls[2] = {};
int g_dacCallCount = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetSysState() {
    std::memset(dacValues + kGroundRampOffset, kPoisonByte, kGroundRampBytes);
    std::memset(g_dacCalls, 0, sizeof(g_dacCalls));
    g_dacCallCount = 0;
    g_horizonGroundColor = kGroundRampCopyColor;
    g_nightMode = kNightModeOff;
}

} // namespace

void gfx_setDacRange(uint16 startReg, uint16 count, const uint8 *vgaTriples) {
    require(g_dacCallCount < kExpectedTwoCalls,
            "setupDac should issue exactly the original two DAC range loads");
    g_dacCalls[g_dacCallCount++] = {startReg, count, vgaTriples};
}

int main() {
    resetSysState();
    setupDac();
    require(g_dacCallCount == kExpectedTwoCalls,
            "setupDac issues the original two VGA DAC range loads");
    require(g_dacCalls[0].startReg == kDacFirstRangeStart &&
                g_dacCalls[0].count == kDacFirstRangeCount &&
                g_dacCalls[0].data == dacValues1,
            "setupDac loads dacValues1 into the original first DAC register range");
    require(g_dacCalls[1].startReg == kDacSecondRangeStart &&
                g_dacCalls[1].count == kDacSecondRangeCount &&
                g_dacCalls[1].data == dacValues,
            "setupDac loads day dacValues into the original second DAC register range");
    require(std::memcmp(dacValues + kGroundRampOffset,
                        g_dacGroundPaletteSrc,
                        kGroundRampBytes) == 0,
            "setupDac copies the original ground ramp into dacValues during day setup");

    resetSysState();
    g_horizonGroundColor = kGroundRampSkipColor;
    setupDac();
    for (int idx = 0; idx < kGroundRampBytes; ++idx) {
        require(dacValues[kGroundRampOffset + idx] == kPoisonByte,
                "setupDac skips the ground ramp copy for the original sentinel ground color");
    }

    resetSysState();
    g_nightMode = kNightModeOn;
    setupDac();
    require(g_dacCalls[1].data == otherDacValues,
            "setupDac selects the original night DAC table when night mode is active");

    std::cout << "sys_behavior_tests passed\n";
    return 0;
}
