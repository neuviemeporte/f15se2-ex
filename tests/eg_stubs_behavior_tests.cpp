#include "egcode.h"
#include "egdata.h"
#include "slot.h"

#include <dos.h>

#include <cstdlib>
#include <iostream>

uint8 joyAxes[8] = {};

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EgStubsOriginalConstant : int {
    kNoError = 0,
    kJoystickCenter = 0x80,
    kInitialAxis0 = 0x11,
    kInitialAxis1 = 0x22,
    kAudioSoundId = 7,
    kAudioSampleId = 3,
    kEngineKnots = 450,
    kEngineThrust = 92,
    kPanelId = 2,
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
    uint8 savedJoyData[4] = {};

    joyAxes[0] = kInitialAxis0;
    joyAxes[1] = kInitialAxis1;
    require(loadF15DgtlBin() == kNoError,
            "loadF15DgtlBin preserves the original no-op sound-data stub");
    require(initJoystickCalibration() == kNoError,
            "initJoystickCalibration preserves the original no-op joystick stub");
    seedJoystickBaseline();
    readJoystickHardware();
    computeJoystickAxis();

    require(readCalibratedJoystick() == kNoError &&
                joyAxes[0] == kJoystickCenter &&
                joyAxes[1] == kJoystickCenter,
            "readCalibratedJoystick preserves the original centered-axis stub");
    require(restoreJoystickData(savedJoyData) == kNoError,
            "restoreJoystickData preserves the original no-op joystick restore stub");
    require(drawCenteredLabelBox(kPanelId, "READY") == kNoError,
            "drawCenteredLabelBox preserves the original no-op label stub");

    require(audio_playSound(kAudioSoundId) == kNoError,
            "audio_playSound preserves the original no-op sound-driver stub");
    require(audio_engineDroneOn() == kNoError,
            "audio_engineDroneOn preserves the original no-op sound-driver stub");
    require(audio_engineDroneOff() == kNoError,
            "audio_engineDroneOff preserves the original no-op sound-driver stub");
    require(audio_playSample(kAudioSampleId) == kNoError,
            "audio_playSample preserves the original no-op sound-driver stub");
    require(audio_setEnginePitch(kEngineKnots, kEngineThrust) == kNoError,
            "audio_setEnginePitch preserves the original no-op sound-driver stub");

    std::cout << "eg_stubs_behavior_tests passed\n";
    return 0;
}
