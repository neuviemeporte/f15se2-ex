// LINK_CORE + headless. Exercises the one piece of real behaviour in the egame
// joystick path: readCalibratedJoystick must report a centred stick when no
// controller is connected, so the game (and menu cursor) don't drift.
//
// The sibling calibration / save-restore / audio entry points are genuine no-ops
// under SDL (initJoystickCalibration/restoreJoystickData/copyJoystickData return
// 0; seedJoystickBaseline/computeJoystickAxis/readJoystickHardware are `{}`; the
// audio_* calls degrade to 0 with no device). Asserting a no-op returns its
// sentinel verifies nothing, so those checks were removed rather than kept as
// coverage padding.
#include "egcode.h"
#include "egdata.h"
#include "headless.h"

#include <cstdlib>
#include <iostream>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
enum EgStubsOriginalConstant : int {
    kJoystickCenter = 0x80,
    // readCalibratedJoystick packs the two centred axis bytes: X | (Y << 8).
    kCentredPacked = kJoystickCenter | (kJoystickCenter << 8),
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

} // namespace

int main() {
    test_headless_init();

    // Seed a stale off-centre deflection; with no device present the calibrated
    // read must snap both axes back to centre (0x80) rather than leave the stale
    // value that would spam a held direction.
    joyAxes[0] = 0x11;
    joyAxes[1] = 0x22;
    const int packed = readCalibratedJoystick();
    require(joyAxes[0] == kJoystickCenter && joyAxes[1] == kJoystickCenter,
            "readCalibratedJoystick centres both axes when no device is present");
    require(packed == kCentredPacked,
            "readCalibratedJoystick returns the centred axes packed as X | (Y << 8)");

    std::cout << "eg_stubs_behavior_tests passed\n";
    return 0;
}
