#include "input.h"
#include "slot.h"
#include "r3d.h"

#include "inttype.h"

// Keep these definitions linkable when weak/selectany attributes are supported by
// the compiler, avoiding symbol conflicts with src/egdata.c definitions.
#if defined(__GNUC__)
#define TEST_WEAK_ATTRIBUTE __attribute__((weak))
#elif defined(_MSC_VER)
#define TEST_WEAK_ATTRIBUTE __declspec(selectany)
#else
#define TEST_WEAK_ATTRIBUTE
#endif

TEST_WEAK_ATTRIBUTE uint8 g_joyRawX = 0x80;
TEST_WEAK_ATTRIBUTE uint8 g_joyRawY = 0x80;

#undef TEST_WEAK_ATTRIBUTE

void timerPump(void);

void r3d_init(void) {}
void r3d_shutdown(void) {}
bool setGamePath(const char* path) {
    (void)path;
    return true;
}
bool verifyGameAssets() { return true; }

void joy_init(void) {}
void joy_shutdown(void) {}

void input_setMode(InputMode mode) { (void)mode; }
void input_setQuitHandler(void (*handler)(void)) { (void)handler; }
bool input_keyWaiting(void) {
    timerPump();
    return false;
}
uint16 input_readKey(void) {
    timerPump();
    return 0;
}
void input_ringReset(void) {
    timerPump();
    g_joyRawX = g_joyRawY = 0x80;
}

int FAR CDECL audio_setup(int16 sampleDataSeg, int16 variantSel) {
    (void)sampleDataSeg;
    (void)variantSel;
    return 0;
}
int FAR CDECL audio_shutdown(void) { return 0; }
int FAR CDECL audio_playIntro(void) { return 0; }

int FAR CDECL misc_readJoystick(int16 axis) {
    (void)axis;
    return 0;
}
