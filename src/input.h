#ifndef F15_SE2_INPUT
#define F15_SE2_INPUT
/*
 * input.c - the single SDL event pump shared by every game phase.
 *
 * One SDL_PollEvent drain and one BIOS-style key ring back both the flight loop
 * (eginput.c: kbhit/egReadKey) and the menu/splash/debrief loops (ovlimpl.c:
 * misc_checkKeyBuf/misc_getKey). Window-level events - quit, Alt+Enter
 * fullscreen, focus, gamepad hotplug, audio-device add/remove - are handled
 * uniformly here regardless of phase. Only the keyboard/gamepad *meaning* is
 * context-dependent (arrows are the virtual stick in flight but cursor keys in
 * menus; letters are commands in flight but localized text entry in menus), so
 * the pump carries an explicit mode.
 */
#include "inttype.h"

typedef enum {
    INPUT_MODE_MENU,   /* splash / menus / briefing / debrief */
    INPUT_MODE_FLIGHT, /* egame in-flight loop */
} InputMode;

/* Select how the pump translates keyboard/gamepad into the key ring. Set by the
 * phase's key readers (kbhit/egReadKey -> FLIGHT, misc_* -> MENU). */
void input_setMode(InputMode mode);
InputMode input_getMode(void);

/* True when the gamepad/joystick should drive flight controls rather than the
 * keyboard: a device is connected and was used more recently than the keyboard.
 * Pressing a key hands control back to the keyboard, moving the stick takes it
 * back - so the flight stick follows the last-used device instead of staying
 * stuck on whichever was plugged in. */
bool input_preferGamepad(void);

/* Drain the SDL event queue once, advance the game clock, refresh the polled
 * controls for the current mode. Every key-wait loop funnels through this. */
void input_pumpEvents(void);

/* --- BIOS-style key ring (AH = scan code, AL = ASCII), shared by all phases - */
void input_ringReset(void);          /* drop any queued keys, recentre stick */
bool input_keyWaiting(void);         /* true when a key word is queued */
uint16 input_readKey(void);          /* blocking pop: pump + wait, then return */

/* --- window state, set by the pump, for callers that want to react --------- */
bool input_quitRequested(void);      /* SDL_EVENT_QUIT seen (also feeds Alt+Q) */
bool input_hasFocus(void);           /* window currently has keyboard focus */

#endif /* F15_SE2_INPUT */
