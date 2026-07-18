#include "inttype.h"
#include "offsets.h"
#include "comm.h"
#include "shared/common.h"
#include "gfx.h"
#include "slot.h"
#include "const.h"

#include "log.h"
#include <SDL3/SDL.h>
#include "stcode.h"
#include "stdata.h"
#include "stgen.h"
#include "stmissn.h"
#include "shared/blackbox.h"
#include "stpilot.h"
#include "stsprit.h"
#include "sttypes.h"
#include "hdsprite.h"
#include "r2d.h"

#include <stdio.h>
#include <dos.h>

/* Private helpers for this translation unit. */
void waitJoyKey(void);
int joyOrKey();
void drawLine(const int16 *pageNum, int x1, int y1, int x2, int y2, int color);
int missionMenuSelect(const char **names, const char **desc, const char *title, int s);
void animateArm(int, int);
void clearBriefing(void);
static void briefingRepaint(void);
static void briefingScenePresent(void);
static void briefingSceneBegin(void);

/* --- Briefing scene: a recorded 2D draw list, replayed layered each present -----
 *
 * On a native-overlay backend with the HD briefing wall present, the board is a
 * per-frame layered scene: HD room backdrop -> menu text -> row highlight -> pointer
 * arm, drawn in that order every present. The menu text can no longer bake into the
 * page (the opaque HD room would cover it), so every string/line issued while the
 * board is built is recorded here and re-issued INSIDE the vector frame, where
 * drawStringCore submits each glyph as a native-resolution point on top of the room.
 * The screen is thus a pure function of (recorded board + armPosition + highlight),
 * so the repaint hook reproduces it on expose/focus with no save-under.
 *
 * Without the HD wall (software backend, or no asset present) the recorder is inert
 * and the legacy page path runs unchanged — byte-identical to upstream. */
typedef struct {
    char text[72];
    int16 x, y, color, font;
} BriefStr;
typedef struct {
    int16 x1, y1, x2, y2, color;
} BriefLine;
#define BRIEF_MAX_STR 64
#define BRIEF_MAX_LINE 8
static BriefStr g_briefStr[BRIEF_MAX_STR];
static BriefLine g_briefLine[BRIEF_MAX_LINE];
static int g_briefStrN, g_briefLineN;
static int g_briefRecording; /* the board is being recorded for HD replay */

/* Registered as g_textRecorder while a briefing screen is up (HD path only). */
static void briefRecordString(const char *s, int x, int y, int color, int font) {
    BriefStr *c;
    if (!g_briefRecording || g_briefStrN >= BRIEF_MAX_STR || !s) return;
    c = &g_briefStr[g_briefStrN++];
    SDL_strlcpy(c->text, s, sizeof(c->text));
    c->x = (int16)x;
    c->y = (int16)y;
    c->color = (int16)color;
    c->font = (int16)font;
}

void briefingSceneEnd(void) {
    g_briefRecording = 0;
    g_textRecorder = NULL;
    gfx_setRepaintHook(NULL);
}

static void briefingSceneBegin(void) {
    g_briefStrN = 0;
    g_briefLineN = 0;
    /* Record + layer only when the HD room is available; otherwise the legacy page
     * path handles everything and recording would be dead work. */
    g_briefRecording = hdsprite_hasBriefingWall();
    g_textRecorder = g_briefRecording ? briefRecordString : NULL;
    gfx_setRepaintHook(briefingRepaint);
}

/* Rebuild and present the whole HD briefing board: room backdrop, then the recorded
 * menu text (native-res points on top of the room, the selected row's description
 * recoloured to the highlight), then the pointer arm for the current position. */
static void briefingScenePresent(void) {
    int i, hlRow;
    hlRow = (enableHighlight != 0 && armPosition >= 0 && armPosition < 5) ? armPosition : -1;
    r2d_vectorBeginFrame(R2D_COMPOSE_SELF);
    hdsprite_drawBriefingWall();
    for (i = 0; i < g_briefLineN; i++)
        r2d_submitLine(g_briefLine[i].x1, g_briefLine[i].y1,
                       g_briefLine[i].x2, g_briefLine[i].y2, g_briefLine[i].color);
    for (i = 0; i < g_briefStrN; i++) {
        int color = g_briefStr[i].color;
        /* Highlight = the selected row's description (drawn NORMAL in the
         * [row*21+34 .. +42] band) recoloured, matching the legacy gfx_switchColor. */
        if (hlRow >= 0 && color == COLOR_BRIEF_DESC_NORMAL &&
            g_briefStr[i].y >= hlRow * 21 + 34 && g_briefStr[i].y <= hlRow * 21 + 42)
            color = COLOR_BRIEF_DESC_HL;
        page1NumPtr[2] = (int16)color;
        page1NumPtr[6] = g_briefStr[i].font;
        /* Call the glyph engine directly (not drawStringAt) so the replay does not
         * feed the recorder; a vector frame is active, so it submits native points. */
        page1NumPtr[4] = g_briefStr[i].x;
        page1NumPtr[5] = g_briefStr[i].y;
        gfx_drawString(page1NumPtr, g_briefStr[i].text);
    }
    if (armPosition >= 0 && armPosition <= 6)
        hdsprite_drawBriefingArm(armPosition);
    gfx_commitPage();
}

void clearKeybuf() {
    while (misc_checkKeyBuf() == 0) {
        misc_getKey();
    }
}

void waitJoyKey(void) {
    while (joyOrKey() == 0) {}
}

int joyOrKey() {
    if (commData->setupUseJoy == 1) {
        if (misc_readJoystick(0) != 0) {
            return 1;
        }
    }
    if (cbreakHit != 0) {
        cleanup();
        restoreCbreakHandler();
        exit(0);
    }
    if (misc_checkKeyBuf() != 0) {
        return 0;
    }
    // alt-q hit check
    if (misc_getKey() == KEYCODE_ALTQ) {
        cleanup();
        exit(0);
    }
    return 1;
}

void waitMdaCgaStatus(int16 iter) {
    if (!blackbox_fastForwarding()) SDL_Delay(1000 * iter / 60);
}

void drawLine(const int16 *pageNum, int x1, int y1, int x2, int y2, int color) {
    (void)pageNum; /* single back buffer now */
    if (g_briefRecording && g_briefLineN < BRIEF_MAX_LINE) {
        BriefLine *l = &g_briefLine[g_briefLineN++];
        l->x1 = (int16)x1;
        l->y1 = (int16)y1;
        l->x2 = (int16)x2;
        l->y2 = (int16)y2;
        l->color = (int16)color;
    }
    gfx_setColor(color);
    lineX1 = x1;
    lineY1 = y1;
    lineX2 = x2;
    lineY2 = y2;
    drawLineWrapper();
    gfx_nop23();
}

void showPic640(const char *filename) {
    SDL_IOStream *fileHandle;
    intRegs[1] = INT_VID_MODESET;
    intRegs[0] = MODE_640_350;
    intDispatch(IRQ_VIDEO, intRegs, intRegs);
    gfx_setDac(0);
    fileHandle = openFileWrapper(filename, 0);
    picBlit(fileHandle, 0);
    closeFileWrapper(fileHandle);
}

/* ---- merged from stmissn.c ---- */
void missionSelect() {
    int index, count;
    gfx_setDac(1);
    gfx_setFadeSteps(0);
    openShowPic("Wall.Pic", *page1NumPtr);
    briefingSceneBegin(); /* start recording the board so it can be layered over the HD room */
    clearBriefing();
    nearmemset(scenarioFoundArr, 0, 5);
    gameData->difficulty = missionMenuSelect(missDiffLevels, missDiffDesc, "DIFFICULTY", gameData->difficulty);
selectTheater:
    if (gameData->theater > 4)
        gameData->theater = 4;
    checkDiskA();
    nearmemset(scenarioFoundArr, 0, 5);
    gameData->theater = missionMenuSelect(missTheaNames, missTheaDesc, "THEATER", gameData->theater);
    if (gameData->theater == THEATER_OTHER) {            // other scenario selected
        for (count = 4, index = 0; index < 4; index++) { // find extra scenarios
            plh3d3Ptr[0] = *scenarioCodePtr[index];
            plh3d3Ptr[1] = *(scenarioCodePtr[index] + 1);

            if ((scenarioFoundArr[index] = ((fileHandle = openFile(plh3d3Ptr, 0)) == NULL))) count--;
            fileClose(fileHandle);
        }
        if (count == 0) { // no scenarios found, print message and go back to previous screen
            clearBriefing();
            drawStringCentered(page1NumPtr, "No scenario files found", 113, 60, 185);
            drawStringCentered(page1NumPtr, "See Technical Supplement", 113, 72, 185);
            enableHighlight = 0;
            timerCounter3 = 6;
            animateArm(armPosition, armPosition);
            waitJoyKey();
            goto selectTheater;
        }
        gameData->theater = missionMenuSelect(missScenarioNames, missScenarioDesc, "THEATER", 0) + 4;
        if (gameData->theater == 8) {
            goto selectTheater;
        }
    }

    // show mission type dialog for desert storm
    if (gameData->theater == THEATER_DS && gameData->difficulty != DIFFICULTY_DEMO) {
        scenarioFoundArr[0] = scenarioFoundArr[1] = 0;
        scenarioFoundArr[2] = scenarioFoundArr[3] = scenarioFoundArr[4] = 1;
        if (missionMenuSelect(missTypeNames, missTypeDesc, "MISSION TYPE", 0) == 0) {
            nearmemset(scenarioFoundArr, 0, 5);

            do {
                if ((missionPick = missionMenuSelect(missHistorical1Names, missHistorical1Desc, missionStr, 4)) != 4) break;
            } while ((missionPick = missionMenuSelect(missHistorical2Names, missHistorical2Desc, missionStr, 4) + 4) == 8);
        }
    }
    /* The board (and its recorder + repaint hook) stays live through the mission
     * printout and disk prompt; start_main ends the scene once START is done. */
}

int missionMenuSelect(const char **names, const char **desc, const char *title, int selection) {
    int yPos, row, action;
    enableHighlight = 1;
    page1Desc.color = COLOR_BLUE;
    drawStringCentered(page1NumPtr, title, 113, 14, 185);
    drawLine(page1NumPtr, 173, 22, 235, 22, 1);
    yPos = 26;
    for (row = 0; row < 5; row++) {
        if (scenarioFoundArr[row] == 0) {
            page1Desc.color = COLOR_BLUE;
            drawStringCentered(page1NumPtr, names[row], 113, yPos, 185);
            page1Desc.font = FONT_SMALL;
            page1Desc.color = COLOR_BRIEF_DESC_NORMAL;
            drawStringCentered(page1NumPtr, desc[row], 113, yPos + 8, 185);
            page1Desc.font = FONT_NORMAL;
        }
        yPos += 21;
    }
    setTimerIrqHandler();
    timerCounter3 = 6;
    animateArm(-1, 6);
    for (row = 5; row >= selection; row--) {
        animateArm(row + 1, row);
    }
    do {
    again:
        if ((action = pollMenuInput()) != KEYCODE_ENTER) {
            if (action == KEYCODE_UPARROW) {
                if (selection > 0) {
                    timerCounter3 = 6;
                    animateArm(selection, selection - 1);
                    selection--;
                }
            } else if (action == KEYCODE_DNARROW && selection < 4) {
                timerCounter3 = 6;
                animateArm(selection, selection + 1);
                selection++;
            }
            goto again;
        }
    } while (scenarioFoundArr[selection] != 0);
    timerCounter3 = 6;
    // animation to lower the arm after accepting?
    for (row = selection + 1; row <= 6; row++) {
        animateArm(row - 1, row);
    }
    restoreTimerIrqHandler();
    clearBriefing();
    return selection;
}

/* Reproduce the briefing's current frame on an expose/focus repaint (registered as
 * the gfx repaint hook while a mission-select screen is up). On the HD path the whole
 * board is a per-frame scene, so just rebuild it. On the legacy page path animateArm
 * leaves the page holding the clean board (the arm is a transient draw), so redraw
 * the current arm pose over it without advancing any state. */
static void briefingRepaint(void) {
    int b = armPosition, idx;
    if (hdsprite_hasBriefingWall()) {
        briefingScenePresent();
        return;
    }
    if (b < 0 || b > 6) {
        gfx_commitPage();
        return;
    }
    idx = armSpriteIndex[b];
    showSprite(*page1NumPtr, armBlitX[idx], armBlitY[idx], armSrcX[idx], armSrcY[idx], armBlitW[idx], armBlitH[idx]);
    gfx_commitPage();
    gfx_restoreFromImage(g_stBacking, *page1NumPtr, armBlitX[idx], armBlitY[idx], armBlitX[idx], armBlitY[idx], armBlitW[idx], armBlitH[idx]);
}

void animateArm(int a, int b) {
    int spriteIdx;
    while (timerCounter3 < 6) timerYield();
    timerCounter3 = 0;
    armPosition = b;
    if (hdsprite_hasBriefingWall()) {
        /* HD path: rebuild the whole board each present (room -> text -> highlight
         * -> arm). No page save-under and no page highlight recolour — the scene is
         * a pure function of the recorded board + armPosition + enableHighlight. */
        (void)a;
        briefingScenePresent();
        return;
    }
    /* Legacy page path (software / no HD art) — byte-identical to upstream. */
    spriteIdx = armSpriteIndex[b];
    if (a == -1) {
        /* Snapshot the clean briefing into the save-under backing image. The
         * arm-cursor erase below restores from here. */
        if (!g_stBacking) g_stBacking = gfx_allocImage(SCREEN_WIDTH, SCREEN_HEIGHT);
        gfx_captureToImage(g_stBacking, *page1NumPtr, 0, 0, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    }
    if (b != -1) {
        if (b < 5 && enableHighlight != 0) {
            gfx_switchColor(page1NumPtr, 113, b * 21 + 34, 297, b * 21 + 42, COLOR_BRIEF_DESC_NORMAL, COLOR_BRIEF_DESC_HL);
        }
        showSprite(*page1NumPtr, armBlitX[spriteIdx], armBlitY[spriteIdx], armSrcX[spriteIdx], armSrcY[spriteIdx], armBlitW[spriteIdx], armBlitH[spriteIdx]);
    }
    gfx_commitPage();
    spriteBlitX = armBlitX[spriteIdx];
    spriteBlitY = armBlitY[spriteIdx];
    spriteBlitW = armBlitW[spriteIdx];
    spriteBlitH = armBlitH[spriteIdx];
    gfx_restoreFromImage(g_stBacking, *page1NumPtr, spriteBlitX, spriteBlitY, spriteBlitX, spriteBlitY, spriteBlitW, spriteBlitH);
    if (b < 5 && enableHighlight != 0) {
        gfx_switchColor(page1NumPtr, 113, b * 21 + 34, 297, b * 21 + 42, COLOR_BRIEF_DESC_HL, COLOR_BRIEF_DESC_NORMAL);
    }
}

int16 askRepeatMission() {
    char keycode;
    page1Desc.color = COLOR_BRIEF_DESC_HL;
    drawStringCentered(page1NumPtr, "Repeat last mission ? (y/n)", 113, 66, 185);
    enableHighlight = 0;
    timerCounter3 = 6;
    animateArm(armPosition, armPosition);
    clearBriefing();
    keycode = misc_getKey();
    if (keycode == 'Y' || keycode == 'y') {
        return 1;
    }
    return 0;
}

void checkDiskA() {
    while ((fileHandle = openFile("F15.spr", 0)) == NULL) {
        clearBriefing();
        drawStringCentered(page1NumPtr, "Please reinsert F15 Disk A", 113, 61, 185);
        page1NumPtr[6] = FONT_SMALL; // page1Desc.font?
        drawStringCentered(page1NumPtr, "<Press selector when ready>", 113, 73, 185);
        page1NumPtr[6] = FONT_NORMAL;
        enableHighlight = 0;
        timerCounter3 = 6;
        animateArm(armPosition, armPosition);
        waitJoyKey();
    }
    fileClose(fileHandle);
    clearBriefing();
}

void missionDecode() {
    page1Desc.color = COLOR_BRIEF_DESC_NORMAL;
    drawStringCentered(page1NumPtr, "decoding mission...", 113, 66, 185);
    enableHighlight = 0;
    timerCounter3 = 6;
    animateArm(armPosition, armPosition);
}

void printMission() {
    int armStep;
    clearBriefing();
    page1Desc.color = COLOR_BLUE;
    drawStringCentered(page1NumPtr, "TODAY'S MISSION", 113, 14, 185);
    drawLine(page1NumPtr, 160, 22, 249, 22, 1);
    drawStringAt(page1NumPtr, "Takeoff from:", 130, 32);
    page1Desc.color = COLOR_BRIEF_DESC_HL;
    buildTargetLabel(targets[0].baseIdx);
    drawStringCentered(page1NumPtr, todayMissStrBuf, 113, 42, 185);
    mystrcpy(todayMissStrBuf, "ONC ");
    mystrcat(todayMissStrBuf, getItemCoordStr(targets[0].baseIdx));
    page1Desc.font = FONT_SMALL;
    page1Desc.color = COLOR_CYAN;
    drawStringCentered(page1NumPtr, todayMissStrBuf, 113, 52, 185);
    page1Desc.font = FONT_NORMAL;
    page1Desc.color = COLOR_BLUE;
    drawStringAt(page1NumPtr, "Primary Target:", 130, 64);
    page1Desc.color = COLOR_BRIEF_DESC_HL;
    buildTargetLabel(targets[0].targetIdx);
    drawStringCentered(page1NumPtr, todayMissStrBuf, 113, 74, 185);
    page1Desc.font = FONT_SMALL;
    page1Desc.color = COLOR_CYAN;
    mystrcpy(todayMissStrBuf, "ONC ");
    mystrcat(todayMissStrBuf, targets[0].coord);
    drawStringCentered(page1NumPtr, todayMissStrBuf, 113, 84, 185);
    page1Desc.font = FONT_NORMAL;
    page1Desc.color = COLOR_BLUE;
    drawStringAt(page1NumPtr, "Secondary Target:", 130, 96);
    page1Desc.color = COLOR_BRIEF_DESC_HL;
    buildTargetLabel(targets[1].targetIdx);
    drawStringCentered(page1NumPtr, todayMissStrBuf, 113, 106, 185);
    page1Desc.font = FONT_SMALL;
    page1Desc.color = COLOR_CYAN;
    mystrcpy(todayMissStrBuf, "ONC ");
    mystrcat(todayMissStrBuf, targets[1].coord);
    drawStringCentered(page1NumPtr, todayMissStrBuf, 113, 116, 185);
    page1Desc.font = FONT_NORMAL;
    enableHighlight = 0;
    setTimerIrqHandler();
    timerCounter3 = 6;
    animateArm(-1, 6);
    for (armStep = 5; armStep >= 0; armStep--) {
        animateArm(armStep + 1, armStep);
    }
    timerCounter = 0;
    armStep++;
printMissionAgain:
    if (joyOrKey() == 0) {
        if (timerCounter >= PRINTMISS_TIMESTEP) {
            timerCounter = 0;
            if (armStep < 5) {
                animateArm(armStep, armStep + 1);
                if (++armStep != 3) {
                    animateArm(armStep, armStep + 1);
                }
                armStep++;
            }
        }
        goto printMissionAgain;
    }
    for (; armStep <= 5; armStep++) {
        animateArm(armStep, armStep + 1);
    }

    restoreTimerIrqHandler();
    enableHighlight = 1;
}

int16 pollMenuInput() {
    uint16 key;
    char repeatHold;
    int joy1;
    int joy0;
    joy0 = joy1 = 0;
    repeatHold = 0;
    if (joyRepeatFlag == 1) {
        timerCounter = 0;
        repeatHold = 1;
    }
    if (commData->setupUseJoy == 1) {
        joy0 = misc_readJoystick(0);
        joy1 = misc_readJoystick(1);
        pollJoystick();
    }
    while ((misc_checkKeyBuf() != 0 && joy0 == 0 && joy1 == 0 && joyAxes[0] >= JOY_DEADZONE_LO && joyAxes[0] <= JOY_DEADZONE_HI && joyAxes[1] >= JOY_DEADZONE_LO && joyAxes[1] <= JOY_DEADZONE_HI) || repeatHold == 1) {
        // XXX: case study for instruction skipping in mzdiff, change above while condition to true and uncomment, run mzdiff with refskip 1 tgtskip 2 to repro
        // if ((((((misc_checkKeyBuf() == 0) || (var_2 != 0)) || (var_4 != 0)) ||
        //     ((joyAxes[0] < 0x4e || (joyAxes[0] > 0xb2)))) ||
        //     ((joyAxes[1] < 0x4e || (joyAxes[1] > 0xb2)))) && (var_6 != 1)) break;
        if ((joyRepeatFlag == 1) && (15 < timerCounter)) { // 113f
            repeatHold = 0;
            joyRepeatFlag = 0;
        }
        if (commData->setupUseJoy == 1) {
            joy0 = misc_readJoystick(0);
            joy1 = misc_readJoystick(1);
            pollJoystick();
        }
        if (cbreakHit != 0) {
            cleanup();
            restoreCbreakHandler();
            exit(0);
        }
        // blink cursor on top of current pilot selection
        blinkPilot();
    }
    if (misc_checkKeyBuf() == 0) {
        key = misc_getKey();
    } else if (joy0 == 1) {
        key = KEYCODE_ENTER;
    } else if (joyAxes[1] < JOY_DEADZONE_LO) {
        key = KEYCODE_UPARROW;
        joyRepeatFlag = 1;
    } else if (joyAxes[1] > JOY_DEADZONE_HI) {
        key = KEYCODE_DNARROW;
        joyRepeatFlag = 1;
    } else if (joyAxes[0] < JOY_DEADZONE_LO) {
        key = KEYCODE_LEFTARROW;
        joyRepeatFlag = 1;
    } else if (joyAxes[0] > JOY_DEADZONE_HI) {
        key = KEYCODE_RIGHTARROW;
        joyRepeatFlag = 1;
    }
    if (((uint8 *)&key)[0]) {
        key = key & 0xff;
    }
    if (key == KEYCODE_ALTQ) {
        cleanup();
        restoreCbreakHandler();
        exit(0);
    }
    return key;
}

void clearBriefing(void) {
    // clear briefing board
    if (g_briefRecording) { /* wiping the board also empties the recorded scene */
        g_briefStrN = 0;
        g_briefLineN = 0;
    }
    clearRect(page1NumPtr, 113, 13, 297, 126);
}
