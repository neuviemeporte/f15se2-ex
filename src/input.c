/*
 * input.c - single SDL event pump and shared BIOS-style key ring.
 *
 * The single SDL event pump for the whole game; the flight-loop keyboard
 * (eginput.c) and the MISC overlay keyboard (shared/ovlimpl.c) are thin wrappers
 * over it. The original DOS pieces hooked the keyboard hardware (INT 09h / INT
 * 16h) and read raw scancodes; here a single SDL_PollEvent drain fills one
 * BIOS-style key ring (AH = scan code, AL = ASCII, matching what INT 16h returned
 * and what the game dispatch compares against).
 *
 * Window-level events are handled uniformly for every game phase: quit, the
 * Alt+Enter fullscreen toggle, focus changes, gamepad hotplug, and audio-device
 * arrival/removal. Only the keyboard/gamepad meaning differs between flight and
 * the menus, so that part is gated on the mode set by the phase's key readers.
 */
#include "input.h"
#include "inttype.h"
#include "const.h"
#include "gfx.h"
#include "joystick.h"
#include <SDL3/SDL.h>

/* Game tick clock (timer.c); pumped here so the window stays responsive and the
 * clock advances even on a frame that does nothing but poll keys. */
extern void timerPump(void);

/* Virtual stick from held keys (egdata.c, egame globals; 0x80 = centre). Only
 * the flight mode touches them. */
extern uint8 g_joyRawX, g_joyRawY;

/* joyAxes[0]=roll, [1]=pitch (stdata.c); centred at 0x80. */
extern uint8 joyAxes[];

static InputMode g_mode = INPUT_MODE_MENU;
static bool g_quitRequested = false;
static bool g_hasFocus = true;
static void (*g_quitHandler)(void) = NULL;

/* Which device was used most recently. Defaults to the device so a connected
 * stick keeps working as before; a key press flips it to the keyboard and stick
 * activity flips it back (see input_preferGamepad / noteGamepadActivity). */
static bool g_lastWasGamepad = true;

void input_setMode(InputMode mode) { g_mode = mode; }
InputMode input_getMode(void) { return g_mode; }
bool input_quitRequested(void) { return g_quitRequested; }
bool input_hasFocus(void) { return g_hasFocus; }
void input_setQuitHandler(void (*handler)(void)) { g_quitHandler = handler; }
bool input_preferGamepad(void) { return g_lastWasGamepad && joy_connected(); }

/* --- shared key ring -------------------------------------------------------
 * Each entry is a BIOS key word: AH = scan code, AL = ASCII. The game masks
 * with `& 0xff` for text and compares the full word for keys with no ASCII
 * (function keys, arrows), so the layout has to match INT 16h. */
#define KEY_RING 32
static uint16 keyRing[KEY_RING];
static int ringHead = 0, ringTail = 0;

static void ringPush(uint16 word) {
    int next = (ringTail + 1) % KEY_RING;
    if (next == ringHead) return; /* full: drop, as the BIOS buffer would */
    keyRing[ringTail] = word;
    ringTail = next;
}

void input_ringReset(void) {
    ringHead = ringTail = 0;
    g_joyRawX = 0x80;
    g_joyRawY = 0x80;
}

bool input_keyWaiting(void) {
    input_pumpEvents();
    return ringHead != ringTail;
}

uint16 input_readKey(void) {
    uint16 word;
    input_pumpEvents();
    while (ringHead == ringTail) {
        SDL_Delay(2); /* idle a touch so the wait doesn't peg a core */
        input_pumpEvents();
    }
    word = keyRing[ringHead];
    ringHead = (ringHead + 1) % KEY_RING;
    return word;
}

/* --- keyboard translation --------------------------------------------------
 * Map an SDL scancode + modifiers to a US-layout BIOS key word, or 0 if the key
 * carries no command meaning (the numeric keypad / arrows are the virtual stick
 * in flight). AH is the BIOS make code; AL is the ASCII byte, 0 for Alt combos
 * and the function keys, exactly as INT 16h reports. Used in flight, where
 * letters are commands; the menus take printable text via TEXT_INPUT instead so
 * it stays localized. */
static uint16 biosWord(SDL_Scancode sc, SDL_Keymod mod) {
    Uint8 scan;
    char lo, hi; /* unshifted / shifted ASCII */

    switch (sc) {
    case SDL_SCANCODE_A:
        scan = 0x1E;
        lo = 'a';
        hi = 'A';
        break;
    case SDL_SCANCODE_B:
        scan = 0x30;
        lo = 'b';
        hi = 'B';
        break;
    case SDL_SCANCODE_C:
        scan = 0x2E;
        lo = 'c';
        hi = 'C';
        break;
    case SDL_SCANCODE_D:
        scan = 0x20;
        lo = 'd';
        hi = 'D';
        break;
    case SDL_SCANCODE_E:
        scan = 0x12;
        lo = 'e';
        hi = 'E';
        break;
    case SDL_SCANCODE_F:
        scan = 0x21;
        lo = 'f';
        hi = 'F';
        break;
    case SDL_SCANCODE_G:
        scan = 0x22;
        lo = 'g';
        hi = 'G';
        break;
    case SDL_SCANCODE_H:
        scan = 0x23;
        lo = 'h';
        hi = 'H';
        break;
    case SDL_SCANCODE_I:
        scan = 0x17;
        lo = 'i';
        hi = 'I';
        break;
    case SDL_SCANCODE_J:
        scan = 0x24;
        lo = 'j';
        hi = 'J';
        break;
    case SDL_SCANCODE_K:
        scan = 0x25;
        lo = 'k';
        hi = 'K';
        break;
    case SDL_SCANCODE_L:
        scan = 0x26;
        lo = 'l';
        hi = 'L';
        break;
    case SDL_SCANCODE_M:
        scan = 0x32;
        lo = 'm';
        hi = 'M';
        break;
    case SDL_SCANCODE_N:
        scan = 0x31;
        lo = 'n';
        hi = 'N';
        break;
    case SDL_SCANCODE_O:
        scan = 0x18;
        lo = 'o';
        hi = 'O';
        break;
    case SDL_SCANCODE_P:
        scan = 0x19;
        lo = 'p';
        hi = 'P';
        break;
    case SDL_SCANCODE_Q:
        scan = 0x10;
        lo = 'q';
        hi = 'Q';
        break;
    case SDL_SCANCODE_R:
        scan = 0x13;
        lo = 'r';
        hi = 'R';
        break;
    case SDL_SCANCODE_S:
        scan = 0x1F;
        lo = 's';
        hi = 'S';
        break;
    case SDL_SCANCODE_T:
        scan = 0x14;
        lo = 't';
        hi = 'T';
        break;
    case SDL_SCANCODE_U:
        scan = 0x16;
        lo = 'u';
        hi = 'U';
        break;
    case SDL_SCANCODE_V:
        scan = 0x2F;
        lo = 'v';
        hi = 'V';
        break;
    case SDL_SCANCODE_W:
        scan = 0x11;
        lo = 'w';
        hi = 'W';
        break;
    case SDL_SCANCODE_X:
        scan = 0x2D;
        lo = 'x';
        hi = 'X';
        break;
    case SDL_SCANCODE_Y:
        scan = 0x15;
        lo = 'y';
        hi = 'Y';
        break;
    case SDL_SCANCODE_Z:
        scan = 0x2C;
        lo = 'z';
        hi = 'Z';
        break;

    case SDL_SCANCODE_1:
        scan = 0x02;
        lo = '1';
        hi = '!';
        break;
    case SDL_SCANCODE_2:
        scan = 0x03;
        lo = '2';
        hi = '@';
        break;
    case SDL_SCANCODE_3:
        scan = 0x04;
        lo = '3';
        hi = '#';
        break;
    case SDL_SCANCODE_4:
        scan = 0x05;
        lo = '4';
        hi = '$';
        break;
    case SDL_SCANCODE_5:
        scan = 0x06;
        lo = '5';
        hi = '%';
        break;
    case SDL_SCANCODE_6:
        scan = 0x07;
        lo = '6';
        hi = '^';
        break;
    case SDL_SCANCODE_7:
        scan = 0x08;
        lo = '7';
        hi = '&';
        break;
    case SDL_SCANCODE_8:
        scan = 0x09;
        lo = '8';
        hi = '*';
        break;
    case SDL_SCANCODE_9:
        scan = 0x0A;
        lo = '9';
        hi = '(';
        break;
    case SDL_SCANCODE_0:
        scan = 0x0B;
        lo = '0';
        hi = ')';
        break;

    case SDL_SCANCODE_MINUS:
        scan = 0x0C;
        lo = '-';
        hi = '_';
        break;
    case SDL_SCANCODE_EQUALS:
        scan = 0x0D;
        lo = '=';
        hi = '+';
        break;
    case SDL_SCANCODE_LEFTBRACKET:
        scan = 0x1A;
        lo = '[';
        hi = '{';
        break;
    case SDL_SCANCODE_RIGHTBRACKET:
        scan = 0x1B;
        lo = ']';
        hi = '}';
        break;
    case SDL_SCANCODE_SEMICOLON:
        scan = 0x27;
        lo = ';';
        hi = ':';
        break;
    case SDL_SCANCODE_APOSTROPHE:
        scan = 0x28;
        lo = '\'';
        hi = '"';
        break;
    case SDL_SCANCODE_GRAVE:
        scan = 0x29;
        lo = '`';
        hi = '~';
        break;
    case SDL_SCANCODE_BACKSLASH:
        scan = 0x2B;
        lo = '\\';
        hi = '|';
        break;
    case SDL_SCANCODE_COMMA:
        scan = 0x33;
        lo = ',';
        hi = '<';
        break;
    case SDL_SCANCODE_PERIOD:
        scan = 0x34;
        lo = '.';
        hi = '>';
        break;
    case SDL_SCANCODE_SLASH:
        scan = 0x35;
        lo = '/';
        hi = '?';
        break;

    case SDL_SCANCODE_SPACE:
        scan = 0x39;
        lo = ' ';
        hi = ' ';
        break;
    case SDL_SCANCODE_RETURN:
        scan = 0x1C;
        lo = 0x0D;
        hi = 0x0D;
        break;
    case SDL_SCANCODE_BACKSPACE:
        scan = 0x0E;
        lo = 0x08;
        hi = 0x08;
        break;
    case SDL_SCANCODE_ESCAPE:
        scan = 0x01;
        lo = 0x1B;
        hi = 0x1B;
        break;
    case SDL_SCANCODE_TAB:
        scan = 0x0F;
        lo = 0x09;
        hi = 0x09;
        break;

    /* Function keys carry no ASCII; egame compares the bare scan word. */
    case SDL_SCANCODE_F1:
        scan = 0x3B;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F2:
        scan = 0x3C;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F3:
        scan = 0x3D;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F4:
        scan = 0x3E;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F5:
        scan = 0x3F;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F6:
        scan = 0x40;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F7:
        scan = 0x41;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F8:
        scan = 0x42;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F9:
        scan = 0x43;
        lo = hi = 0;
        break;
    case SDL_SCANCODE_F10:
        scan = 0x44;
        lo = hi = 0;
        break;

    default:
        return 0;
    }

    /* Alt forces AL = 0 (BIOS reports the scan code alone for Alt combos). */
    if (mod & SDL_KMOD_ALT)
        return (uint16)scan << 8;
    /* Ctrl+letter -> control code 1..26 in AL. */
    if ((mod & SDL_KMOD_CTRL) && lo >= 'a' && lo <= 'z')
        return ((uint16)scan << 8) | (uint8)(lo - 'a' + 1);
    return ((uint16)scan << 8) | (uint8)((mod & SDL_KMOD_SHIFT) ? hi : lo);
}

/* Arrows carry no ASCII; the menus compare the full word against KEYCODE_*. In
 * flight the arrows drive the virtual stick instead, so they are not queued. */
static uint16 arrowWord(SDL_Scancode sc) {
    switch (sc) {
    case SDL_SCANCODE_UP:
        return KEYCODE_UPARROW;
    case SDL_SCANCODE_DOWN:
        return KEYCODE_DNARROW;
    case SDL_SCANCODE_LEFT:
        return KEYCODE_LEFTARROW;
    case SDL_SCANCODE_RIGHT:
        return KEYCODE_RIGHTARROW;
    default:
        return 0;
    }
}

/* A keydown in the menus: arrows navigate, Enter/Esc/Backspace and Ctrl+letter
 * carry the BIOS word, and printable characters are left to TEXT_INPUT so they
 * arrive already shifted/localized for pilot-name entry. */
static void menuKeyDown(const SDL_Event *ev) {
    SDL_Keymod mod = ev->key.mod;
    uint16 arrow = arrowWord(ev->key.scancode);
    if (arrow) {
        ringPush(arrow);
        return;
    }
    if ((mod & SDL_KMOD_ALT) && ev->key.key == SDLK_Q) {
        ringPush(KEYCODE_ALTQ); /* 0x1000: Q scan code in AH, AL = 0 */
        return;
    }
    switch (ev->key.key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        ringPush(0x1c00 | KEYCODE_ENTER); /* AL = 0x0d */
        break;
    case SDLK_ESCAPE:
        ringPush(0x0100 | KEYCODE_ESC); /* AL = 0x1b */
        break;
    case SDLK_BACKSPACE:
        ringPush(0x0e00 | KEYCODE_BACKSPACE); /* AL = 0x08 */
        break;
    default:
        /* Ctrl+letter -> control code in AL (SDL emits no text event for it). */
        if ((mod & SDL_KMOD_CTRL) && ev->key.key >= SDLK_A && ev->key.key <= SDLK_Z)
            ringPush((uint16)(ev->key.key - SDLK_A + 1));
        break;
    }
}

/* --- flight virtual stick + gamepad bindings ------------------------------- */

/* Virtual stick from the live key state. The original mapped the numeric keypad
 * and arrow keys to one held direction at a time; polling the held state
 * reproduces the same axis assignment and gives smooth diagonals for free.
 * Deflection matches the original first-press value (0x5A off centre). */
static void updateStick(void) {
    const bool *ks = SDL_GetKeyboardState(NULL);
    const Uint8 LO = 0x26, HI = 0xDA; /* centre 0x80 -/+ 0x5A */
    Uint8 x = 0x80;                   /* g_joyRawX = roll  (left = LO, right = HI) */
    Uint8 y = 0x80;                   /* g_joyRawY = pitch (up   = LO, down  = HI) */

    if (ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_KP_8]) y = LO;
    if (ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_KP_2]) y = HI;
    if (ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_KP_4]) x = LO;
    if (ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_KP_6]) x = HI;
    /* Keypad diagonals deflect both axes at once. */
    if (ks[SDL_SCANCODE_KP_7]) {
        y = LO;
        x = LO;
    }
    if (ks[SDL_SCANCODE_KP_9]) {
        y = LO;
        x = HI;
    }
    if (ks[SDL_SCANCODE_KP_1]) {
        y = HI;
        x = LO;
    }
    if (ks[SDL_SCANCODE_KP_3]) {
        y = HI;
        x = HI;
    }

    g_joyRawX = x;
    g_joyRawY = y;
}

/* Gamepad flight bindings ----------------------------------------------------
 * The left stick (roll/pitch) and the fire triggers (RT guns, LT missiles) are
 * read directly by the game through joystick.c. The remaining cockpit actions
 * are discrete keystrokes, so we feed the corresponding BIOS key word into the
 * same ring the keyboard uses: stepFlightModel reads one keyScancode per frame
 * and both it (throttle) and keyDispatch (egkeys.c: weapons, views,
 * countermeasures, ...) act on it. Everything is edge-triggered (one press =
 * one keystroke) except thrust, which auto-repeats while held.
 *
 * Button layout mirrors the cockpit's physical arrangement:
 *   Y  cycle weapon    X  afterburner    A  brake    B  designate target
 *   L1 chaff           R1 flare          Start autopilot   Select landing gear
 *   L3 cycle radar zoom    R3 eject
 *   D-pad up/down thrust +/-   D-pad left/right chase cam (F5) / target view
 *   (F10).  Right stick is a view hat: forward front, left/right/back look that
 *   way (forward also exits the F5/F10 cams). */
#define VIEW_FRONT_KEY 0x3920 /* space */
#define VIEW_LEFT_KEY 0x3c00  /* F2 */
#define VIEW_RIGHT_KEY 0x3d00 /* F3 */
#define VIEW_REAR_KEY 0x3e00  /* F4 */

static bool gamepadActive(void); /* defined with the menu poll below */

/* Rising edge of button b since the last poll. prev state is shared with the
 * thrust auto-repeat below; a button is driven by only one of the two. */
static bool gpPrev[SDL_GAMEPAD_BUTTON_COUNT];

static bool gpEdge(SDL_GamepadButton b) {
    bool now = joy_button(b);
    bool edge = now && !gpPrev[b];
    gpPrev[b] = now;
    return edge;
}

/* True on press and then every repeat interval while held, for throttle ramps. */
static bool gpHeldRepeat(SDL_GamepadButton b, Uint64 *nextRepeat) {
    bool now = joy_button(b);
    if (!now) {
        gpPrev[b] = false;
        return false;
    }
    Uint64 t = SDL_GetTicks();
    if (!gpPrev[b]) {
        gpPrev[b] = true;
        *nextRepeat = t + 250; /* hold this long before the first repeat */
        return true;
    }
    if (t >= *nextRepeat) {
        *nextRepeat = t + 100;
        return true;
    }
    return false;
}

/* Right stick is a 4-way view hat: push forward/left/right/back to look that
 * way (forward = front, which also drops you out of the F5/F10 external cams).
 * Acts only on zone transitions; a centred stick is neutral and makes no
 * change, so it leaves a D-pad-selected camera alone until you pick a view. */
static void rightStickView(void) {
    static int zone = 0; /* last view this stick selected; 0 = centred/neutral */
    const int GATE = 16000;
    int x = joy_axisRaw(SDL_GAMEPAD_AXIS_RIGHTX);
    int y = joy_axisRaw(SDL_GAMEPAD_AXIS_RIGHTY);
    int ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int z = 0;
    if (ay > GATE && ay >= ax)
        z = (y < 0) ? VIEW_FRONT_KEY : VIEW_REAR_KEY; /* push away / pull back */
    else if (ax > GATE)
        z = (x < 0) ? VIEW_LEFT_KEY : VIEW_RIGHT_KEY;

    if (z != zone) {
        if (z) ringPush(z); /* entering neutral leaves the current view as-is */
        zone = z;
    }
}

static void pollGamepadFlight(void) {
    /* Weapon-select keys 's'/'m'/'g' cycle missileSpecIndex 0->1->2; rotate
     * through them so one button toggles the loadout. */
    static const uint16 weaponKeys[3] = {0x1f73, 0x326d, 0x2267};
    static int weaponSel = 0;
    static Uint64 thrustUpAt, thrustDownAt;

    if (!joy_isGamepad()) return;
    if (gamepadActive()) g_lastWasGamepad = true;

    if (gpEdge(SDL_GAMEPAD_BUTTON_NORTH)) { /* Y: cycle weapon */
        ringPush(weaponKeys[weaponSel]);
        weaponSel = (weaponSel + 1) % 3;
    }
    if (gpEdge(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) ringPush(0x2e63);  /* L1: chaff ('c') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) ringPush(0x2166); /* R1: flare ('f') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_WEST)) ringPush(0x1e61);           /* X: afterburner ('a') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_SOUTH)) ringPush(0x3062);          /* A: brake ('b') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_EAST)) ringPush(0x1474);           /* B: designate target ('t') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_START)) ringPush(0x1970);          /* Start: autopilot ('p') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_BACK)) ringPush(0x266c);           /* Select: landing gear ('l') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_LEFT_STICK)) ringPush(0x1372);     /* L3: cycle radar zoom ('r') */
    if (gpEdge(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) ringPush(0x011b);    /* R3: eject (Esc) */

    if (gpEdge(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) ringPush(0x3f00);  /* D-pad left: chase cam (F5) */
    if (gpEdge(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) ringPush(0x4400); /* D-pad right: target view (F10) */
    rightStickView();

    /* Thrust ramps while up/down is held. */
    if (gpHeldRepeat(SDL_GAMEPAD_BUTTON_DPAD_UP, &thrustUpAt)) ringPush(0x0d3d);     /* '=' */
    if (gpHeldRepeat(SDL_GAMEPAD_BUTTON_DPAD_DOWN, &thrustDownAt)) ringPush(0x0c2d); /* '-' */
}

/* A trigger past this (of SDL's 0..32767) counts as a press; matches the fire
 * threshold in joystick.c so menu accept and flight fire agree. */
#define JOY_TRIGGER_GATE 16000

/* Rising edge of an analog trigger (the triggers are axes, not buttons, so they
 * need their own edge state separate from gpPrev). idx 0 = right, 1 = left. */
static bool gpTrigPrev[2];
static bool trigEdge(SDL_GamepadAxis trig, int idx) {
    bool now = joy_axisRaw(trig) > JOY_TRIGGER_GATE;
    bool edge = now && !gpTrigPrev[idx];
    gpTrigPrev[idx] = now;
    return edge;
}

/* One analog-stick axis as a repeating arrow: fires when it crosses into a
 * direction and then on an interval while held, recentring resets it. Gives the
 * menu cursor the hold-to-scroll the keyboard arrows get. */
static void stickArrowRepeat(int axisVal, uint16 negKey, uint16 posKey, int *zone, Uint64 *nextAt) {
    const int GATE = 16000;
    int z = (axisVal < -GATE) ? -1 : (axisVal > GATE) ? 1
                                                      : 0;
    if (z == 0) {
        *zone = 0;
        return;
    }
    Uint64 t = SDL_GetTicks();
    if (*zone != z) {
        *zone = z;
        *nextAt = t + 300; /* settle before the first repeat */
        ringPush(z < 0 ? negKey : posKey);
    } else if (t >= *nextAt) {
        *nextAt = t + 120;
        ringPush(z < 0 ? negKey : posKey);
    }
}

/* True while the gamepad is being actively used (a button held or a stick /
 * trigger off centre), so the flight controls can follow the last-used device. */
static bool gamepadActive(void) {
    const int DZ = 12000;
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        if (joy_button((SDL_GamepadButton)b)) return true;
    return SDL_abs(joy_axisRaw(SDL_GAMEPAD_AXIS_LEFTX)) > DZ ||
           SDL_abs(joy_axisRaw(SDL_GAMEPAD_AXIS_LEFTY)) > DZ ||
           SDL_abs(joy_axisRaw(SDL_GAMEPAD_AXIS_RIGHTX)) > DZ ||
           SDL_abs(joy_axisRaw(SDL_GAMEPAD_AXIS_RIGHTY)) > DZ ||
           joy_axisRaw(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > JOY_TRIGGER_GATE ||
           joy_axisRaw(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > JOY_TRIGGER_GATE;
}

/* Gamepad menu navigation, the single menu path: face A / Start / right trigger
 * confirm (Enter), B / left trigger cancel (Esc), the D-pad and left stick move
 * the cursor (arrows). The menu loops' own joystick reads are neutralised in
 * menu mode (joystick.c), so this is the only place that reads the pad for
 * menus - one press is one action on every screen, splash through debrief. */
static void pollGamepadMenu(void) {
    static int zoneX, zoneY;
    static Uint64 repX, repY;
    if (!joy_isGamepad()) return;
    if (gamepadActive()) g_lastWasGamepad = true;

    if (gpEdge(SDL_GAMEPAD_BUTTON_SOUTH) || gpEdge(SDL_GAMEPAD_BUTTON_START) ||
        trigEdge(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0))
        ringPush(0x1c00 | KEYCODE_ENTER);
    if (gpEdge(SDL_GAMEPAD_BUTTON_EAST) || trigEdge(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1))
        ringPush(0x0100 | KEYCODE_ESC);
    if (gpEdge(SDL_GAMEPAD_BUTTON_DPAD_UP)) ringPush(KEYCODE_UPARROW);
    if (gpEdge(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) ringPush(KEYCODE_DNARROW);
    if (gpEdge(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) ringPush(KEYCODE_LEFTARROW);
    if (gpEdge(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) ringPush(KEYCODE_RIGHTARROW);
    stickArrowRepeat(joy_axisRaw(SDL_GAMEPAD_AXIS_LEFTX), KEYCODE_LEFTARROW, KEYCODE_RIGHTARROW, &zoneX, &repX);
    stickArrowRepeat(joy_axisRaw(SDL_GAMEPAD_AXIS_LEFTY), KEYCODE_UPARROW, KEYCODE_DNARROW, &zoneY, &repY);
}

/* --- the single event pump ------------------------------------------------- */

void input_pumpEvents(void) {
    SDL_Event ev;
    /* Every key-polling wait loop funnels through here, so advance the game
     * clock too: this is what drives the tick counters those loops spin on, and
     * what keeps the window responsive on a poll-only frame. */
    timerPump();
    while (SDL_PollEvent(&ev)) {
        joy_handleEvent(&ev); /* device hotplug, every phase */
        /* Window / system events are handled here for every phase, before any
         * mode-specific game-input translation below: a window close, the
         * fullscreen toggle, focus, and the redraw-on-resize never reach the
         * game as keystrokes. Only after this does the pump feed context input
         * (flight controls / menu navigation / skip-screen). */
        switch (ev.type) {
        case SDL_EVENT_QUIT:
            /* A window close is an app-level quit intent, not a keystroke (the
             * "press any key to advance" screens would otherwise eat it as
             * advance). Quit the whole application gracefully from whatever
             * phase is running. */
            g_quitRequested = true;
            if (g_quitHandler) g_quitHandler(); /* teardown + exit; no return */
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            g_hasFocus = true;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            g_hasFocus = false;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_EXPOSED:
            /* Redraw the current frame so window changes (incl. the Alt+Enter
             * fullscreen toggle) are visible even on a static screen blocked in
             * a key-wait, which produces no game frame of its own. */
            gfx_repaint();
            break;
        case SDL_EVENT_KEY_DOWN:
            /* A real key hands flight control back to the keyboard (it stays
             * with the keyboard until the stick is moved again). */
            g_lastWasGamepad = false;
            /* Alt+Enter toggles fullscreen in every phase; swallow it so it
             * never reaches the ring as an Alt-shifted Return. */
            if (ev.key.scancode == SDL_SCANCODE_RETURN && (ev.key.mod & SDL_KMOD_ALT)) {
                gfx_toggleFullscreen();
                break;
            }
            if (g_mode == INPUT_MODE_FLIGHT) {
                uint16 word = biosWord(ev.key.scancode, ev.key.mod);
                if (word) ringPush(word);
            } else {
                menuKeyDown(&ev);
            }
            break;
        case SDL_EVENT_TEXT_INPUT:
            /* Printable characters for menu text entry arrive here already
             * shifted/localized; queue each ASCII byte in AL (AH = 0). Flight
             * takes its letters as commands from biosWord instead. */
            if (g_mode == INPUT_MODE_MENU) {
                const char *p = ev.text.text;
                for (; *p; ++p) {
                    unsigned char c = (unsigned char)*p;
                    if (c >= 0x20 && c < 0x80) ringPush(c);
                }
            }
            break;
        default:
            break;
        }
    }

    if (g_mode == INPUT_MODE_FLIGHT) {
        updateStick();
        pollGamepadFlight();
    } else {
        pollGamepadMenu();
    }
}
