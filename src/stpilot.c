/* Pilot selection and display */
#include "stcode.h"
#include "stdata.h"
#include "stgen.h"
#include "stmissn.h"
#include "stpilot.h"
#include "stsprit.h"
#include "sttypes.h"
#include "const.h"
#include "gfx.h"
#include "input.h"
#include "slot.h"
#include "comm.h"
#include "offsets.h"
#include "shared/common.h"
#include "log.h"

#include <stdio.h>
#include <dos.h>
#include <string.h>

/* Private helpers for this translation unit. */
int16 doFcbSearch();
void updateHallfame();
void displayPilots(void);
void printPilot(int);
void processPilotInput();
void gameDataToPilot(struct Pilot *pilot);
void pilotToGameData(const uint8 *pilotData);
void pilotNameInput(int16 *, int, int, int, struct Pilot *);
void loadHallfame(void);
void saveHallfame();
int getJoyKey();
int readInputKey();

/* Restore the pilot-selection prompt after transient name editing. */
static void redrawPilotSelectorPrompt(int16 *page) {
    int oldBg = page[3];
    int oldColor = page[2];
    int oldFont = page[6];
    page[3] = 0;
    clearRect(page, 15, 192, 303, 197);
    gfx_invalidateTtfTextOverlayRect(15, 190, 303, 199);
    page[2] = COLOR_LIGHTRED;
    page[6] = 3;
    drawStringCentered(page, "Use SELECTOR to choose pilot,  ESC to enter new pilot.", 0, 192, 320);
    page[2] = oldColor;
    page[6] = oldFont;
    page[3] = oldBg;
}

/* Redraw the name-entry prompt and current UTF-8 pilot name without stale overlays. */
static void redrawPilotNamePrompt(int16 *page) {
    int oldBg = page[3];
    int oldColor = page[2];
    int oldFont = page[6];
    page[3] = 0;
    clearRect(page, 15, 192, 303, 197);
    gfx_invalidateTtfTextOverlayRect(0, 190, SCREEN_MAXX, 199);
    page[2] = COLOR_LIGHTRED;
    page[6] = 3;
    drawStringCentered(page, "ENTER YOUR NAME !", 15, 192, 289);
    page[2] = oldColor;
    page[6] = oldFont;
    page[3] = oldBg;
}

void pilotSelect(int16 needSplash) {
    int unused;
    gfx_waitRetrace();
    loadHallfame();
    if (needSplash == 0) {
        updateHallfame();
    }
    gfx_setFadeSteps(4);
    loadPic("ArmPiece.Pic", menuSprites);
    gfx_setFadeSteps(7);
    openShowPic("HiScore.Pic", *screenBuf);
    displayPilots();
    gfx_setDac(1);
    gfx_flipPage();
    processPilotInput();
    pilotToGameData((uint8 *)&hallfameBuf[selectedPilotIdx]);
    screenBuf[3] = 0;
    clearRect(screenBuf, 0, 0, SCREEN_MAXX, SCREEN_MAXY);
}

void updateHallfame() {
    int shiftIdx;
    if (gameData->hallOfFameEligible != 0) {
        selectedPilotIdx = HALLFAME_SLOTS - 2;
        for (; selectedPilotIdx >= 0; selectedPilotIdx--) {
            if (hallfameBuf[selectedPilotIdx].total_score >= gameData->totalScore) break;
        }
        selectedPilotIdx++;
        for (shiftIdx = gameData->pilotIdx - 1; shiftIdx >= selectedPilotIdx; shiftIdx--) {
            hallfameBuf[shiftIdx + 1] = hallfameBuf[shiftIdx];
        }
    } else {
        selectedPilotIdx = gameData->pilotIdx;
    }
    gameDataToPilot(&hallfameBuf[selectedPilotIdx]);
    if (doFcbSearch() != 0) {
        saveHallfame();
        return;
    }
    screenBuf[3] = 0;
    clearRect(screenBuf, 0, 0, SCREEN_MAXX, SCREEN_MAXY);
    gfx_commitPage();
    screenBuf[2] = COLOR_WHITE;
    drawStringCentered(screenBuf, "Original Disk in drive.  Roster will not be saved.", 0, 100, 320);
    drawStringCentered(screenBuf, "Press a key to continue.", 0, 150, 320);
    screenBuf[2] = COLOR_LIGHTGRAY;
    gfx_flipPage();
    misc_getKey();
    gfx_waitRetrace();
}

void displayPilots(void) {
    int pilotIdx;
    screenBuf[3] = 0;
    pilotIdx = 0;
    do {
        printPilot(pilotIdx);
    } while (++pilotIdx < HALLFAME_SLOTS);
    redrawPilotSelectorPrompt(screenBuf);
    gfx_commitPage();
}

void printPilot(int pilotIdx) {
    struct Pilot *pilot;
    int xPos;
    int yPos;
    int medalIdx;
    int totalMedalWidth;
    pilot = &hallfameBuf[pilotIdx];
    xPos = (pilotIdx < PILOTS_PER_COLUMN) ? PILOT_COL_LEFT : PILOT_COL_RIGHT;
    yPos = ((pilotIdx & (PILOTS_PER_COLUMN - 1)) * PILOT_ROW_HEIGHT) + PILOT_TOP_MARGIN;
    clearRect(screenBuf, xPos, yPos - 1, xPos + PILOT_ENTRY_WIDTH, yPos + 32);
    screenDesc.color = (pilotIdx == selectedPilotIdx) ? COLOR_WHITE : COLOR_LIGHTGRAY;
    mystrcpy(todayMissStrBuf, ranks[pilot->rank & 0xf]);
    mystrcat(todayMissStrBuf, pilot->name);
    drawStringCentered(screenBuf, todayMissStrBuf, xPos, yPos, 144);
    screenDesc.color = COLOR_LIGHTRED;
    screenDesc.font = 4;
    my_ltoa(pilot->total_score, todayMissStrBuf);
    mystrcat(todayMissStrBuf, " (");
    my_itoa(pilot->last_score, &todayMissStrBuf[mystrlen(todayMissStrBuf)]);
    mystrcat(todayMissStrBuf, ")");
    drawStringCentered(screenBuf, todayMissStrBuf, xPos, yPos + 9, 144);
    screenDesc.font = 1;
    for (medalIdx = 0, totalMedalWidth = 0; medalIdx < 7; medalIdx++) {
        if ((pilot->medals & (1 << medalIdx)) == 0) continue;
        totalMedalWidth += medalWidth[medalIdx] + 4;
    }
    xPos += (144 - totalMedalWidth) / 2;
    yPos += 17;
    medalIdx = 0;
    // display medals
    do {
        if ((pilot->medals & (1 << medalIdx)) == 0) continue;
        showSprite(screenBuf[0], xPos, yPos, medalSpriteX[medalIdx], medalSpriteY[medalIdx], medalWidth[medalIdx], 16);
        xPos += medalWidth[medalIdx] + 4;
    } while (++medalIdx < 7);
}

/* ---- merged from stpinp.c ---- */
void processPilotInput() {
    int prevIdx;
    int xPos;
    int shiftIdx;
    int yPos;
    pilotSelectFlag = 1;
    setTimerIrqHandler();
    while (prevIdx = selectedPilotIdx, true) switch (pollMenuInput()) {
        case KEYCODE_ENTER:
            if ((hallfameBuf[selectedPilotIdx].medals & 0x60) == 0) {
                restoreTimerIrqHandler();
                pilotSelectFlag = 0;
                return;
            }
            putch(CHAR_BELL);
            continue;
        case KEYCODE_ESC:
            hallfameBuf[selectedPilotIdx].theater = hallfameBuf[selectedPilotIdx].difficulty = hallfameBuf[selectedPilotIdx].total_score = hallfameBuf[selectedPilotIdx].last_score = hallfameBuf[selectedPilotIdx].medals = hallfameBuf[selectedPilotIdx].rank = 0;
            pilotNameInput(screenBuf, MAX_PILOT_NAME_LEN, PILOT_NAME_HEIGHT, PILOT_NAME_HEIGHT, &hallfameBuf[selectedPilotIdx]);
            pilotToGameData((uint8 *)&hallfameBuf[selectedPilotIdx]);
            shiftIdx = selectedPilotIdx;
            while (shiftIdx < HALLFAME_SLOTS - 1) {
                hallfameBuf[shiftIdx] = hallfameBuf[shiftIdx + 1];
                shiftIdx++;
            }
            gameDataToPilot(&hallfameBuf[HALLFAME_SLOTS - 1]);
            selectedPilotIdx = HALLFAME_SLOTS - 1;
            displayPilots();
            if (doFcbSearch() != 0) {
                saveHallfame();
            }
            redrawPilotSelectorPrompt(screenBuf);
            gfx_commitPage();
            continue;
        case KEYCODE_UPARROW:
            selectedPilotIdx--;
            goto handleArrow;
        case KEYCODE_DNARROW:
            selectedPilotIdx++;
            goto handleArrow;
        case KEYCODE_LEFTARROW:
            selectedPilotIdx -= PILOTS_PER_COLUMN;
            goto handleArrow;
        case KEYCODE_RIGHTARROW:
            selectedPilotIdx += PILOTS_PER_COLUMN;
        handleArrow:
            selectedPilotIdx &= HALLFAME_SLOTS - 1;
            xPos = (prevIdx < PILOTS_PER_COLUMN) ? PILOT_COL_LEFT : PILOT_COL_RIGHT;
            yPos = ((prevIdx & (PILOTS_PER_COLUMN - 1)) * PILOT_ROW_HEIGHT) + PILOT_TOP_MARGIN;
            // looks like ChangeColor() from library.h?
            gfx_switchColor(screenBuf, xPos, yPos, xPos + PILOT_ENTRY_WIDTH, yPos + PILOT_NAME_HEIGHT, COLOR_WHITE, COLOR_LIGHTGRAY);
            xPos = (selectedPilotIdx < PILOTS_PER_COLUMN) ? PILOT_COL_LEFT : PILOT_COL_RIGHT;
            yPos = ((selectedPilotIdx & (PILOTS_PER_COLUMN - 1)) * PILOT_ROW_HEIGHT) + PILOT_TOP_MARGIN;
            gfx_switchColor(screenBuf, xPos, yPos, xPos + PILOT_ENTRY_WIDTH, yPos + PILOT_NAME_HEIGHT, COLOR_LIGHTGRAY, COLOR_WHITE);
        }
}

void blinkPilot() {
    int16 xPos, yPos;
    if (pilotSelectFlag == 0) return;
    waitMdaCgaStatus(6);
    xPos = selectedPilotIdx < PILOTS_PER_COLUMN ? PILOT_COL_LEFT : PILOT_COL_RIGHT;
    yPos = ((selectedPilotIdx & (PILOTS_PER_COLUMN - 1)) * PILOT_ROW_HEIGHT) + PILOT_TOP_MARGIN;
    gfx_switchColor(screenBuf, xPos, yPos, xPos + PILOT_ENTRY_WIDTH, yPos + PILOT_NAME_HEIGHT, blinkColors[blinkColorIdx], blinkColors[blinkColorIdx ^ 1]);
    blinkColorIdx ^= 1;
    gfx_commitPage();
}

void gameDataToPilot(struct Pilot *pilot) {
    // uint16 var_4;
    int charIdx;
    for (charIdx = 0; (pilot->name[charIdx] = gameData->pilotName[charIdx]); charIdx++) {
    }
    pilot->total_score = gameData->totalScore;
    pilot->last_score = gameData->lastScore;
    pilot->theater = gameData->theater;
    pilot->difficulty = gameData->difficulty;
    pilot->rank = (gameData->rankHigh * 64) + gameData->rank;
    pilot->medals = (uint8)((gameData->campaignProgress == 2) ? 0x20 : 0) + (uint8)((gameData->campaignProgress == 1) ? 0x40 : 0) + gameData->medals;
}

// TODO: change argument to struct Pilot
void pilotToGameData(const uint8 *pilotData) {
    int charIdx;
    for (charIdx = 0; 1; charIdx++) {
        if ((gameData->pilotName[charIdx] = pilotData[charIdx]) == '\0') break;
    }
    gameData->totalScore = rdU32(pilotData + ROSTER_SCORE_LO);
    gameData->lastScore = rdU16(pilotData + ROSTER_LASTSCORE);
    gameData->theater = *(pilotData + ROSTER_THEATER);
    gameData->difficulty = *(pilotData + ROSTER_DIFFICULTY);
    gameData->rank = *(pilotData + ROSTER_UNK1) & 0xf;
    gameData->medals = *(pilotData + ROSTER_UNK2) & 0x1f;
    gameData->rankHigh = *(pilotData + ROSTER_UNK1) >> 6;
    gameData->campaignProgress = 0;
    gameData->pilotIdx = selectedPilotIdx;
}

void pilotNameInput(int16 *page, int a, int b, int c, struct Pilot *pilot) {
    int blinkToggle;
    int xPos, yPos;
    int nameLen = 0;
    int cursorX;
    uint16 keyCode;
    int rankWidth;
    blinkToggle = 0;
    xPos = (selectedPilotIdx < PILOTS_PER_COLUMN) ? PILOT_COL_LEFT : PILOT_COL_RIGHT;
    yPos = ((selectedPilotIdx & (PILOTS_PER_COLUMN - 1)) * PILOT_ROW_HEIGHT) + PILOT_TOP_MARGIN;
    page[2] = COLOR_WHITE;
    page[6] = 1;
    clearRect(page, xPos, yPos, xPos + PILOT_ENTRY_WIDTH, yPos + 35);
    gfx_invalidateTtfTextOverlayRect(xPos, yPos - 2, xPos + PILOT_ENTRY_WIDTH, yPos + 35);
    drawStringAt(page, ranks[0], xPos, yPos);
    xPos += (rankWidth = stringWidth(page, ranks[0]));
    rankWidth = PILOT_ENTRY_WIDTH - rankWidth;
    redrawPilotNamePrompt(page);
    misc_clearKeyFlags();
    keyCode = KEYCODE_CTRLX;
    do {
        switch (keyCode) {
        case KEYCODE_CTRLX:
            nameLen = 0;
            pilot->name[0] = '\0';
            page[2] = COLOR_WHITE;
            clearRect(page, xPos, yPos, xPos + rankWidth, yPos + c);
            gfx_invalidateTtfTextOverlayRect(xPos, yPos - 2, xPos + rankWidth, yPos + c + 8);
            drawStringAt(page, pilot->name, xPos, yPos);
            cursorX = page[4];
            break;
        case 8: // backspace
            if (nameLen > 0) {
                do {
                    nameLen--;
                } while (nameLen > 0 && (((uint8)pilot->name[nameLen] & 0xc0) == 0x80));
                pilot->name[nameLen] = '\0';
                // 2403 - duplicate code block coalesced with above
                page[2] = COLOR_WHITE;
                clearRect(page, xPos, yPos, xPos + rankWidth, yPos + c);
                gfx_invalidateTtfTextOverlayRect(xPos, yPos - 2, xPos + rankWidth, yPos + c + 8);
                drawStringAt(page, pilot->name, xPos, yPos);
                cursorX = page[4];
            }
            break;
        default:
            {
                char utf8[8]{};
                int utf8Len = input_readMenuTextUtf8(utf8, sizeof(utf8));
                if (utf8Len == 1 && (uint8)utf8[0] >= 0x20 && (uint8)utf8[0] < 0x80) {
                    input_discardNextAsciiKey((uint8)utf8[0]);
                }
                if (utf8Len == 0 && keyCode >= 0x20 && keyCode <= 0x7f) {
                    utf8[0] = (char)keyCode;
                    utf8[1] = '\0';
                    utf8Len = 1;
                }
                if (utf8Len > 0 && nameLen + utf8Len < a && stringWidth(page, pilot->name) <= 144) {
                    memcpy(&pilot->name[nameLen], utf8, (size_t)utf8Len);
                    nameLen += utf8Len;
                    pilot->name[nameLen] = '\0';
                    page[2] = COLOR_WHITE;
                    clearRect(page, xPos, yPos, xPos + rankWidth, yPos + c);
                    gfx_invalidateTtfTextOverlayRect(xPos, yPos - 2, xPos + rankWidth, yPos + c + 8);
                    drawStringAt(page, pilot->name, xPos, yPos);
                    cursorX = page[4];
                }
            }
            break;
        }
        redrawPilotNamePrompt(page);
        gfx_commitPage();
        while (getJoyKey() == 0) {
            waitMdaCgaStatus(3);
            gfx_switchColor(page, xPos, yPos - 1, xPos + rankWidth, yPos + c,
                            pilotNameInputColors[blinkToggle], pilotNameInputColors[blinkToggle ^ 1]);
            blinkToggle ^= 1;
            page[3] = pilotNameInputColors[blinkToggle];
            redrawPilotNamePrompt(page);
            gfx_commitPage();
        }
        keyCode = readInputKey();
        if ((keyCode & 0xff) != 0) {
            keyCode &= 0xff;
        }
        if (keyCode == KEYCODE_ENTER) {
            screenBuf[3] = 0;
            clearRect(page, 15, 192, 303, 197);
            gfx_invalidateTtfTextOverlayRect(0, 188, SCREEN_MAXX, SCREEN_MAXY);
            return;
        }
    } while (true);
}

void loadHallfame(void) {
    int slotIdx;
    SDL_IOStream *handle;
    handle = openFile("HallFame", 0);
    fileRead(&selectedPilotIdx, 2, 1, handle);
    slotIdx = 0;
    do {
        fileRead(hallfameBuf + slotIdx, HALLFAME_RECORDSZ, 1, handle);
        slotIdx++;
    } while (slotIdx < HALLFAME_SLOTS);
    fileClose(handle);
}

void saveHallfame() {
    SDL_IOStream *fp;
    int idx;
    fp = createFile("HallFame", 0);
    fileWrite(&selectedPilotIdx, 2, 1, fp);
    idx = 0;
    do {
        fileWrite(&hallfameBuf[idx], HALLFAME_RECORDSZ, 1, fp);
    } while (++idx < HALLFAME_SLOTS);
    fileClose(fp);
}

int getJoyKey() {
    if (commData->setupUseJoy == 1) {
        if (misc_readJoystick(0) != 0) return 1;
    }
    if (cbreakHit != 0) {
        cleanup();
        restoreCbreakHandler();
        exit(0);
    }
    return misc_checkKeyBuf() == 0 || input_menuTextWaiting();
}

/* ---- merged from stinkey.c ---- */
int readInputKey() {
    int key;
    if (commData->setupUseJoy == 1) {
        do {
            if (misc_checkKeyBuf() == 0) break;
        } while (misc_readJoystick(0) == 0);
        if (misc_checkKeyBuf() != 0) {
            key = KEYCODE_ENTER;
            goto checkKey;
        }
    }
    key = input_menuTextWaiting() ? 0 : misc_getKey();
checkKey:
    if (key == KEYCODE_ALTQ || cbreakHit != 0) {
        cleanup();
        if (cbreakHit != 0) restoreCbreakHandler();
        exit(0);
    }
    return key;
}
