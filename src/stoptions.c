/*
 * stoptions.c - optional gameplay settings on the pilot-selection screen.
 *
 * This module owns the entire new screen so the reconstructed menu routines
 * need only draw one gear and call one function. The underlying pilot screen is
 * captured and restored exactly, including the selected user's name.
 */
#include "stoptions.h"

#include "const.h"
#include "game_options.h"
#include "gfx.h"
#include "input.h"
#include "shared/common.h"
#include "stcode.h"
#include "stdata.h"
#include "stmissn.h"
#include "stpilot.h"

#include <stdio.h>
#include <string.h>

enum {
    GEAR_LEFT = 302,
    GEAR_TOP = 183,
    GEAR_RIGHT = 318,
    GEAR_BOTTOM = 198,
    PANEL_LEFT = 42,
    PANEL_TOP = 42,
    PANEL_RIGHT = 277,
    PANEL_BOTTOM = 157,
    OPTION_FIRST_Y = 70,
    OPTION_ROW_HEIGHT = 15
};

static const char *const g_optionLabels[GAME_OPTION_COUNT] = {
    "INFINITE AMMO / STORES",
    "INFINITE FUEL",
    "NO DAMAGE / CRASH"
};

static const char *g_activePilotName;
static int g_activeSelection;

/* Draw an unboxed native-resolution cog that remains legible at 320x200. */
void stOptionsDrawGear(int16 *page) {
    clearRect(page, GEAR_LEFT, GEAR_TOP, GEAR_RIGHT, GEAR_BOTTOM);
    drawLine(page, 307, 186, 313, 186, COLOR_WHITE);
    drawLine(page, 313, 186, 316, 189, COLOR_WHITE);
    drawLine(page, 316, 189, 316, 193, COLOR_WHITE);
    drawLine(page, 316, 193, 313, 196, COLOR_WHITE);
    drawLine(page, 313, 196, 307, 196, COLOR_WHITE);
    drawLine(page, 307, 196, 304, 193, COLOR_WHITE);
    drawLine(page, 304, 193, 304, 189, COLOR_WHITE);
    drawLine(page, 304, 189, 307, 186, COLOR_WHITE);

    drawLine(page, 309, 188, 311, 188, COLOR_LIGHTGRAY);
    drawLine(page, 311, 188, 314, 190, COLOR_LIGHTGRAY);
    drawLine(page, 314, 190, 314, 192, COLOR_LIGHTGRAY);
    drawLine(page, 314, 192, 312, 194, COLOR_LIGHTGRAY);
    drawLine(page, 312, 194, 308, 194, COLOR_LIGHTGRAY);
    drawLine(page, 308, 194, 306, 192, COLOR_LIGHTGRAY);
    drawLine(page, 306, 192, 306, 190, COLOR_LIGHTGRAY);
    drawLine(page, 306, 190, 309, 188, COLOR_LIGHTGRAY);

    drawLine(page, 309, 184, 311, 186, COLOR_WHITE);
    drawLine(page, 314, 187, 317, 186, COLOR_WHITE);
    drawLine(page, 316, 190, 318, 191, COLOR_WHITE);
    drawLine(page, 314, 195, 317, 196, COLOR_WHITE);
    drawLine(page, 309, 196, 311, 198, COLOR_WHITE);
    drawLine(page, 303, 196, 306, 195, COLOR_WHITE);
    drawLine(page, 302, 191, 304, 190, COLOR_WHITE);
    drawLine(page, 303, 186, 306, 187, COLOR_WHITE);
}

/* Return whether one logical 320x200 point is inside the settings gear. */
int stOptionsGearHit(int x, int y) {
    return x >= GEAR_LEFT && x <= GEAR_RIGHT &&
           y >= GEAR_TOP && y <= GEAR_BOTTOM;
}

/* Draw the modal with the compact font so later options fit without scrolling. */
static void drawOptionsPanel(const char *pilotName, int selected) {
    char title[48];
    int option;
    int oldColor = screenBuf[2];
    int oldFont = screenBuf[6];

    clearRect(screenBuf, PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM);
    drawLine(screenBuf, PANEL_LEFT, PANEL_TOP, PANEL_RIGHT, PANEL_TOP, COLOR_LIGHTGRAY);
    drawLine(screenBuf, PANEL_RIGHT, PANEL_TOP, PANEL_RIGHT, PANEL_BOTTOM, COLOR_LIGHTGRAY);
    drawLine(screenBuf, PANEL_RIGHT, PANEL_BOTTOM, PANEL_LEFT, PANEL_BOTTOM, COLOR_LIGHTGRAY);
    drawLine(screenBuf, PANEL_LEFT, PANEL_BOTTOM, PANEL_LEFT, PANEL_TOP, COLOR_LIGHTGRAY);

    screenBuf[6] = FONT_SMALL;
    screenBuf[2] = COLOR_WHITE;
    snprintf(title, sizeof(title), "OPTIONS - %s", pilotName && *pilotName ? pilotName : "PILOT");
    drawStringCentered(screenBuf, title, PANEL_LEFT, 51, PANEL_RIGHT);

    for (option = 0; option < GAME_OPTION_COUNT; option++) {
        int y = OPTION_FIRST_Y + option * OPTION_ROW_HEIGHT;
        screenBuf[2] = option == selected ? COLOR_WHITE : COLOR_LIGHTGRAY;
        drawStringAt(screenBuf, option == selected ? ">" : " ", 55, y);
        drawStringAt(screenBuf, g_optionLabels[option], 67, y);
        screenBuf[2] = gameOptionsEnabled((enum GameOption)option)
                           ? COLOR_LIGHTGREEN
                           : COLOR_LIGHTRED;
        drawStringAt(screenBuf,
                     gameOptionsEnabled((enum GameOption)option) ? "ON" : "OFF",
                     238, y);
    }

    screenBuf[2] = COLOR_LIGHTGRAY;
    drawStringCentered(screenBuf, "ARROWS SELECT  SPACE/CLICK TOGGLE  ESC CLOSE",
                       PANEL_LEFT, 143, PANEL_RIGHT);
    screenBuf[2] = oldColor;
    screenBuf[6] = oldFont;
    gfx_commitPage();
}

/*
 * Reproduce the modal after expose, resize, or fullscreen events. The GL menu
 * path submits some primitives immediately, so presenting the retained pilot
 * page alone would otherwise make the modal appear to vanish.
 */
static void repaintOptionsPanel(void) {
    drawOptionsPanel(g_activePilotName, g_activeSelection);
}

/* Return the option row under a logical point, or -1 outside all rows. */
static int optionAtPoint(int x, int y) {
    int option;
    if (x < PANEL_LEFT || x > PANEL_RIGHT) return -1;
    for (option = 0; option < GAME_OPTION_COUNT; option++) {
        int rowY = OPTION_FIRST_Y + option * OPTION_ROW_HEIGHT;
        if (y >= rowY - 3 && y <= rowY + 8) return option;
    }
    return -1;
}

/* Show the modal gameplay-options screen over the selected pilot background. */
void stOptionsShow(const char *pilotName) {
    struct R2DImage *background;
    int selected = 0;
    int done = 0;

    background = gfx_allocImage(LOGICAL_WIDTH, LOGICAL_HEIGHT);
    if (background) {
        gfx_captureToImage(background, screenBuf[0], 0, 0, 0, 0,
                           LOGICAL_WIDTH, LOGICAL_HEIGHT);
    }

    g_activePilotName = pilotName;
    g_activeSelection = selected;
    gfx_setRepaintHook(repaintOptionsPanel);
    drawOptionsPanel(pilotName, selected);
    while (!done) {
        int key = pollMenuInput();
        switch (key) {
        case KEYCODE_UPARROW:
            selected = (selected + GAME_OPTION_COUNT - 1) % GAME_OPTION_COUNT;
            g_activeSelection = selected;
            drawOptionsPanel(pilotName, selected);
            break;
        case KEYCODE_DNARROW:
            selected = (selected + 1) % GAME_OPTION_COUNT;
            g_activeSelection = selected;
            drawOptionsPanel(pilotName, selected);
            break;
        case KEYCODE_LEFTARROW:
            gameOptionsSet((enum GameOption)selected, false);
            drawOptionsPanel(pilotName, selected);
            break;
        case KEYCODE_RIGHTARROW:
            gameOptionsSet((enum GameOption)selected, true);
            drawOptionsPanel(pilotName, selected);
            break;
        case KEYCODE_ENTER:
        case ' ':
            gameOptionsToggle((enum GameOption)selected);
            drawOptionsPanel(pilotName, selected);
            break;
        case INPUT_MENU_MOUSE_CLICK: {
            int x;
            int y;
            int option;
            if (!input_takeMenuClick(&x, &y)) break;
            if (stOptionsGearHit(x, y)) {
                done = 1;
                break;
            }
            option = optionAtPoint(x, y);
            if (option >= 0) {
                selected = option;
                g_activeSelection = selected;
                gameOptionsToggle((enum GameOption)selected);
                drawOptionsPanel(pilotName, selected);
            }
            break;
        }
        case KEYCODE_ESC:
            done = 1;
            break;
        }
    }

    gfx_setRepaintHook(NULL);
    g_activePilotName = NULL;
    if (background) {
        gfx_restoreFromImage(background, screenBuf[0], 0, 0, 0, 0,
                             LOGICAL_WIDTH, LOGICAL_HEIGHT);
        gfx_freeImage(background);
    } else {
        displayPilots();
    }
    gfx_commitPage();
}
