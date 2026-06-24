/*
 * ovlimpl.c - Overlay slot implementations for misc/audio overlays (NO_ASM build).
 * These replace the 5-byte far-jump entries that get patched by setupOverlaySlots.
 */

#include "inttype.h"
#include "pointers.h"
#include "comm.h"
#include "const.h"
#include <dos.h>
#include <SDL3/SDL.h>

extern uint8  exitCode;
extern int16  fileHandle;

/* Game tick clock (timer.c); pumped here so key-polling loops advance time. */
extern void timerPump(void);

/* --- SDL keyboard input ----------------------------------------------------
 * The original MISC.EXE slots read the BIOS keyboard (INT 16h) and the BIOS
 * key buffer directly. Under SDL we instead drain the SDL event queue into a
 * small ring of BIOS-style key words (AH = scan code, AL = ASCII). The rest of
 * the game is unchanged: it masks with `& 0xff` to read text and compares the
 * full word against KEYCODE_UPARROW etc. for keys that carry no ASCII, so the
 * word layout has to match what INT 16h would have returned. */

#define KEYBUF_SIZE 32
static uint16 keyBuf[KEYBUF_SIZE];
static int    keyHead = 0, keyTail = 0;

static void keyPush(uint16 code) {
    int next = (keyTail + 1) % KEYBUF_SIZE;
    if (next == keyHead) return; /* buffer full: drop, as the BIOS would */
    keyBuf[keyTail] = code;
    keyTail = next;
}

/* Keys with no ASCII (arrows): BIOS word is scan code in AH, AL = 0, so the
 * callers that compare the full word still match. */
static uint16 biosFromScancode(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP:    return KEYCODE_UPARROW;    /* 0x4800 */
    case SDL_SCANCODE_DOWN:  return KEYCODE_DNARROW;    /* 0x5000 */
    case SDL_SCANCODE_LEFT:  return KEYCODE_LEFTARROW;  /* 0x4b00 */
    case SDL_SCANCODE_RIGHT: return KEYCODE_RIGHTARROW; /* 0x4d00 */
    default:                 return 0;
    }
}

/* Drain the SDL event queue into keyBuf. Also keeps the OS window responsive
 * (without an event pump SDL never processes resize/close). */
static void pumpInput(void) {
    SDL_Event ev;
    /* Every key-polling wait loop funnels through here, so advance the game
     * clock too: this is what drives the tick counters those loops spin on. */
    timerPump();
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            /* Feed the game's own Alt+Q quit path rather than hard-exiting. */
            keyPush(KEYCODE_ALTQ);
            break;
        case SDL_EVENT_KEY_DOWN: {
            SDL_Keymod  mod = ev.key.mod;
            uint16 special = biosFromScancode(ev.key.scancode);
            if (special) { keyPush(special); break; }
            if ((mod & SDL_KMOD_ALT) && ev.key.key == SDLK_Q) {
                keyPush(KEYCODE_ALTQ); /* 0x1000: Q scan code in AH, AL = 0 */
                break;
            }
            switch (ev.key.key) {
            case SDLK_RETURN:
            case SDLK_KP_ENTER:  keyPush(0x1c00 | KEYCODE_ENTER);     break; /* AL=0x0d */
            case SDLK_ESCAPE:    keyPush(0x0100 | KEYCODE_ESC);       break; /* AL=0x1b */
            case SDLK_BACKSPACE: keyPush(0x0e00 | KEYCODE_BACKSPACE); break; /* AL=0x08 */
            default:
                /* Ctrl+letter -> control code in AL (e.g. Ctrl+X = 0x18). SDL
                 * emits no text event for these, so synthesize the byte here. */
                if ((mod & SDL_KMOD_CTRL) &&
                    ev.key.key >= SDLK_A && ev.key.key <= SDLK_Z)
                    keyPush((uint16)(ev.key.key - SDLK_A + 1));
                break;
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            /* Printable characters arrive here already shifted/localised. Queue
             * each ASCII byte in AL (AH = 0); callers mask with `& 0xff`. */
            const char *p = ev.text.text;
            for (; *p; ++p) {
                unsigned char c = (unsigned char)*p;
                if (c >= 0x20 && c < 0x80) keyPush(c);
            }
            break;
        }
        default: break;
        }
    }
}

/* Original MISC.EXE slot 0x5a: 0 if a key is waiting, 0xFFFF if empty. */
int far cdecl misc_checkKeyBuf(void) {
    pumpInput();
    /* Callers poll this in tight wait loops; yield a sub-tick slice so those
     * waits advance the clock (via pumpInput) without pegging a core. egame's
     * per-frame loop uses a separate input path, so it is unaffected. */
    SDL_DelayNS(SDL_NS_PER_MS);
    return (keyHead != keyTail) ? 0 : 0xFFFF;
}

/* Original: GetKey. Blocking read: scan code in AH, ASCII in AL. */
int far cdecl misc_getKey(void) {
    uint16 code;
    pumpInput();
    while (keyHead == keyTail) {
        SDL_Delay(2); /* idle a touch so the wait doesn't peg a core */
        pumpInput();
    }
    code = keyBuf[keyHead];
    keyHead = (keyHead + 1) % KEYBUF_SIZE;
    return code;
}

int far cdecl misc_readJoystick(int16 param) { return 0; }

void far cdecl misc_clearKeyFlags(void) {
    pumpInput();
    keyHead = keyTail = 0;
}

/* Audio overlay slots */
int far cdecl audio_setup(int16 a, int16 b) { return 0; }
int far cdecl audio_shutdown(void) { return 0; }
int far cdecl audio_playIntro(void) { return 0; }
