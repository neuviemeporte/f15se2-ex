/*
 * eginput.c - flight-loop keyboard/joystick entry points.
 *
 * Replaces the hand-written INT 09h keyboard ISR from egseg3.asm
 * (setInt9Handler / restoreInt9Handler / kbdInt9Handler). The single SDL event
 * pump and the BIOS-style key ring now live in input.c; these are the thin
 * flight-loop wrappers over it, selecting INPUT_MODE_FLIGHT (letters are
 * cockpit commands, the arrows/keypad are the virtual stick, and the gamepad
 * drives the cockpit bindings). stepFlightModel (egflight.c) reads one word per
 * frame through kbhit()/egReadKey() and dispatches it (egkeys.c).
 */
#include "eginput.h"
#include "input.h"
#include <dos.h> /* far/cdecl macros, kbhit() prototype */

/* Blocking read, scan code in AH and ASCII in AL (INT 16h function 0). */
int egReadKey(void) {
    input_setMode(INPUT_MODE_FLIGHT);
    return input_readKey();
}

/* Non-zero when a key word is waiting. */
int kbhit(void) {
    input_setMode(INPUT_MODE_FLIGHT);
    return input_keyWaiting();
}

int far setInt9Handler(void) {
    input_setMode(INPUT_MODE_FLIGHT);
    input_ringReset();
    return 0;
}

int far restoreInt9Handler(void) {
    return 0;
}
