#ifndef F15_SE2_EGINPUT
#define F15_SE2_EGINPUT
/* public interface of eginput.c (SDL keyboard/joystick for the flight loop) */

/* Blocking read of the next key word, BIOS INT 16h-style: scan code in the high
 * byte, ASCII in the low byte (e.g. 0x1372 = 'r', 0x3b00 = F1). Drains the SDL
 * event queue first, then waits for a key. */
int egReadKey(void);

#endif /* F15_SE2_EGINPUT */
