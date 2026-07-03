#include "egcode.h"
#include "egdata.h"
#include "slot.h"
#include "headless.h"

#include <dos.h>

#include <cstdlib>
#include <iostream>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EgStubsOriginalConstant : int {
    kNoError = 0,
    kJoystickCenter = 0x80,
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
    test_headless_init();

    uint8 savedJoyData[4] = {};

    // With no controller connected the stick reads centred: readCalibratedJoystick
    // must re-centre both axes rather than leaving stale deflection.
    joyAxes[0] = 0x11;
    joyAxes[1] = 0x22;
    readJoystickHardware();
    computeJoystickAxis();
    seedJoystickBaseline();
    readCalibratedJoystick();
    require(joyAxes[0] == kJoystickCenter && joyAxes[1] == kJoystickCenter,
            "readCalibratedJoystick centres both axes when no device is present");

    // The SDL port makes runtime calibration and cross-EXE save/restore no-ops;
    // they must succeed (return 0) so the existing call sites stay inert.
    require(initJoystickCalibration() == kNoError,
            "initJoystickCalibration is a no-op success under SDL");
    require(restoreJoystickData(savedJoyData) == kNoError,
            "restoreJoystickData is a no-op success under SDL");
    require(drawCenteredLabelBox(kPanelId, "READY") == kNoError,
            "drawCenteredLabelBox is a no-op success");

    // Audio calls must degrade gracefully (return 0, no crash) when no audio
    // device has been opened, so headless/CI runs are safe.
    require(audio_playSound(kAudioSoundId) == kNoError,
            "audio_playSound is a safe no-op without an audio device");
    require(audio_engineDroneOn() == kNoError,
            "audio_engineDroneOn is a safe no-op without an audio device");
    require(audio_engineDroneOff() == kNoError,
            "audio_engineDroneOff is a safe no-op without an audio device");
    require(audio_playSample(kAudioSampleId) == kNoError,
            "audio_playSample is a safe no-op without an audio device");
    require(audio_setEnginePitch(kEngineKnots, kEngineThrust) == kNoError,
            "audio_setEnginePitch is a safe no-op without an audio device");

    std::cout << "eg_stubs_behavior_tests passed\n";
    return 0;
}
