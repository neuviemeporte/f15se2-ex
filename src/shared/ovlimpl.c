/*
 * ovlimpl.c - Overlay slot implementations for misc/audio overlays (NO_ASM build).
 * These replace the 5-byte far-jump entries that get patched by setupOverlaySlots.
 */

#include "inttype.h"
#include "comm.h"
#include "const.h"
#include "input.h"
#include "dos_compat.h"
#include <SDL3/SDL.h>

extern int16 exitCode;
extern int16 fileHandle;

/* --- SDL keyboard input ----------------------------------------------------
 * The original MISC.EXE slots read the BIOS keyboard (INT 16h) directly. The
 * single SDL event pump and the BIOS-style key ring now live in input.c; these
 * are the thin menu-side wrappers over it, selecting INPUT_MODE_MENU (arrows
 * navigate, printable keys come through as localized text, and the gamepad
 * drives menu navigation). The rest of the game is unchanged: it masks with
 * `& 0xff` for text and compares the full word against KEYCODE_UPARROW etc. */

/* Original MISC.EXE slot 0x5a: 0 if a key is waiting, 0xFFFF if empty. */
int FAR cdecl misc_checkKeyBuf(void) {
    input_setMode(INPUT_MODE_MENU);
    /* Callers poll this in tight wait loops; yield a sub-tick slice so those
     * waits advance the clock (via the pump) without pegging a core. egame's
     * per-frame loop uses INPUT_MODE_FLIGHT, so it is unaffected. */
    bool waiting = input_keyWaiting();
    SDL_DelayNS(SDL_NS_PER_MS);
    return waiting ? 0 : 0xFFFF;
}

/* Original: GetKey. Blocking read: scan code in AH, ASCII in AL. */
int FAR cdecl misc_getKey(void) {
    input_setMode(INPUT_MODE_MENU);
    return input_readKey();
}

/* misc_readJoystick lives in joystick.c (SDL gamepad/joystick buttons). */

void FAR cdecl misc_clearKeyFlags(void) {
    input_setMode(INPUT_MODE_MENU);
    input_ringReset();
}

/* Audio overlay slots (audio_setup/shutdown/playIntro) live in
 * asound/asound_sdl.c alongside the rest of the SDL audio backend. */
