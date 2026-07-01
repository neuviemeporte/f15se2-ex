#include "inttype.h"
#include "pointers.h"

void setViewPosition(int, int, int) {}

int FAR CDECL fillSpanRect(const int16 *, int, int, int, int) {
    return 0;
}

void drawStringActivePage(const char *, int, int, int) {}

int FAR CDECL misc_readJoystick(int16) {
    return 0;
}

int getTimeOfDay(void) {
    return 1;
}
