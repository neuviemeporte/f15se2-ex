#include "inttype.h"
#include "slot.h"
#include "stdata.h"

int loadF15DgtlBin(void) { return 0; }
int FAR initJoystickCalibration(void) { return 0; }
int FAR readCalibratedJoystick(void) {
    joyAxes[0] = 0x80;
    joyAxes[1] = 0x80;
    return 0;
}
void seedJoystickBaseline(void) {}
void readJoystickHardware(void) {}
void computeJoystickAxis(void) {}
int FAR restoreJoystickData(uint8 FAR *ptr) {
    (void)ptr;
    return 0;
}

int FAR CDECL audio_playSound(int soundId) {
    (void)soundId;
    return 0;
}
int FAR CDECL audio_engineDroneOn(void) { return 0; }
int FAR CDECL audio_engineDroneOff(void) { return 0; }
int FAR CDECL audio_playSample(int soundIdx) {
    (void)soundIdx;
    return 0;
}
int FAR CDECL audio_setEnginePitch(int knots, int thrust) {
    (void)knots;
    (void)thrust;
    return 0;
}
