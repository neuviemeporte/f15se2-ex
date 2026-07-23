#ifndef F15_SE2_STOPTIONS
#define F15_SE2_STOPTIONS

#include "inttype.h"

/* Draw the settings gear in the bottom-right corner of the pilot screen. */
void stOptionsDrawGear(int16 *page);

/* Return whether one logical 320x200 point is inside the settings gear. */
int stOptionsGearHit(int x, int y);

/* Show the modal gameplay-options screen over the selected pilot background. */
void stOptionsShow(const char *pilotName);

#endif /* F15_SE2_STOPTIONS */
