/* Pointer-only adapters for the legacy menus. Keyboard handling and routine
 * order stay in the reconstructed files; highlights and animations are reused. */
#include "menu_pointer.h"
#include "input.h"
#include "stdata.h"
#include "sttypes.h"
#include "const.h"
#include "gfx.h"

void animateArm(int from, int to);

/* Hitboxes follow the original roster and briefing artwork. Upper bounds are
 * exclusive; the roster includes four pixels above each first name. */
enum MenuPointerGeometry {
    PILOT_HIT_TOP_PADDING = 4,
    MISSION_HIT_LEFT = 105,
    MISSION_HIT_TOP = 24,
    MISSION_ROW_HEIGHT = 21,
    MISSION_ROW_COUNT = 5,
    MISSION_HIT_BOTTOM = MISSION_HIT_TOP + MISSION_ROW_COUNT * MISSION_ROW_HEIGHT
};

/* Use the same roster rectangles and palette transition as keyboard arrows. */
static void highlightPilot(int index, int from, int to) {
    const int x = index < PILOTS_PER_COLUMN ? PILOT_COL_LEFT : PILOT_COL_RIGHT;
    const int y = (index % PILOTS_PER_COLUMN) * PILOT_ROW_HEIGHT + PILOT_TOP_MARGIN;
    gfx_switchColor(screenBuf, x, y, x + PILOT_ENTRY_WIDTH, y + PILOT_NAME_HEIGHT, from, to);
}

/* First click selects; a second on that slot confirms. A keyboard action
 * cancels the pending click so mixed input cannot accidentally confirm. */
int menu_pilotPointerInput(int action, int *pending) {
    if (action != INPUT_KEY_MENU_POINTER) { *pending = -1; return action; }
    int x = 0, y = 0;
    if (!input_takeMenuPointer(&x, &y)) return 0;
    int column = -1;
    if (x >= PILOT_COL_LEFT && x < PILOT_COL_LEFT + PILOT_ENTRY_WIDTH) column = 0;
    else if (x >= PILOT_COL_RIGHT && x < PILOT_COL_RIGHT + PILOT_ENTRY_WIDTH) column = 1;
    const int top = PILOT_TOP_MARGIN - PILOT_HIT_TOP_PADDING;
    if (column < 0 || y < top || y >= top + PILOTS_PER_COLUMN * PILOT_ROW_HEIGHT) return 0;
    const int slot = column * PILOTS_PER_COLUMN + (y - top) / PILOT_ROW_HEIGHT;
    if (slot != selectedPilotIdx) {
        highlightPilot(selectedPilotIdx, COLOR_WHITE, COLOR_LIGHTGRAY);
        selectedPilotIdx = slot;
        highlightPilot(slot, COLOR_LIGHTGRAY, COLOR_WHITE);
    }
    if (*pending == slot) return KEYCODE_ENTER;
    *pending = slot;
    return 0;
}

/* Ignore disabled rows. Selection moves the original arm; confirmation still
 * passes through missionMenuSelect's normal acceptance and exit animation. */
int menu_missionPointerInput(int action, int *selection, int *pending) {
    if (action != INPUT_KEY_MENU_POINTER) { *pending = -1; return action; }
    int x = 0, y = 0;
    if (!input_takeMenuPointer(&x, &y) ||
        x < MISSION_HIT_LEFT || x >= SCREEN_WIDTH ||
        y < MISSION_HIT_TOP || y >= MISSION_HIT_BOTTOM) return 0;
    const int row = (y - MISSION_HIT_TOP) / MISSION_ROW_HEIGHT;
    if (scenarioFoundArr[row] != 0) return 0;
    if (row != *selection) {
        timerCounter3 = 6;
        animateArm(*selection, row);
        *selection = row;
    }
    if (*pending == row) return KEYCODE_ENTER;
    *pending = row;
    return 0;
}
