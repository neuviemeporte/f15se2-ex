/*
 * eginput.c - SDL keyboard input for the egame flight loop.
 *
 * Replaces the hand-written INT 09h keyboard ISR from egseg3.asm
 * (setInt9Handler / restoreInt9Handler / kbdInt9Handler). The original hooked
 * the keyboard hardware interrupt, read raw scancodes off port 0x60, hand-built
 * BIOS-style key words in the BIOS keyboard buffer, and turned the arrow /
 * numeric-keypad keys into a virtual analog stick. None of that has a meaning
 * in a native process, so this provides the same two behaviours from SDL:
 *
 *   1. A BIOS-style key ring (scan code in AH, ASCII in AL) drained from the SDL
 *      event queue, read back through kbhit()/_bios_keybrd(0). stepFlightModel
 *      (egflight.c) reads one word per frame and dispatches it (egkeys.c), which
 *      compares the full word (e.g. 0x1372 = 'r', 0x3b00 = F1), so the AH scan
 *      code has to match what INT 16h would have returned.
 *
 *   2. The virtual stick: held arrow / keypad keys deflect g_joyRawX (roll) and
 *      g_joyRawY (pitch), which stepFlightModel reads when no real joystick is
 *      configured. We poll the live key state each frame rather than tracking
 *      press/release, so diagonals and recentre fall out naturally.
 *
 * Joystick hardware stays out of scope (stubbed in egstubs.c).
 */
#include "egtypes.h"
#include "egcode.h"
#include "egdata.h"
#include "inttype.h"
#include "gfx.h"
#include <dos.h>
#include <SDL3/SDL.h>

/* Game tick clock (timer.c); pumped here so the window stays responsive and the
 * clock advances even on a frame that does nothing but poll keys. */
extern void timerPump(void);

/* BIOS-style key ring: each entry is AH = scan code, AL = ASCII, matching the
 * word layout INT 16h returns and the egame dispatch compares against. */
#define EGKEY_RING 32
static uint16 keyRing[EGKEY_RING];
static int    ringHead = 0, ringTail = 0;

static void ringPush(uint16 word) {
    int next = (ringTail + 1) % EGKEY_RING;
    if (next == ringHead) return;   /* full: drop, as the BIOS buffer would */
    keyRing[ringTail] = word;
    ringTail = next;
}

/* Map an SDL scancode + modifiers to a US-layout BIOS key word, or 0 if the key
 * carries no command meaning (the numeric keypad / arrows are handled as the
 * virtual stick instead). AH is the BIOS make code; AL is the ASCII byte, which
 * is 0 for Alt combos and the function keys, exactly as INT 16h reports. */
static uint16 biosWord(SDL_Scancode sc, SDL_Keymod mod) {
    Uint8 scan;
    char lo, hi;   /* unshifted / shifted ASCII */

    switch (sc) {
    case SDL_SCANCODE_A: scan = 0x1E; lo = 'a'; hi = 'A'; break;
    case SDL_SCANCODE_B: scan = 0x30; lo = 'b'; hi = 'B'; break;
    case SDL_SCANCODE_C: scan = 0x2E; lo = 'c'; hi = 'C'; break;
    case SDL_SCANCODE_D: scan = 0x20; lo = 'd'; hi = 'D'; break;
    case SDL_SCANCODE_E: scan = 0x12; lo = 'e'; hi = 'E'; break;
    case SDL_SCANCODE_F: scan = 0x21; lo = 'f'; hi = 'F'; break;
    case SDL_SCANCODE_G: scan = 0x22; lo = 'g'; hi = 'G'; break;
    case SDL_SCANCODE_H: scan = 0x23; lo = 'h'; hi = 'H'; break;
    case SDL_SCANCODE_I: scan = 0x17; lo = 'i'; hi = 'I'; break;
    case SDL_SCANCODE_J: scan = 0x24; lo = 'j'; hi = 'J'; break;
    case SDL_SCANCODE_K: scan = 0x25; lo = 'k'; hi = 'K'; break;
    case SDL_SCANCODE_L: scan = 0x26; lo = 'l'; hi = 'L'; break;
    case SDL_SCANCODE_M: scan = 0x32; lo = 'm'; hi = 'M'; break;
    case SDL_SCANCODE_N: scan = 0x31; lo = 'n'; hi = 'N'; break;
    case SDL_SCANCODE_O: scan = 0x18; lo = 'o'; hi = 'O'; break;
    case SDL_SCANCODE_P: scan = 0x19; lo = 'p'; hi = 'P'; break;
    case SDL_SCANCODE_Q: scan = 0x10; lo = 'q'; hi = 'Q'; break;
    case SDL_SCANCODE_R: scan = 0x13; lo = 'r'; hi = 'R'; break;
    case SDL_SCANCODE_S: scan = 0x1F; lo = 's'; hi = 'S'; break;
    case SDL_SCANCODE_T: scan = 0x14; lo = 't'; hi = 'T'; break;
    case SDL_SCANCODE_U: scan = 0x16; lo = 'u'; hi = 'U'; break;
    case SDL_SCANCODE_V: scan = 0x2F; lo = 'v'; hi = 'V'; break;
    case SDL_SCANCODE_W: scan = 0x11; lo = 'w'; hi = 'W'; break;
    case SDL_SCANCODE_X: scan = 0x2D; lo = 'x'; hi = 'X'; break;
    case SDL_SCANCODE_Y: scan = 0x15; lo = 'y'; hi = 'Y'; break;
    case SDL_SCANCODE_Z: scan = 0x2C; lo = 'z'; hi = 'Z'; break;

    case SDL_SCANCODE_1: scan = 0x02; lo = '1'; hi = '!'; break;
    case SDL_SCANCODE_2: scan = 0x03; lo = '2'; hi = '@'; break;
    case SDL_SCANCODE_3: scan = 0x04; lo = '3'; hi = '#'; break;
    case SDL_SCANCODE_4: scan = 0x05; lo = '4'; hi = '$'; break;
    case SDL_SCANCODE_5: scan = 0x06; lo = '5'; hi = '%'; break;
    case SDL_SCANCODE_6: scan = 0x07; lo = '6'; hi = '^'; break;
    case SDL_SCANCODE_7: scan = 0x08; lo = '7'; hi = '&'; break;
    case SDL_SCANCODE_8: scan = 0x09; lo = '8'; hi = '*'; break;
    case SDL_SCANCODE_9: scan = 0x0A; lo = '9'; hi = '('; break;
    case SDL_SCANCODE_0: scan = 0x0B; lo = '0'; hi = ')'; break;

    case SDL_SCANCODE_MINUS:        scan = 0x0C; lo = '-';  hi = '_';  break;
    case SDL_SCANCODE_EQUALS:       scan = 0x0D; lo = '=';  hi = '+';  break;
    case SDL_SCANCODE_LEFTBRACKET:  scan = 0x1A; lo = '[';  hi = '{';  break;
    case SDL_SCANCODE_RIGHTBRACKET: scan = 0x1B; lo = ']';  hi = '}';  break;
    case SDL_SCANCODE_SEMICOLON:    scan = 0x27; lo = ';';  hi = ':';  break;
    case SDL_SCANCODE_APOSTROPHE:   scan = 0x28; lo = '\''; hi = '"';  break;
    case SDL_SCANCODE_GRAVE:        scan = 0x29; lo = '`';  hi = '~';  break;
    case SDL_SCANCODE_BACKSLASH:    scan = 0x2B; lo = '\\'; hi = '|';  break;
    case SDL_SCANCODE_COMMA:        scan = 0x33; lo = ',';  hi = '<';  break;
    case SDL_SCANCODE_PERIOD:       scan = 0x34; lo = '.';  hi = '>';  break;
    case SDL_SCANCODE_SLASH:        scan = 0x35; lo = '/';  hi = '?';  break;

    case SDL_SCANCODE_SPACE:     scan = 0x39; lo = ' ';  hi = ' ';  break;
    case SDL_SCANCODE_RETURN:    scan = 0x1C; lo = 0x0D; hi = 0x0D; break;
    case SDL_SCANCODE_BACKSPACE: scan = 0x0E; lo = 0x08; hi = 0x08; break;
    case SDL_SCANCODE_ESCAPE:    scan = 0x01; lo = 0x1B; hi = 0x1B; break;
    case SDL_SCANCODE_TAB:       scan = 0x0F; lo = 0x09; hi = 0x09; break;

    /* Function keys carry no ASCII; egame compares the bare scan word. */
    case SDL_SCANCODE_F1:  scan = 0x3B; lo = hi = 0; break;
    case SDL_SCANCODE_F2:  scan = 0x3C; lo = hi = 0; break;
    case SDL_SCANCODE_F3:  scan = 0x3D; lo = hi = 0; break;
    case SDL_SCANCODE_F4:  scan = 0x3E; lo = hi = 0; break;
    case SDL_SCANCODE_F5:  scan = 0x3F; lo = hi = 0; break;
    case SDL_SCANCODE_F6:  scan = 0x40; lo = hi = 0; break;
    case SDL_SCANCODE_F7:  scan = 0x41; lo = hi = 0; break;
    case SDL_SCANCODE_F8:  scan = 0x42; lo = hi = 0; break;
    case SDL_SCANCODE_F9:  scan = 0x43; lo = hi = 0; break;
    case SDL_SCANCODE_F10: scan = 0x44; lo = hi = 0; break;

    default: return 0;
    }

    /* Alt forces AL = 0 (BIOS reports the scan code alone for Alt combos). */
    if (mod & SDL_KMOD_ALT)
        return (uint16)scan << 8;
    /* Ctrl+letter -> control code 1..26 in AL. */
    if ((mod & SDL_KMOD_CTRL) && lo >= 'a' && lo <= 'z')
        return ((uint16)scan << 8) | (uint8)(lo - 'a' + 1);
    return ((uint16)scan << 8) | (uint8)((mod & SDL_KMOD_SHIFT) ? hi : lo);
}

/* Update the virtual stick from the live key state. The original mapped the
 * numeric keypad and arrow keys to one held direction at a time via a scancode
 * table; polling the held state reproduces the same axis assignment and gives
 * smooth diagonals for free. Deflection matches the original first-press value
 * (0x5A off centre); a real joystick is handled elsewhere. */
static void updateStick(void) {
    const bool *ks = SDL_GetKeyboardState(NULL);
    const Uint8 LO = 0x26, HI = 0xDA;   /* centre 0x80 -/+ 0x5A */
    Uint8 x = 0x80;   /* g_joyRawX = roll  (left = LO, right = HI) */
    Uint8 y = 0x80;   /* g_joyRawY = pitch (up   = LO, down  = HI) */

    if (ks[SDL_SCANCODE_UP]    || ks[SDL_SCANCODE_KP_8]) y = LO;
    if (ks[SDL_SCANCODE_DOWN]  || ks[SDL_SCANCODE_KP_2]) y = HI;
    if (ks[SDL_SCANCODE_LEFT]  || ks[SDL_SCANCODE_KP_4]) x = LO;
    if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6]) x = HI;
    /* Keypad diagonals deflect both axes at once. */
    if (ks[SDL_SCANCODE_KP_7]) { y = LO; x = LO; }
    if (ks[SDL_SCANCODE_KP_9]) { y = LO; x = HI; }
    if (ks[SDL_SCANCODE_KP_1]) { y = HI; x = LO; }
    if (ks[SDL_SCANCODE_KP_3]) { y = HI; x = HI; }

    g_joyRawX = x;
    g_joyRawY = y;
}

/* Drain the SDL event queue into the key ring and refresh the virtual stick.
 * Called from every kbhit()/_bios_keybrd() the flight loop makes, so input and
 * the window stay current frame by frame. */
static void pumpInput(void) {
    SDL_Event ev;
    timerPump();
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            ringPush(0x1000);   /* feed Alt+Q so the mission quits cleanly */
            break;
        case SDL_EVENT_KEY_DOWN: {
            /* Alt+Enter toggles fullscreen; swallow it so it never reaches the
             * key ring as an Alt-shifted Return. */
            if (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                gfx_toggleFullscreen();
                break;
            }
            uint16 word = biosWord(ev.key.scancode, ev.key.mod);
            if (word) ringPush(word);
            break;
        }
        default: break;
        }
    }
    updateStick();
}

/* INT 16h function 0: blocking read, scan code in AH and ASCII in AL. The
 * flight loop only calls this after kbhit() reports a key, but block like the
 * BIOS would for safety. */
int _bios_keybrd(int cmd) {
    uint16 word;
    pumpInput();
    if (cmd == 1)   /* peek: 0 if empty, else the word without consuming */
        return (ringHead != ringTail) ? keyRing[ringHead] : 0;
    while (ringHead == ringTail) {
        SDL_Delay(2);
        pumpInput();
    }
    word = keyRing[ringHead];
    ringHead = (ringHead + 1) % EGKEY_RING;
    return word;
}

/* Non-zero when a key word is waiting. */
int kbhit(void) {
    pumpInput();
    return (ringHead != ringTail);
}

int far setInt9Handler(void)
{
    ringHead = ringTail = 0;
    g_joyRawX = 0x80;
    g_joyRawY = 0x80;
    return 0;
}

int far restoreInt9Handler(void)
{
    return 0;
}
