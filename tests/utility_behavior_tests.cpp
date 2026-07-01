#include "inttype.h"
#include "const.h"
#include "endata.h"
#include "endtypes.h"
#include "gfx.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <tuple>
#include <utility>

#include "posix_test_compat.h"

extern int randMul(uint16 arg);
extern int approxDistance(int dx, int dy);
extern int calcBearing(int dx, int dy);
extern int clampValue(int val, int lo, int hi);
extern uint32 scaleCoordByLevel(int level, uint32 coord);
extern int stringWidth(int16 *page, const char *str);
extern void drawStringCentered(int16 *page, const char *str, int startx, int y, int endx);
extern void my_ltoa(int32 value, char *buf);
extern void my_itoa(int value, char *buf);
extern void mystrcpy(char *dest, const char *source);
extern void mystrcat(char *dst, const char *src);
extern int mystrlen(const char *s);
extern void nearmemset(void *dst, char val, int count);
extern void strcpyFromDot(char *dst, const char *src);
extern char *formatFlightTime(int timeValue, char *buffer);
extern int mapToScreenX(unsigned char mapCoord);
extern int mapToScreenY(unsigned char mapCoord);
extern void plotMapPoint(int x, int y, int color, int unused);
extern void drawFlightLine(int p1, int p2, int p3, int p4);
extern void drawClippedLineEx(int x1, int y1, int x2, int y2, int cx1, int cy1, int cx2, int cy2, int flag);
extern void computeMissionResult(void);
extern int isPointInRect(const MenuItem *p);
extern void blinkWidget(MenuItem *item, int16 *gfxPage);
extern void processMenuItems(MenuItem *items, int unused, int itemCount, int cursorStartX, int cursorStartY, int16 *gfxPage);
extern int selectMenuItem(MenuItem *items, int unused, int itemCount, int16 *inputState, int16 *gfxPage);
extern void drawMenuItem(const MenuItem *items, unsigned int index, int16 *gfxPage);
extern void timerWait(unsigned int ticks);
extern void processDebriefInput(const int16 *cursorBounds, const MenuItem *menuItem, int16 *gfxPage);
extern int drawEventSprite(int recordIdx);
extern unsigned int drawFlightPath(int16 *gfxPage, unsigned int maxRecord);
extern void showEventPopup(void);
extern void animateFlightPath(int16 *gfxPage);
extern long calcMissionScore(int param);
extern void readFromWorldBuf(void *dest, int size, int count, SDL_IOStream *bufHandle);
extern void writeToWorldBuf(void *dest, int size, int count, SDL_IOStream *bufHandle);
extern void loadWorldData(void *destOffset, int size);
extern void farStrcpy(char *dst, const char far *src);
extern void drawStringAtPos(int16 *s, const char far *str, int x, int y);
extern void drawFarString(int16 *s, const char far *str);
extern void loadWorldStrings(void);
extern void seedRandom(void);
extern int getTimeOfDay(void);
extern SDL_IOStream *openFileWrapper(const char *filename, int mode);
extern void closeFileWrapper(SDL_IOStream *handle);
extern void openShowPic(const char *filename, int page);
extern void loadPic(const char *filename, int segment);
extern void outportByte(int port, int value);
extern void restoreVideoMode(void);
extern void restoreInterrupts(void);
extern void setupWorldBufPtr(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum UtilityOriginalConstant : int {
    kBearingEast = 0x4000,
    kBearingSouth = 0x8000,
    kBearingWest = 0xC000,
    kDistanceMax = 0x7FFF,
    kClampWrapFloor = -0x4000,
    kRandomScaleShift = 15,
    kRandomSeedProbe = 123,
    kMemsetByte = 0x5A,
    kMemsetCount = 3,
    kOneGameHourTicks = 1800,
    kOneGameMinuteTicks = 30,
    kBearingCurveBase = 0x2800,
    kBearingCurveCenter = 0x1333,
    kBearingCurveScale = 0x0B00,
    kMaxScoredWeaponCount = 15,
    kMissionScoreAirFactor = 25,
    kMissionScoreSamFactor = 50,
    kMissionScoreGroundFactor = 20,
    kMissionScorePrimaryFactor = 200,
    kMissionScoreSecondaryFactor = 100,
    kMissionScoreEjectNumerator = 3,
    kMissionScoreEjectDenominator = 4,
    kFlightRecordGroundClearX = 0x20,
    kFlightRecordGroundClearY = 0x30,
    kFlightRecordGroundBlockedX = 0x40,
    kFlightRecordGroundBlockedY = 0x40,
    kDebriefFullClipMaxX = 319,
    kDebriefFullClipMaxY = 199,
    kDebriefAirSlotFlag = 8,
    kDebriefAirFriendlySpriteX = 286,
    kDebriefAirHostileSpriteX = 301,
    kDebriefSpriteCenterBias = 2,
    kDebriefPathMaxRecord = 3,
    kDebriefWorldGridShift = 11,
    kDebriefWorldGridWidth = 16,
    kDebriefMissionResultMask = 3,
    kDebriefMissionGridX = 5,
    kDebriefMissionGridY = 6,
    kDebriefMissionGridFlags = 0x86,
    kWorldDataNotReady = 0,
    kWorldLoadBytes = 4,
    kDebriefCursorInsideX = 42,
    kDebriefCursorInsideY = 27,
    kDebriefCursorOutsideX = 75,
    kDebriefHitX1 = 30,
    kDebriefHitX2 = 50,
    kDebriefHitY1 = 20,
    kDebriefHitY2 = 40,
    kDebriefBlinkColorPair = 0xAB,
    kDebriefBlinkColorFrom = 0x0A,
    kDebriefBlinkColorTo = 0x0B,
    kDebriefBlinkX1 = 11,
    kDebriefBlinkY1 = 12,
    kDebriefBlinkX2 = 21,
    kDebriefBlinkY2 = 22,
    kDebriefTimerWaitTicks = 4,
    kDebriefInputStepX = 7,
    kDebriefInputStepY = 5,
    kDebriefInputMinX = 10,
    kDebriefInputMaxX = 40,
    kDebriefInputMinY = 20,
    kDebriefInputMaxY = 45,
    kDebriefInputStartX = 20,
    kDebriefInputStartY = 30,
    kDebriefInputLeftResultX = 13,
    kDebriefInputRightResultX = 27,
    kDebriefInputUpResultY = 25,
    kDebriefInputDownResultY = 35,
    kDebriefInputEnterKey = 0x0d,
    kDebriefInputEscapeKey = 0x1b,
    kDebriefInputAltQKey = 0x1000,
    kDebriefKeyAvailable = 1,
    kDebriefNoKeyAvailable = 0,
    kDebriefMenuItemCount = 3,
    kDebriefMenuKeepState = 3,
    kDebriefMenuPendingState = 2,
    kDebriefMenuSelectedIndex = 1,
    kDebriefMenuMovedIndex = 1,
    kDebriefMenuCursorX = 77,
    kDebriefMenuCursorY = 88,
    kDebriefMenuHitX1 = 70,
    kDebriefMenuHitX2 = 90,
    kDebriefMenuHitY1 = 80,
    kDebriefMenuHitY2 = 100,
    kDebriefMenuSecondHitX1 = 100,
    kDebriefMenuSecondHitX2 = 130,
    kDebriefMenuSecondHitY1 = 80,
    kDebriefMenuSecondHitY2 = 100,
    kDebriefMenuMoveStepX = 30,
    kDebriefMenuMoveStartX = 75,
    kDebriefMenuMoveStartY = 90,
    kDebriefMenuColorPair = 0x69,
    kDebriefMenuColorFrom = 6,
    kDebriefMenuColorTo = 9,
    kDebriefMenuColorX1 = 31,
    kDebriefMenuColorY1 = 32,
    kDebriefMenuColorX2 = 41,
    kDebriefMenuColorY2 = 42,
    kDebriefMenuEnterSwitchCount = 3,
    kDebriefMenuMoveSwitchCount = 6,
    kDebriefMenuEnterLastFrom = 0x0d,
    kDebriefMenuEnterTo = 9,
    kDebriefMenuColorTableOne = 1,
    kDebriefMenuColorTableOneFrom = 8,
    kDebriefMenuColorTableOneTo = 7,
    kDebriefMenuDisabledColorAnim = 1,
    kDebriefMenuColorTableZeroMoveSwitchCount = 7,
    kDebriefAnimatePopupX = 12,
    kDebriefAnimatePopupY = 34,
    kDebriefAnimateStartX = 10,
    kDebriefAnimateStartY = 20,
    kDebriefAnimateTimestampX = 30,
    kDebriefAnimateTimestampY = 40,
    kDebriefAnimateSecondTimestampX = 45,
    kDebriefAnimateSecondTimestampY = 60,
    kDebriefAnimateYieldCount = 6,
    kDebriefAnimateClearCount = 3,
    kDebriefAnimateLineCount = 2,
    kDebriefAnimateTwoTimestampLineCount = 3,
    kPopupThresholdX = 115,
    kPopupThresholdY = 89,
    kPopupOffsetLow = 10,
    kPopupOffsetHighX = -58,
    kPopupOffsetHighY = -40,
    kPopupAirFriendlySprite = 15,
    kPopupAirHostileSprite = 0,
    kPopupAirKill2Sprite = 2,
    kPopupSamSprite = 1,
    kPopupGroundSprite = 2,
    kPopupBombSprite = 3,
    kPopupWaypointSprite = 10,
    kPopupEjectStartSprite = 8,
    kPopupEjectSafeSprite = 7,
    kPopupEjectCrashSprite = 14,
    kPopupEjectMissionOkSprite = 11,
    kPopupEjectMissionFailedSprite = 13,
    kWorldStringsOffset = 210,
    kWorldDataReady = 1,
    kDrawStringX = 12,
    kDrawStringY = 34,
    kCenteredStartX = 20,
    kCenteredEndX = 80,
    kCenteredY = 44,
    kCenteredFont = 3,
    kCenteredExpectedX = 51,
    kFilePicMode = 7,
    kFilePicPage = 2,
    kFilePicSegment = 0x3456,
    kFakeFilePicHandle = 0x7A00,
    kOutportFixturePort = 0x3c8,
    kOutportFixtureValue = 0x3f,
    kDebriefMissionSummaryType = 7,
    kDebriefEventDetailType = 1,
    kDebriefSpriteTimerThreshold = 19,
    kDebriefSpriteAirUnit = 4,
    kDebriefSpriteX = 44,
    kDebriefSpriteY = 55,
    kDebriefScoreTotal = 500,
    kDebriefMissionScore = 90,
    kDebriefDrawMenuFlightTime = 60,
    kDebriefWorldRef = 2,
    kDebriefWorldObject = 3,
    kDebriefSamWeapon = 2,
    kDebriefPlaneUnit = 3,
};

int g_fontCallCount = 0;
int g_lastGfxColor = -1;
int g_lastBlitOffset = -1;
int g_lastOverlayMaxX = -1;
int g_lastOverlayMaxY = -1;
int g_lineWrapperCalls = 0;
int g_blitSpriteCalls = 0;
struct SpriteParams *g_lastSprite = nullptr;
struct SpriteParams *g_spriteLog[16] = {};
struct LineSnapshot {
    int x1;
    int y1;
    int x2;
    int y2;
    int clipX;
    int clipY;
};
LineSnapshot g_lineSnapshots[16] = {};
struct CopyRectSnapshot {
    int srcPage;
    int srcX;
    int srcY;
    int dstPage;
    int dstX;
    int dstY;
    int width;
    int height;
};
CopyRectSnapshot g_copySnapshots[16] = {};
int g_copyRectCalls = 0;
int g_clearRectCalls = 0;
int g_drawWrappedTextCalls = 0;
int g_drawStringCalls = 0;
int16 *g_lastDrawStringPage = nullptr;
char g_lastDrawStringText[200] = {};
int g_switchColorCalls = 0;
int g_lastSwitchFromColor = -1;
int g_lastSwitchToColor = -1;
int g_lastSwitchX1 = -1;
int g_lastSwitchY1 = -1;
int g_lastSwitchX2 = -1;
int g_lastSwitchY2 = -1;
int g_setTimerCalls = 0;
int g_restoreTimerCalls = 0;
int g_timerYieldCalls = 0;
int g_checkKeyCalls = 0;
bool g_tickTimer2OnCheck = false;
bool g_tickTimerOnCheck = false;
int g_getKeyCalls = 0;
int g_readJoystickCalls = 0;
int g_pollJoystickCalls = 0;
int g_joystickScript[8] = {};
int g_joystickScriptLen = 0;
int g_joystickScriptPos = 0;
int g_cleanupCalls = 0;
int g_restoreCbreakCalls = 0;
int g_keyScript[32] = {};
int g_keyScriptLen = 0;
int g_keyScriptPos = 0;
int g_getKeyScript[8] = {};
int g_getKeyScriptLen = 0;
int g_getKeyScriptPos = 0;
int g_getKeyResult = 0;
int g_openFileCalls = 0;
int g_fileCloseCalls = 0;
int g_showPicCalls = 0;
int g_decodePicCalls = 0;
int g_lastOpenMode = -1;
int g_lastShowPicPage = -1;
int g_lastDecodeSegment = -1;
const char *g_lastOpenFileName = nullptr;
SDL_IOStream *g_lastClosedFile = nullptr;
SDL_IOStream *g_lastShowPicHandle = nullptr;
SDL_IOStream *g_lastDecodePicHandle = nullptr;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

int expectedApproxDistance(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    const long dist = dx > dy ? static_cast<long>(dy >> 1) + dx
                              : static_cast<long>(dx >> 1) + dy;
    return dist > kDistanceMax ? kDistanceMax : static_cast<int>(dist);
}

int expectedBearing(int dx, int dy) {
    if (dx == 0) return dy > 0 ? 0 : kBearingSouth;
    if (dy == 0) return dx > 0 ? kBearingEast : kBearingWest;

    const int absX = std::abs(dx);
    const int absY = std::abs(dy);
    const bool swapped = absX > absY;
    const int32 ratioNumerator = static_cast<int32>(swapped ? absY : absX) << 14;
    const int divisor = swapped ? absX : absY;
    const int quotient = static_cast<int>(ratioNumerator / divisor);
    const int angle = static_cast<int>(
        ((kBearingCurveBase - (((long)std::abs(kBearingCurveCenter - quotient) * kBearingCurveScale) >> 14)) *
         static_cast<long>(quotient)) >> 14);

    if (dx > 0) {
        return dy > 0 ? (swapped ? kBearingEast - angle : angle)
                      : (swapped ? angle + kBearingEast : kBearingSouth - angle);
    }
    return dy > 0 ? (swapped ? angle + kBearingWest : -angle)
                  : (swapped ? kBearingWest - angle : angle + kBearingSouth);
}

int expectedClampValue(int val, int lo, int hi) {
    if (val > hi) return hi;
    if (val >= lo) return val;
    return val > kClampWrapFloor ? lo : hi;
}

void resetDebriefState(struct GameComm &comm, struct Game &game) {
    std::memset(&comm, 0, sizeof(comm));
    std::memset(&game, 0, sizeof(game));
    std::memset(flightDataBuf, 0, sizeof(flightDataBuf));
    std::memset(unitTypeTable, 0, 100);
    std::memset(slotInfoTable, 0, 1194);
    std::memset(gridFlags, 0, 0x100);
    std::memset(&weaponDataBlock, 0, sizeof(weaponDataBlock));
    commData = &comm;
    gameData = &game;
    g_lastGfxColor = -1;
    g_lastBlitOffset = -1;
    g_lastOverlayMaxX = -1;
    g_lastOverlayMaxY = -1;
    g_lineWrapperCalls = 0;
    g_blitSpriteCalls = 0;
    g_lastSprite = nullptr;
    std::memset(g_spriteLog, 0, sizeof(g_spriteLog));
    g_copyRectCalls = 0;
    g_clearRectCalls = 0;
    g_drawWrappedTextCalls = 0;
    g_drawStringCalls = 0;
    g_lastDrawStringPage = nullptr;
    g_switchColorCalls = 0;
    g_lastSwitchFromColor = -1;
    g_lastSwitchToColor = -1;
    g_lastSwitchX1 = -1;
    g_lastSwitchY1 = -1;
    g_lastSwitchX2 = -1;
    g_lastSwitchY2 = -1;
    g_setTimerCalls = 0;
    g_restoreTimerCalls = 0;
    g_timerYieldCalls = 0;
    g_checkKeyCalls = 0;
    g_tickTimer2OnCheck = false;
    g_tickTimerOnCheck = false;
    g_getKeyCalls = 0;
    g_readJoystickCalls = 0;
    g_pollJoystickCalls = 0;
    std::memset(g_joystickScript, 0, sizeof(g_joystickScript));
    g_joystickScriptLen = 0;
    g_joystickScriptPos = 0;
    g_cleanupCalls = 0;
    g_restoreCbreakCalls = 0;
    std::memset(g_keyScript, 0, sizeof(g_keyScript));
    g_keyScriptLen = 0;
    g_keyScriptPos = 0;
    std::memset(g_getKeyScript, 0, sizeof(g_getKeyScript));
    g_getKeyScriptLen = 0;
    g_getKeyScriptPos = 0;
    g_getKeyResult = 0;
    g_openFileCalls = 0;
    g_fileCloseCalls = 0;
    g_showPicCalls = 0;
    g_decodePicCalls = 0;
    g_lastOpenMode = -1;
    g_lastShowPicPage = -1;
    g_lastDecodeSegment = -1;
    g_lastOpenFileName = nullptr;
    g_lastClosedFile = nullptr;
    g_lastShowPicHandle = nullptr;
    g_lastDecodePicHandle = nullptr;
    std::memset(g_lastDrawStringText, 0, sizeof(g_lastDrawStringText));
    std::memset(g_lineSnapshots, 0, sizeof(g_lineSnapshots));
    std::memset(g_copySnapshots, 0, sizeof(g_copySnapshots));
    popupVisible = 0;
    popupX = 0;
    popupY = 0;
    ejectedFlag = 0;
    missionResult = 0;
    curRecordIdx = 0;
    lastDrawX = 0;
    lastDrawY = 0;
    prevDrawX = 0;
    prevDrawY = 0;
    colorAnimEnabled = 0;
    colorAnimIdx = 0;
    joyRepeatFlag = 0;
    quitFlag = 0;
    inputChanged = 0;
    enterPressed = 0;
    animDone = 0;
    joyAxisX = joyAxisY = JOY_CENTER;
}

void setKeyScript(const int *values, int count, int keyResult) {
    require(count <= static_cast<int>(sizeof(g_keyScript) / sizeof(g_keyScript[0])),
            "test key script capacity");
    std::memset(g_keyScript, 0, sizeof(g_keyScript));
    for (int idx = 0; idx < count; ++idx) g_keyScript[idx] = values[idx];
    g_keyScriptLen = count;
    g_keyScriptPos = 0;
    std::memset(g_getKeyScript, 0, sizeof(g_getKeyScript));
    g_getKeyScriptLen = 0;
    g_getKeyScriptPos = 0;
    g_getKeyResult = keyResult;
}

void setKeyAndGetScript(const int *keyBufValues, int keyBufCount,
                        const int *getKeyValues, int getKeyCount) {
    setKeyScript(keyBufValues, keyBufCount, 0);
    require(getKeyCount <= static_cast<int>(sizeof(g_getKeyScript) / sizeof(g_getKeyScript[0])),
            "test get-key script capacity");
    for (int idx = 0; idx < getKeyCount; ++idx) g_getKeyScript[idx] = getKeyValues[idx];
    g_getKeyScriptLen = getKeyCount;
    g_getKeyScriptPos = 0;
}

void setJoystickScript(const int *values, int count) {
    require(count <= static_cast<int>(sizeof(g_joystickScript) / sizeof(g_joystickScript[0])),
            "test joystick script capacity");
    std::memset(g_joystickScript, 0, sizeof(g_joystickScript));
    for (int idx = 0; idx < count; ++idx) g_joystickScript[idx] = values[idx];
    g_joystickScriptLen = count;
    g_joystickScriptPos = 0;
}

int gridIndexForRecord(unsigned char mapX, unsigned char mapY) {
    return (((mapY & 0xff) >> 4) << 4) + (mapX >> 4);
}

} // namespace

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;
uint16 worldObjectCount = 0;
uint8 timerCounter = 0;
uint8 timerCounter2 = 0;
uint8 timerCounter3 = 0;
uint8 joyRepeatFlag = 0;
struct WorldObject worldObjects[0x4B] = {};
int16 lineX1 = 0;
int16 lineY1 = 0;
int16 lineX2 = 0;
int16 lineY2 = 0;
int16 clipMaxX = kDebriefFullClipMaxX;
int16 clipMaxY = kDebriefFullClipMaxY;

int gfx_setFont(uint16 ch, uint16 font) {
    ++g_fontCallCount;
    return static_cast<int>((ch & 0x0f) + font);
}

int FAR CDECL gfx_calcRowAddr(int col, int row) {
    return row * 320 + col;
}

void FAR CDECL gfx_setBlitOffset(int offset) {
    g_lastBlitOffset = offset;
}

void FAR CDECL gfx_setOvlVal1(int value) {
    g_lastOverlayMaxY = value;
}

void FAR CDECL gfx_setOvlVal2(int value) {
    g_lastOverlayMaxX = value;
}

void FAR CDECL gfx_setColor(int color) {
    g_lastGfxColor = color;
}

void FAR CDECL gfx_nop23(void) {}

void drawLineWrapper(void) {
    require(g_lineWrapperCalls < static_cast<int>(sizeof(g_lineSnapshots) / sizeof(g_lineSnapshots[0])),
            "test line snapshot recorder capacity");
    g_lineSnapshots[g_lineWrapperCalls++] =
        {lineX1, lineY1, lineX2, lineY2, clipMaxX, clipMaxY};
}

int FAR CDECL gfx_blitSprite(struct SpriteParams *sprite) {
    require(g_blitSpriteCalls < static_cast<int>(sizeof(g_spriteLog) / sizeof(g_spriteLog[0])),
            "test sprite recorder capacity");
    g_lastSprite = sprite;
    g_spriteLog[g_blitSpriteCalls] = sprite;
    ++g_blitSpriteCalls;
    return 0x55 + g_blitSpriteCalls;
}

void FAR CDECL gfx_copyRect(int srcPage, uint16 srcX, uint16 srcY, int dstPage,
                            uint16 dstX, uint16 dstY, int width, int height) {
    require(g_copyRectCalls < static_cast<int>(sizeof(g_copySnapshots) / sizeof(g_copySnapshots[0])),
            "test copy-rect recorder capacity");
    g_copySnapshots[g_copyRectCalls++] =
        {srcPage, static_cast<int>(srcX), static_cast<int>(srcY), dstPage,
         static_cast<int>(dstX), static_cast<int>(dstY), width, height};
}

void clearRect(int16 *, int, int, int, int) {
    ++g_clearRectCalls;
}

void drawWrappedText(int16 *, char *, unsigned int, int, int, int) {
    ++g_drawWrappedTextCalls;
}

void FAR CDECL gfx_drawString(int16 *pageNum, const char *string) {
    ++g_drawStringCalls;
    g_lastDrawStringPage = pageNum;
    std::strncpy(g_lastDrawStringText, string ? string : "", sizeof(g_lastDrawStringText) - 1);
}

void FAR CDECL gfx_switchColor(int16 *, int x1, int y1, int x2, int y2,
                               int oldColor, int newColor) {
    ++g_switchColorCalls;
    g_lastSwitchX1 = x1;
    g_lastSwitchY1 = y1;
    g_lastSwitchX2 = x2;
    g_lastSwitchY2 = y2;
    g_lastSwitchFromColor = oldColor;
    g_lastSwitchToColor = newColor;
}

void setTimerIrqHandler(void) {
    ++g_setTimerCalls;
}

void restoreTimerIrqHandler(void) {
    ++g_restoreTimerCalls;
}

void timerYield(void) {
    ++g_timerYieldCalls;
    ++timerCounter;
}

int FAR CDECL misc_checkKeyBuf(void) {
    ++g_checkKeyCalls;
    if (g_tickTimerOnCheck) ++timerCounter;
    if (g_tickTimer2OnCheck) ++timerCounter2;
    if (g_keyScriptPos < g_keyScriptLen) return g_keyScript[g_keyScriptPos++];
    return 0;
}

int FAR CDECL misc_getKey(void) {
    ++g_getKeyCalls;
    if (g_getKeyScriptPos < g_getKeyScriptLen) return g_getKeyScript[g_getKeyScriptPos++];
    return g_getKeyResult;
}

int FAR CDECL misc_readJoystick(int16) {
    ++g_readJoystickCalls;
    if (g_joystickScriptPos < g_joystickScriptLen) return g_joystickScript[g_joystickScriptPos++];
    return 0;
}

void cleanup(void) { ++g_cleanupCalls; }
void FAR CDECL gfx_commitPage(void) {}

SDL_IOStream *openFile(const char *name, int mode) {
    ++g_openFileCalls;
    g_lastOpenFileName = name;
    g_lastOpenMode = mode;
    return reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFilePicHandle));
}

void fileClose(SDL_IOStream *handle) {
    ++g_fileCloseCalls;
    g_lastClosedFile = handle;
}

void showPicFile(SDL_IOStream *handle, int page) {
    ++g_showPicCalls;
    g_lastShowPicHandle = handle;
    g_lastShowPicPage = page;
}

void decodePic(SDL_IOStream *handle, int segment) {
    ++g_decodePicCalls;
    g_lastDecodePicHandle = handle;
    g_lastDecodeSegment = segment;
}

int main() {
    for (auto [dx, dy] : {std::pair{0, 0}, std::pair{10, 4}, std::pair{-10, 4},
                          std::pair{4, -10}, std::pair{40000, 40000}}) {
        require(approxDistance(dx, dy) == expectedApproxDistance(dx, dy),
                "start approxDistance matches original max-plus-half-min formula");
    }

    for (auto [dx, dy] : {std::pair{0, 1}, std::pair{0, -1}, std::pair{1, 0},
                          std::pair{-1, 0}, std::pair{100, 100}, std::pair{100, -50},
                          std::pair{-50, 100}, std::pair{-100, -20}, std::pair{7, -123}}) {
        require(static_cast<uint16>(calcBearing(dx, dy)) == static_cast<uint16>(expectedBearing(dx, dy)),
                "start calcBearing matches original quadrant approximation");
    }

    const int clampSamples[] = {-20000, kClampWrapFloor, kClampWrapFloor + 1, -10, 0, 10, 20000};
    for (int value : clampSamples) {
        require(clampValue(value, -10, 10) == expectedClampValue(value, -10, 10),
                "start clampValue preserves original wrap-floor behavior");
    }

    for (auto [level, coord, expected] : {std::tuple{4, 0x10000u, 0x400u},
                                          std::tuple{3, 0x10000u, 0x1000u},
                                          std::tuple{2, 0x10000u, 0x4000u},
                                          std::tuple{1, 0x10000u, 0x10000u},
                                          std::tuple{0, 0x10000u, 0x20000u},
                                          std::tuple{-1, 0x1234u, 0x2468u}}) {
        require(scaleCoordByLevel(level, coord) == expected,
                "scaleCoordByLevel matches original terrain LOD shifts");
    }

    for (auto [randValue, maxVal] : {std::pair{0, 100}, std::pair{1, 100},
                                     std::pair{16384, 100}, std::pair{32767, 2000}}) {
        std::srand(1);
        /* randMul masks rand() internally; choose an equivalent libc seed path by
         * checking only formula samples through repeated seeding where possible. */
        const int expected = static_cast<int>((static_cast<long>(randValue & 0x7fff) * maxVal) >> kRandomScaleShift);
        const int oldRand = std::rand();
        (void)oldRand;
        /* randMul has no injection point, so validate the same scaling contract by
         * calling it for the host rand stream and recomputing that stream value. */
        std::srand(1234);
        const int hostRand = std::rand() & 0x7fff;
        std::srand(1234);
        require(randMul(maxVal) == static_cast<int>((static_cast<long>(hostRand) * maxVal) >> kRandomScaleShift),
                "randMul scales the DOS 15-bit rand output");
        require(expected >= 0 && expected <= maxVal, "randMul sample formula remains in range");
    }
    std::srand(kRandomSeedProbe);
    seedRandom();
    const int seededByHelper = std::rand();
    std::srand(getTimeOfDay());
    require(seededByHelper == std::rand(),
            "seedRandom seeds libc rand from the original getTimeOfDay source");

    char buf[32];
    my_itoa(0, buf);
    require(std::strcmp(buf, "0") == 0, "my_itoa formats zero");
    my_itoa(123, buf);
    require(std::strcmp(buf, "123") == 0, "my_itoa formats small positive values");
    my_itoa(1234, buf);
    require(std::strcmp(buf, "1,234") == 0, "my_itoa inserts thousands comma");
    my_itoa(-1234, buf);
    require(std::strcmp(buf, "-1,234") == 0, "my_itoa preserves negative sign");

    my_ltoa(99999, buf);
    require(std::strcmp(buf, "99,999") == 0, "my_ltoa formats five digits with comma");
    my_ltoa(-42, buf);
    require(std::strcmp(buf, "-42") == 0, "my_ltoa formats negative values");

    int16 page[8] = {};
    page[6] = 3;
    g_fontCallCount = 0;
    require(stringWidth(page, "AZ") == ((static_cast<int>('A') & 0x0f) + 3) + ((static_cast<int>('Z') & 0x0f) + 3),
            "stringWidth sums gfx_setFont character widths");
    require(g_fontCallCount == 2, "stringWidth calls gfx_setFont once per character");
    page[6] = kCenteredFont;
    g_fontCallCount = 0;
    g_drawStringCalls = 0;
    drawStringCentered(page, "AZ", kCenteredStartX, kCenteredY, kCenteredEndX);
    require(g_fontCallCount == 2 && g_drawStringCalls == 1 &&
                g_lastDrawStringPage == page &&
                std::strcmp(g_lastDrawStringText, "AZ") == 0 &&
                page[4] == kCenteredExpectedX && page[5] == kCenteredY,
            "drawStringCentered computes the original centered x coordinate and draws at that position");

    char text[32] = "F15";
    mystrcat(text, " SE2");
    require(std::strcmp(text, "F15 SE2") == 0, "mystrcat preserves original strcat-style append");
    require(mystrlen(text) == 7, "mystrlen preserves original strlen-style byte count");

    char copied[16] = {};
    mystrcpy(copied, "VIPER");
    require(std::strcmp(copied, "VIPER") == 0, "mystrcpy copies through the terminating nul");

    unsigned char memory[] = {1, 2, 3, 4, 5};
    /* nearmemset was the near-memory memset helper in the DOS code; only the
     * requested byte span changes, and following bytes must remain untouched. */
    nearmemset(&memory[1], static_cast<char>(kMemsetByte), kMemsetCount);
    require(memory[0] == 1 && memory[1] == kMemsetByte && memory[2] == kMemsetByte &&
                memory[3] == kMemsetByte && memory[4] == 5,
            "nearmemset writes exactly the original requested byte count");

    char filenameWithDot[] = "IRAN.XXX";
    strcpyFromDot(filenameWithDot, ".3D3");
    require(std::strcmp(filenameWithDot, "IRAN.3D3") == 0,
            "strcpyFromDot replaces from the first dot");

    char filenameWithoutDot[] = "DESERT";
    strcpyFromDot(filenameWithoutDot, ".3dG");
    require(std::strcmp(filenameWithoutDot, "DESERT.3dG") == 0,
            "strcpyFromDot appends when no dot is present");

    char filenameStartingDot[] = ".OLD";
    strcpyFromDot(filenameStartingDot, ".3dT");
    require(std::strcmp(filenameStartingDot, ".3dT") == 0,
            "strcpyFromDot handles filenames that start at the extension");

    char timeBuf[16];
    std::memset(&targetBlock, 0, sizeof(targetBlock));
    require(formatFlightTime(0, timeBuf) == timeBuf, "formatFlightTime returns its caller buffer");
    require(std::strcmp(timeBuf, "20:00:00") == 0,
            "formatFlightTime infers original night clock when low target misc bits are zero");

    std::memset(&targetBlock, 0, sizeof(targetBlock));
    targetBlock.target1Type[0] = 1;
    formatFlightTime(kOneGameHourTicks + kOneGameMinuteTicks, timeBuf);
    require(std::strcmp(timeBuf, "11:01:00") == 0,
            "formatFlightTime forces day clock for target type 1");

    std::memset(&targetBlock, 0, sizeof(targetBlock));
    targetBlock.target2Type[0] = 4;
    formatFlightTime(2, timeBuf);
    require(std::strcmp(timeBuf, "20:00:04") == 0,
            "formatFlightTime forces night clock for target type 4");

    std::memset(&targetBlock, 0, sizeof(targetBlock));
    targetBlock.target1MiscBits[0] = 3;
    targetBlock.target2MiscBits[0] = 2;
    formatFlightTime(0, timeBuf);
    require(std::strcmp(timeBuf, "10:42:40") == 0,
            "formatFlightTime adds original low-nibble target time offset");

    for (unsigned char coord : {static_cast<unsigned char>(0), static_cast<unsigned char>(1),
                                static_cast<unsigned char>(127), static_cast<unsigned char>(255)}) {
        require(mapToScreenX(coord) == ((static_cast<unsigned int>(coord) << 7) / MAP_SCALE_X),
                "mapToScreenX preserves original integer scaling");
        require(mapToScreenY(coord) == ((static_cast<unsigned int>(coord) << 7) / MAP_SCALE_Y),
                "mapToScreenY preserves original integer scaling");
    }

    struct GameComm comm;
    struct Game game;

    resetDebriefState(comm, game);
    const int insideMapX = 40;
    const int insideMapY = 50;
    plotMapPoint(insideMapX, insideMapY, 6, 0);
    require(g_lineWrapperCalls == 1,
            "plotMapPoint draws one clipped pixel for visible nonnegative colors");
    require(g_lineSnapshots[0].x1 == mapToScreenX(insideMapX) &&
                g_lineSnapshots[0].y1 == mapToScreenY(insideMapY) &&
                g_lineSnapshots[0].x2 == mapToScreenX(insideMapX) &&
                g_lineSnapshots[0].y2 == mapToScreenY(insideMapY),
            "plotMapPoint forwards original scaled map coordinates to drawMapPixel");
    plotMapPoint(insideMapX, insideMapY, -1, 0);
    require(g_lineWrapperCalls == 1,
            "plotMapPoint suppresses the original -1 sentinel color");
    plotMapPoint(0, 0, 6, 0);
    require(g_lineWrapperCalls == 1,
            "plotMapPoint clips points outside the original debrief map rectangle");

    resetDebriefState(comm, game);
    drawClippedLineEx(11, 12, 21, 22, mapViewX1, mapViewX2, mapViewY1, mapViewY2, 1);
    require(g_lineWrapperCalls == 1 && g_lineSnapshots[0].x1 == 11 &&
                g_lineSnapshots[0].y1 == 12 && g_lineSnapshots[0].x2 == 21 &&
                g_lineSnapshots[0].y2 == 22,
            "drawClippedLineEx passes original line endpoints to drawLineWrapper");
    require(g_lineSnapshots[0].clipX == (mapViewX2 - mapViewX1 - 1) &&
                g_lineSnapshots[0].clipY == (mapViewY2 - mapViewY1 - 1),
            "drawClippedLineEx preserves the original unusual clip width/height formula");
    require(g_lastBlitOffset == 0 && clipMaxX == kDebriefFullClipMaxX &&
                clipMaxY == kDebriefFullClipMaxY && g_lastOverlayMaxX == kDebriefFullClipMaxX &&
                g_lastOverlayMaxY == kDebriefFullClipMaxY,
            "drawClippedLineEx restores full-page clip globals and blit offset");

    resetDebriefState(comm, game);
    drawFlightLine(80, 90, 40, 50);
    require(g_lineWrapperCalls == 1 &&
                g_lineSnapshots[0].x1 == mapToScreenX(80) &&
                g_lineSnapshots[0].y1 == mapToScreenY(90) &&
                g_lineSnapshots[0].x2 == mapToScreenX(40) &&
                g_lineSnapshots[0].y2 == mapToScreenY(50),
            "drawFlightLine scales both map endpoints before original clipping");

    resetDebriefState(comm, game);
    flightRecords[0].mapX = 40;
    flightRecords[0].mapY = 50;
    flightRecords[0].status = EVENT_AIR_KILL;
    flightRecords[0].unitId = 3;
    curRecordIdx = 0;
    slotInfoTable[(flightRecords[0].unitId & UNIT_ID_MASK) * 16] = kDebriefAirSlotFlag;
    const int airReturn = drawEventSprite(0);
    require(airReturn == 0x56 && g_blitSpriteCalls == 1 && g_lastSprite == spriteAir,
            "drawEventSprite blits original air-event sprite");
    require(spriteAir->dstX == mapToScreenX(40) + mapViewX1 - kDebriefSpriteCenterBias &&
                spriteAir->dstY == mapToScreenY(50) + mapViewY1 - kDebriefSpriteCenterBias &&
                spriteAir->srcX == kDebriefAirFriendlySpriteX,
            "drawEventSprite places friendly air sprite with original bias and source column");
    slotInfoTable[(flightRecords[0].unitId & UNIT_ID_MASK) * 16] = 0;
    drawEventSprite(0);
    require(spriteAir->srcX == kDebriefAirHostileSpriteX,
            "drawEventSprite switches air source column when the original slot flag is clear");
    flightRecords[1].mapX = 60;
    flightRecords[1].mapY = 70;
    flightRecords[1].status = EVENT_WAYPOINT;
    drawEventSprite(1);
    require(g_lastSprite == spriteWaypoint &&
                spriteWaypoint->dstX == mapToScreenX(60) + mapViewX1 &&
                spriteWaypoint->dstY == mapToScreenY(70) + mapViewY1,
            "drawEventSprite draws waypoint events without the 5x5 sprite center bias");
    flightRecords[2].status = EVENT_TIMESTAMP;
    require(drawEventSprite(2) == 0,
            "drawEventSprite ignores event types without an original sprite");

    resetDebriefState(comm, game);
    flightRecords[0].mapX = 40;
    flightRecords[0].mapY = 50;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[1].mapX = 50;
    flightRecords[1].mapY = 55;
    flightRecords[1].status = EVENT_AIR_KILL;
    flightRecords[1].unitId = 1;
    flightRecords[2].mapX = 60;
    flightRecords[2].mapY = 65;
    flightRecords[2].status = EVENT_TIMESTAMP;
    flightRecords[3].mapX = 70;
    flightRecords[3].mapY = 75;
    flightRecords[3].status = EVENT_GROUND_KILL;
    flightRecords[4].status = 0;
    curRecordIdx = 1;
    const unsigned int lastRecord = drawFlightPath(nullptr, kDebriefPathMaxRecord);
    require(lastRecord == kDebriefPathMaxRecord,
            "drawFlightPath returns the last original record index it processed");
    require(g_lineWrapperCalls == 4,
            "drawFlightPath draws start point plus one line per following record including timestamps");
    require(g_blitSpriteCalls == 3,
            "drawFlightPath blits only non-timestamp event sprites on the second pass");
    require(g_lastSprite == spriteGround,
            "drawFlightPath leaves the final event sprite as the last drawn ground event");

    resetDebriefState(comm, game);
    comm.worldX = kDebriefMissionGridX << kDebriefWorldGridShift;
    comm.worldY = kDebriefMissionGridY << kDebriefWorldGridShift;
    gridFlags[kDebriefMissionGridX + kDebriefMissionGridY * kDebriefWorldGridWidth] =
        kDebriefMissionGridFlags;
    computeMissionResult();
    require(missionResult == (kDebriefMissionGridFlags & kDebriefMissionResultMask),
            "computeMissionResult preserves the original 16-column world-grid lookup and low-bit mask");

    resetDebriefState(comm, game);
    MenuItem hitItem = {};
    hitItem.hitX1 = kDebriefHitX1;
    hitItem.hitX2 = kDebriefHitX2;
    hitItem.hitY1 = kDebriefHitY1;
    hitItem.hitY2 = kDebriefHitY2;
    cursorX = kDebriefCursorInsideX;
    cursorY = kDebriefCursorInsideY;
    require(isPointInRect(&hitItem) == 1,
            "isPointInRect includes points inside the original inclusive hit rectangle");
    cursorX = kDebriefCursorOutsideX;
    require(isPointInRect(&hitItem) == 0,
            "isPointInRect rejects points outside the original inclusive hit rectangle");

    resetDebriefState(comm, game);
    int16 blinkPage[8] = {};
    MenuItem blinkItem = {};
    blinkItem.colorPair = kDebriefBlinkColorPair;
    blinkItem.colorX1 = kDebriefBlinkX1;
    blinkItem.colorY1 = kDebriefBlinkY1;
    blinkItem.colorX2 = kDebriefBlinkX2;
    blinkItem.colorY2 = kDebriefBlinkY2;
    blinkWidget(&blinkItem, blinkPage);
    require(blinkItem.state == 1 && g_switchColorCalls == 2 &&
                g_lastSwitchFromColor == kDebriefBlinkColorFrom &&
                g_lastSwitchToColor == kDebriefBlinkColorTo &&
                g_lastSwitchX1 == kDebriefBlinkX1 && g_lastSwitchY1 == kDebriefBlinkY1 &&
                g_lastSwitchX2 == kDebriefBlinkX2 && g_lastSwitchY2 == kDebriefBlinkY2,
            "blinkWidget turns an unselected item on and repeats the original color swap");
    blinkWidget(&blinkItem, blinkPage);
    require(blinkItem.state == 0 && g_switchColorCalls == 3 &&
                g_lastSwitchFromColor == kDebriefBlinkColorTo &&
                g_lastSwitchToColor == kDebriefBlinkColorFrom,
            "blinkWidget turns a selected item off with the original reverse color swap");

    resetDebriefState(comm, game);
    timerWait(kDebriefTimerWaitTicks);
    require(g_setTimerCalls == 1 && g_restoreTimerCalls == 1 &&
                g_timerYieldCalls == kDebriefTimerWaitTicks + 1 &&
                timerCounter == kDebriefTimerWaitTicks + 1,
            "timerWait keeps yielding while ticks is greater than or equal to the original counter");

    resetDebriefState(comm, game);
    int16 inputPage[8] = {};
    const int16 cursorBounds[] = {
        kDebriefInputStepX, kDebriefInputStepY,
        kDebriefInputMinX, kDebriefInputMaxX,
        kDebriefInputMinY, kDebriefInputMaxY,
    };
    MenuItem inputItem = {};
    inputItem.colorTableIdx = 0;
    cursorX = kDebriefInputStartX;
    cursorY = kDebriefInputStartY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, kDebriefInputEnterKey);
    }
    processDebriefInput(cursorBounds, &inputItem, inputPage);
    require(enterPressed == 1 &&
                inputChanged == 0 &&
                cursorX == kDebriefInputStartX &&
                cursorY == kDebriefInputStartY &&
                g_getKeyCalls == 1,
            "processDebriefInput sets the original ENTER flag without moving the cursor");

    resetDebriefState(comm, game);
    cursorX = kDebriefInputStartX;
    cursorY = kDebriefInputStartY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, KEYCODE_LEFTARROW);
    }
    processDebriefInput(cursorBounds, &inputItem, inputPage);
    require(inputChanged == 1 &&
                enterPressed == 0 &&
                cursorX == kDebriefInputLeftResultX &&
                cursorY == kDebriefInputStartY,
            "processDebriefInput applies the original LEFT key cursor step and lower bound logic");

    resetDebriefState(comm, game);
    MenuItem menuItems[kDebriefMenuItemCount] = {};
    menuItems[0].state = kDebriefMenuKeepState;
    menuItems[kDebriefMenuSelectedIndex].state = kDebriefMenuPendingState;
    menuItems[kDebriefMenuSelectedIndex].colorPair = kDebriefMenuColorPair;
    menuItems[kDebriefMenuSelectedIndex].colorX1 = kDebriefMenuColorX1;
    menuItems[kDebriefMenuSelectedIndex].colorY1 = kDebriefMenuColorY1;
    menuItems[kDebriefMenuSelectedIndex].colorX2 = kDebriefMenuColorX2;
    menuItems[kDebriefMenuSelectedIndex].colorY2 = kDebriefMenuColorY2;
    menuItems[2].state = 1;
    processMenuItems(menuItems, 0, kDebriefMenuItemCount,
                     kDebriefMenuCursorX, kDebriefMenuCursorY, inputPage);
    require(selectedMenuItem == kDebriefMenuSelectedIndex &&
                menuItems[0].state == kDebriefMenuKeepState &&
                menuItems[kDebriefMenuSelectedIndex].state == 1 &&
                menuItems[2].state == 0 &&
                cursorX == kDebriefMenuCursorX &&
                cursorY == kDebriefMenuCursorY,
            "processMenuItems preserves state 3, selects pending state 2, clears ordinary state, and resets the cursor");
    require(g_switchColorCalls == 2 &&
                g_lastSwitchFromColor == kDebriefMenuColorFrom &&
                g_lastSwitchToColor == kDebriefMenuColorTo,
            "processMenuItems uses blinkWidget's original double color swap for a pending item");

    resetDebriefState(comm, game);
    MenuItem plainItem = {};
    drawMenuItem(&plainItem, 0, inputPage);
    require(g_clearRectCalls == 0 &&
                g_drawWrappedTextCalls == 0 &&
                g_drawStringCalls == 0 &&
                g_copyRectCalls == 0 &&
                g_blitSpriteCalls == 0,
            "drawMenuItem is an original no-op for menu items without sprite flags");

    resetDebriefState(comm, game);
    MenuItem enterItem = {};
    enterItem.hitX1 = kDebriefMenuHitX1;
    enterItem.hitX2 = kDebriefMenuHitX2;
    enterItem.hitY1 = kDebriefMenuHitY1;
    enterItem.hitY2 = kDebriefMenuHitY2;
    enterItem.colorX1 = kDebriefMenuColorX1;
    enterItem.colorY1 = kDebriefMenuColorY1;
    enterItem.colorX2 = kDebriefMenuColorX2;
    enterItem.colorY2 = kDebriefMenuColorY2;
    enterItem.colorTableIdx = 0;
    enterItem.flags = MENUITEM_SELECTABLE | MENUITEM_ENABLED;
    selectedMenuItem = 0;
    cursorX = kDebriefMenuCursorX;
    cursorY = kDebriefMenuCursorY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, kDebriefInputEnterKey);
    }
    int16 selectBounds[] = {
        kDebriefInputStepX, kDebriefInputStepY,
        kDebriefInputMinX, kDebriefInputMaxX,
        kDebriefInputMinY, kDebriefInputMaxY,
    };
    const int selected = selectMenuItem(&enterItem, 0, 1, selectBounds, inputPage);
    require(selected == 0 &&
                enterPressed == 1 &&
                joyRepeatFlag == 0 &&
                colorAnimEnabled == 0,
            "selectMenuItem returns the original selected item when ENTER is pressed inside its hit rectangle");
    require(g_switchColorCalls == kDebriefMenuEnterSwitchCount &&
                g_lastSwitchFromColor == kDebriefMenuEnterLastFrom &&
                g_lastSwitchToColor == kDebriefMenuEnterTo &&
                g_lastSwitchX1 == kDebriefMenuColorX1 &&
                g_lastSwitchY1 == kDebriefMenuColorY1 &&
                g_lastSwitchX2 == kDebriefMenuColorX2 &&
                g_lastSwitchY2 == kDebriefMenuColorY2,
            "selectMenuItem preserves the original ENTER color-table-0 confirmation flashes");

    resetDebriefState(comm, game);
    MenuItem disabledItem = enterItem;
    disabledItem.flags = MENUITEM_SELECTABLE; /* Selectable but not enabled triggers the original color animation. */
    selectedMenuItem = 0;
    cursorX = kDebriefMenuCursorX;
    cursorY = kDebriefMenuCursorY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, kDebriefInputEnterKey);
    }
    const int disabledSelected = selectMenuItem(&disabledItem, 0, 1, selectBounds, inputPage);
    require(disabledSelected == 0 &&
                colorAnimEnabled == kDebriefMenuDisabledColorAnim,
            "selectMenuItem preserves original disabled-item color animation enable before ENTER");

    resetDebriefState(comm, game);
    MenuItem scanItems[2] = {};
    scanItems[0] = enterItem;
    scanItems[1] = enterItem;
    scanItems[0].hitX1 = kDebriefMenuSecondHitX1;
    scanItems[0].hitX2 = kDebriefMenuSecondHitX2;
    scanItems[1].hitX1 = kDebriefMenuHitX1;
    scanItems[1].hitX2 = kDebriefMenuHitX2;
    scanItems[1].hitY1 = kDebriefMenuHitY1;
    scanItems[1].hitY2 = kDebriefMenuHitY2;
    selectedMenuItem = 1;
    cursorX = kDebriefMenuCursorX;
    cursorY = kDebriefMenuCursorY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, kDebriefInputEnterKey);
    }
    const int scanSelected = selectMenuItem(scanItems, 0, 2, selectBounds, inputPage);
    require(scanSelected == 1,
            "selectMenuItem preserves original initial hit-test scan past nonmatching items");

    resetDebriefState(comm, game);
    MenuItem rescanItems[2] = {};
    rescanItems[0] = enterItem;
    rescanItems[1] = enterItem;
    rescanItems[0].hitX1 = kDebriefMenuSecondHitX1;
    rescanItems[0].hitX2 = kDebriefMenuSecondHitX2;
    rescanItems[1].hitX1 = kDebriefMenuHitX1;
    rescanItems[1].hitX2 = kDebriefMenuHitX2;
    rescanItems[1].hitY1 = kDebriefMenuHitY1;
    rescanItems[1].hitY2 = kDebriefMenuHitY2;
    selectedMenuItem = 0;
    cursorX = kDebriefMenuCursorX;
    cursorY = kDebriefMenuCursorY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, kDebriefInputEnterKey);
    }
    const int rescanSelected = selectMenuItem(rescanItems, 0, 2, selectBounds, inputPage);
    require(rescanSelected == 1 &&
                selectedMenuItem == 0,
            "selectMenuItem preserves original ENTER rescan when cursor item differs from selected item");

    resetDebriefState(comm, game);
    MenuItem mismatchItems[2] = {};
    mismatchItems[0] = enterItem;
    mismatchItems[1] = enterItem;
    mismatchItems[1].hitX1 = kDebriefMenuSecondHitX1;
    mismatchItems[1].hitX2 = kDebriefMenuSecondHitX2;
    mismatchItems[1].hitY1 = kDebriefMenuSecondHitY1;
    mismatchItems[1].hitY2 = kDebriefMenuSecondHitY2;
    selectedMenuItem = 1;
    cursorX = kDebriefMenuCursorX;
    cursorY = kDebriefMenuCursorY;
    {
        const int keyStates[] = {0, 0};
        setKeyScript(keyStates, 2, kDebriefInputEnterKey);
    }
    const int mismatchSelected = selectMenuItem(mismatchItems, 0, 2, selectBounds, inputPage);
    require(mismatchSelected == 0 &&
                selectedMenuItem == 1 &&
                g_switchColorCalls == kDebriefMenuEnterSwitchCount,
            "selectMenuItem preserves original ENTER path when cursor item differs from selected item");

    resetDebriefState(comm, game);
    MenuItem moveItems[2] = {};
    moveItems[0].hitX1 = kDebriefMenuHitX1;
    moveItems[0].hitX2 = kDebriefMenuHitX2;
    moveItems[0].hitY1 = kDebriefMenuHitY1;
    moveItems[0].hitY2 = kDebriefMenuHitY2;
    moveItems[0].colorX1 = kDebriefMenuColorX1;
    moveItems[0].colorY1 = kDebriefMenuColorY1;
    moveItems[0].colorX2 = kDebriefMenuColorX2;
    moveItems[0].colorY2 = kDebriefMenuColorY2;
    moveItems[0].colorTableIdx = kDebriefMenuColorTableOne;
    moveItems[0].flags = MENUITEM_SELECTABLE | MENUITEM_ENABLED;
    moveItems[0].state = 1;
    moveItems[1].hitX1 = kDebriefMenuSecondHitX1;
    moveItems[1].hitX2 = kDebriefMenuSecondHitX2;
    moveItems[1].hitY1 = kDebriefMenuSecondHitY1;
    moveItems[1].hitY2 = kDebriefMenuSecondHitY2;
    moveItems[1].colorX1 = kDebriefMenuColorX1;
    moveItems[1].colorY1 = kDebriefMenuColorY1;
    moveItems[1].colorX2 = kDebriefMenuColorX2;
    moveItems[1].colorY2 = kDebriefMenuColorY2;
    moveItems[1].colorPair = kDebriefMenuColorPair;
    moveItems[1].flags = MENUITEM_SELECTABLE | MENUITEM_ENABLED;
    selectedMenuItem = 0;
    cursorX = kDebriefMenuMoveStartX;
    cursorY = kDebriefMenuMoveStartY;
    {
        const int keyStates[] = {kDebriefNoKeyAvailable, kDebriefNoKeyAvailable,
                                 kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        const int getKeys[] = {KEYCODE_RIGHTARROW, kDebriefInputEnterKey};
        setKeyAndGetScript(keyStates, 4, getKeys, 2);
    }
    int16 moveBounds[] = {
        kDebriefMenuMoveStepX, kDebriefInputStepY,
        kDebriefInputMinX, kDebriefMenuSecondHitX2,
        kDebriefInputMinY, kDebriefInputMaxY,
    };
    const int movedSelected = selectMenuItem(moveItems, 0, 2, moveBounds, inputPage);
    require(movedSelected == kDebriefMenuMovedIndex &&
                selectedMenuItem == kDebriefMenuMovedIndex &&
                cursorX == kDebriefMenuMoveStartX + kDebriefMenuMoveStepX &&
                g_switchColorCalls >= kDebriefMenuMoveSwitchCount,
            "selectMenuItem preserves original moved-selection, color-table-1 deselect, and final ENTER path");

    resetDebriefState(comm, game);
    MenuItem moveColorZeroItems[2] = {};
    moveColorZeroItems[0] = moveItems[0];
    moveColorZeroItems[0].colorTableIdx = 0;
    moveColorZeroItems[0].state = 1;
    moveColorZeroItems[1] = moveItems[1];
    selectedMenuItem = 0;
    cursorX = kDebriefMenuMoveStartX;
    cursorY = kDebriefMenuMoveStartY;
    {
        const int keyStates[] = {kDebriefNoKeyAvailable, kDebriefNoKeyAvailable,
                                 kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        const int getKeys[] = {KEYCODE_RIGHTARROW, kDebriefInputEnterKey};
        setKeyAndGetScript(keyStates, 4, getKeys, 2);
    }
    const int movedColorZero = selectMenuItem(moveColorZeroItems, 0, 2, moveBounds, inputPage);
    require(movedColorZero == kDebriefMenuMovedIndex &&
                g_switchColorCalls >= kDebriefMenuColorTableZeroMoveSwitchCount,
            "selectMenuItem preserves original color-table-0 deselect flashes while moving selection");

    for (auto [key, expectedX, expectedY] : {
             std::tuple{KEYCODE_UPARROW, kDebriefInputStartX, kDebriefInputUpResultY},
             std::tuple{KEYCODE_DNARROW, kDebriefInputStartX, kDebriefInputDownResultY},
             std::tuple{KEYCODE_RIGHTARROW, kDebriefInputRightResultX, kDebriefInputStartY},
         }) {
        resetDebriefState(comm, game);
        cursorX = kDebriefInputStartX;
        cursorY = kDebriefInputStartY;
        const int keyStates[] = {kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        setKeyScript(keyStates, 2, key);
        processDebriefInput(cursorBounds, &inputItem, inputPage);
        require(inputChanged == 1 && cursorX == expectedX && cursorY == expectedY,
                "processDebriefInput applies original arrow-key cursor movement and clamps");
    }

    resetDebriefState(comm, game);
    cursorX = kDebriefInputStartX;
    cursorY = kDebriefInputStartY;
    {
        const int keyStates[] = {kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        setKeyScript(keyStates, 2, kDebriefInputAltQKey);
    }
    processDebriefInput(cursorBounds, &inputItem, inputPage);
    require(quitFlag == 1 && enterPressed == 1,
            "processDebriefInput preserves the original Alt-Q quit-and-enter state without exiting until the next poll");

    {
        const pid_t pid = fork();
        require(pid >= 0, "fork for processDebriefInput quit-path coverage");
        if (pid == 0) {
            resetDebriefState(comm, game);
            quitFlag = 1;
            const int keyStates[] = {kDebriefKeyAvailable, kDebriefNoKeyAvailable};
            setKeyScript(keyStates, 2, kDebriefInputEscapeKey);
            processDebriefInput(cursorBounds, &inputItem, inputPage);
            std::exit(2);
        }
        int status = 0;
        require(waitpid(pid, &status, 0) == pid &&
                    WIFEXITED(status) &&
                    WEXITSTATUS(status) == 0,
                "processDebriefInput preserves original quitFlag cleanup and process exit path");
    }

    for (auto [axisX, axisY, expectedX, expectedY] : {
             std::tuple{JOY_DEADZONE_LO - 1, JOY_CENTER, kDebriefInputLeftResultX, kDebriefInputStartY},
             std::tuple{JOY_DEADZONE_HI + 1, JOY_CENTER, kDebriefInputRightResultX, kDebriefInputStartY},
             std::tuple{JOY_CENTER, JOY_DEADZONE_LO - 1, kDebriefInputStartX, kDebriefInputUpResultY},
             std::tuple{JOY_CENTER, JOY_DEADZONE_HI + 1, kDebriefInputStartX, kDebriefInputDownResultY},
         }) {
        resetDebriefState(comm, game);
        cursorX = kDebriefInputStartX;
        cursorY = kDebriefInputStartY;
        joyAxisX = static_cast<uint8>(axisX);
        joyAxisY = static_cast<uint8>(axisY);
        const int keyStates[] = {kDebriefKeyAvailable, kDebriefKeyAvailable};
        setKeyScript(keyStates, 2, 0);
        processDebriefInput(cursorBounds, &inputItem, inputPage);
        require(inputChanged == 1 && joyRepeatFlag == 1 &&
                    cursorX == expectedX && cursorY == expectedY,
                "processDebriefInput maps original joystick axis thresholds to arrow-key movement");
    }

    for (auto [button0, button1, expectedEnter] : {
             std::tuple{1, 0, 1},
             std::tuple{0, 1, 0},
         }) {
        resetDebriefState(comm, game);
        comm.setupUseJoy = 1;
        const int joystickValues[] = {button0, button1};
        setJoystickScript(joystickValues, 2);
        const int keyStates[] = {kDebriefKeyAvailable, kDebriefKeyAvailable};
        setKeyScript(keyStates, 2, 0);
        processDebriefInput(cursorBounds, &inputItem, inputPage);
        require((enterPressed == expectedEnter) &&
                    g_readJoystickCalls == 2,
                "processDebriefInput preserves original joystick button-to-ENTER/ESC mapping");
    }

    resetDebriefState(comm, game);
    MenuItem animItem = {};
    animItem.colorX1 = kDebriefMenuColorX1;
    animItem.colorY1 = kDebriefMenuColorY1;
    animItem.colorX2 = kDebriefMenuColorX2;
    animItem.colorY2 = kDebriefMenuColorY2;
    colorAnimEnabled = 1;
    g_tickTimer2OnCheck = true;
    {
        const int keyStates[] = {
            kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable,
            kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable,
            kDebriefKeyAvailable, kDebriefNoKeyAvailable,
        };
        setKeyScript(keyStates, 8, kDebriefInputEscapeKey);
        g_tickTimer2OnCheck = true;
    }
    processDebriefInput(cursorBounds, &animItem, inputPage);
    require(colorAnimIdx == 1 &&
                g_switchColorCalls == 1,
            "processDebriefInput preserves original timed color-animation color swap");

    resetDebriefState(comm, game);
    comm.setupUseJoy = 1;
    {
        const int joystickValues[] = {0, 0, 0, 0};
        const int keyStates[] = {kDebriefKeyAvailable, kDebriefNoKeyAvailable};
        setJoystickScript(joystickValues, 4);
        setKeyScript(keyStates, 2, kDebriefInputEscapeKey);
    }
    processDebriefInput(cursorBounds, &inputItem, inputPage);
    require(g_readJoystickCalls == 4,
            "processDebriefInput preserves original joystick pre-read and in-loop re-read cadence");

    resetDebriefState(comm, game);
    joyRepeatFlag = 1;
    g_tickTimerOnCheck = true;
    {
        const int keyStates[] = {
            kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable,
            kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable,
            kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable,
            kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable, kDebriefKeyAvailable,
            kDebriefKeyAvailable, kDebriefNoKeyAvailable, kDebriefNoKeyAvailable,
        };
        setKeyScript(keyStates, 19, kDebriefInputEscapeKey);
    }
    processDebriefInput(cursorBounds, &inputItem, inputPage);
    require(joyRepeatFlag == 0 &&
                timerCounter > 15,
            "processDebriefInput preserves original joystick repeat timeout after sixteen timer ticks");

    for (auto [startX, startY, key, expectedX, expectedY] : {
             std::tuple<int, int, int, int, int>{kDebriefInputStartX, kDebriefInputMinY - 1, KEYCODE_LEFTARROW,
                                                 kDebriefInputStartX, kDebriefInputMinY - 1},
             std::tuple<int, int, int, int, int>{kDebriefInputMinX, kDebriefInputStartY, KEYCODE_LEFTARROW,
                                                 kDebriefInputMinX, kDebriefInputStartY},
             std::tuple<int, int, int, int, int>{kDebriefInputStartX, kDebriefInputMinY - 1, KEYCODE_UPARROW,
                                                 kDebriefInputStartX, kDebriefInputMinY},
             std::tuple<int, int, int, int, int>{kDebriefInputStartX, kDebriefInputMaxY + 1, KEYCODE_DNARROW,
                                                 kDebriefInputStartX, kDebriefInputMaxY},
             std::tuple<int, int, int, int, int>{kDebriefInputMaxX + 1, kDebriefInputStartY, KEYCODE_RIGHTARROW,
                                                 kDebriefInputMaxX, kDebriefInputStartY},
         }) {
        resetDebriefState(comm, game);
        cursorX = static_cast<uint16>(startX);
        cursorY = static_cast<uint16>(startY);
        const int keyStates[] = {kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        setKeyScript(keyStates, 2, key);
        processDebriefInput(cursorBounds, &inputItem, inputPage);
        require(cursorX == expectedX && cursorY == expectedY,
                "processDebriefInput preserves original cursor clamp edge cases");
    }

    for (auto [status, expectedFirstSprite] : {
             std::pair{EVENT_AIR_KILL, spriteAirBlink},
             std::pair{EVENT_AIR_KILL2, spriteAirBlink},
             std::pair{EVENT_SAM_KILL, spriteSamBlink},
             std::pair{EVENT_GROUND_KILL, spriteGroundBlink},
             std::pair{EVENT_BOMB_HIT, spriteWaypointBlink},
             std::pair{EVENT_EJECTED, spriteSamBlink},
             std::pair{EVENT_WAYPOINT, spriteWaypointBlink},
         }) {
        resetDebriefState(comm, game);
        MenuItem spriteItem = {};
        spriteItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK;
        flightRecords[0].status = static_cast<char>(status);
        flightRecords[0].unitId = kDebriefSpriteAirUnit;
        flightRecords[0].mapX = static_cast<char>(kDebriefSpriteX);
        flightRecords[0].mapY = static_cast<char>(kDebriefSpriteY);
        if (status == EVENT_AIR_KILL) {
            slotInfoTable[(flightRecords[0].unitId & UNIT_ID_MASK) * 16] = kDebriefAirSlotFlag;
        }
        curRecordIdx = 0;
        timerCounter3 = kDebriefSpriteTimerThreshold;
        spriteToggle = 1;
        const int keyStates[] = {kDebriefKeyAvailable, kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        setKeyScript(keyStates, 3, kDebriefInputEscapeKey);
        processDebriefInput(cursorBounds, &spriteItem, inputPage);
        require(g_blitSpriteCalls >= 2 && g_spriteLog[0] == expectedFirstSprite,
                "processDebriefInput draws the original blinking event sprite before cleanup redraw");
        if (status == EVENT_AIR_KILL) {
            require(spriteAirBlink->srcX == kDebriefAirFriendlySpriteX,
                    "processDebriefInput preserves original friendly-air blink source column");
        } else if (status == EVENT_AIR_KILL2) {
            require(spriteAirBlink->srcX == kDebriefAirHostileSpriteX,
                    "processDebriefInput preserves original hostile-air blink source column");
        }
    }

    resetDebriefState(comm, game);
    MenuItem normalSpriteItem = {};
    normalSpriteItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[0].mapX = static_cast<char>(kDebriefSpriteX);
    flightRecords[0].mapY = static_cast<char>(kDebriefSpriteY);
    curRecordIdx = 0;
    timerCounter3 = kDebriefSpriteTimerThreshold;
    spriteToggle = 0;
    {
        const int keyStates[] = {kDebriefKeyAvailable, kDebriefNoKeyAvailable, kDebriefNoKeyAvailable};
        setKeyScript(keyStates, 3, kDebriefInputEscapeKey);
    }
    processDebriefInput(cursorBounds, &normalSpriteItem, inputPage);
    require(g_blitSpriteCalls >= 2 && g_spriteLog[0] == spriteWaypoint,
            "processDebriefInput redraws the normal event sprite on the original alternate blink phase");

    resetDebriefState(comm, game);
    flightRecords[0].status = EVENT_SAM_KILL;
    flightRecords[0].mapX = static_cast<char>(kDebriefSpriteX);
    flightRecords[0].mapY = static_cast<char>(kDebriefSpriteY);
    drawEventSprite(0);
    require(g_lastSprite == spriteSam,
            "drawEventSprite draws the original SAM event sprite");
    flightRecords[1].status = EVENT_EJECTED;
    flightRecords[1].mapX = static_cast<char>(kDebriefSpriteX);
    flightRecords[1].mapY = static_cast<char>(kDebriefSpriteY);
    drawEventSprite(1);
    require(g_lastSprite == spriteSam,
            "drawEventSprite draws the original ejection marker with the SAM sprite");

    resetDebriefState(comm, game);
    flightRecords[0].mapX = kDebriefAnimateStartX;
    flightRecords[0].mapY = kDebriefAnimateStartY;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[1].mapX = kDebriefAnimateTimestampX;
    flightRecords[1].mapY = kDebriefAnimateTimestampY;
    flightRecords[1].status = EVENT_TIMESTAMP;
    flightRecords[2].status = 0;
    flightTimeTable[3] = kOneGameMinuteTicks;
    popupVisible = 1;
    popupX = kDebriefAnimatePopupX;
    popupY = kDebriefAnimatePopupY;
    animateFlightPath(inputPage);
    require(popupVisible == 0 &&
                g_copyRectCalls == 1 &&
                g_copySnapshots[0].srcPage == 1 &&
                g_copySnapshots[0].srcX == 0 &&
                g_copySnapshots[0].srcY == POPUP_SAVE_Y &&
                g_copySnapshots[0].dstPage == 0 &&
                g_copySnapshots[0].dstX == kDebriefAnimatePopupX &&
                g_copySnapshots[0].dstY == kDebriefAnimatePopupY,
            "animateFlightPath first restores the original saved popup rectangle");
    require(curRecordIdx == 1 &&
                g_clearRectCalls == kDebriefAnimateClearCount &&
                g_timerYieldCalls == kDebriefAnimateYieldCount &&
                g_lineWrapperCalls == kDebriefAnimateLineCount &&
                prevDrawX == kDebriefAnimateTimestampX &&
                prevDrawY == kDebriefAnimateTimestampY &&
                g_lastGfxColor == 0,
            "animateFlightPath advances through timestamp records, waits six ticks, and draws the final original segment");

    resetDebriefState(comm, game);
    flightRecords[0].mapX = kDebriefAnimateStartX;
    flightRecords[0].mapY = kDebriefAnimateStartY;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[1].mapX = kDebriefAnimateTimestampX;
    flightRecords[1].mapY = kDebriefAnimateTimestampY;
    flightRecords[1].status = EVENT_TIMESTAMP;
    flightRecords[2].mapX = kDebriefAnimateSecondTimestampX;
    flightRecords[2].mapY = kDebriefAnimateSecondTimestampY;
    flightRecords[2].status = EVENT_TIMESTAMP;
    flightRecords[3].status = 0;
    flightTimeTable[3] = kOneGameMinuteTicks;
    flightTimeTable[6] = kOneGameMinuteTicks * 2;
    animateFlightPath(inputPage);
    require(curRecordIdx == 2 &&
                g_lineWrapperCalls == kDebriefAnimateTwoTimestampLineCount &&
                prevDrawX == kDebriefAnimateSecondTimestampX &&
                prevDrawY == kDebriefAnimateSecondTimestampY,
            "animateFlightPath preserves original chained timestamp line drawing");

    resetDebriefState(comm, game);
    flightRecords[0].mapX = kDebriefAnimateStartX;
    flightRecords[0].mapY = kDebriefAnimateStartY;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[1].status = 0;
    animateFlightPath(inputPage);
    require(curRecordIdx == 0 &&
                g_lineWrapperCalls == 1 &&
                prevDrawX == kDebriefAnimateStartX &&
                prevDrawY == kDebriefAnimateStartY,
            "animateFlightPath preserves original final first-segment draw when no timestamp records were animated");

    resetDebriefState(comm, game);
    flightRecords[0].mapX = 40;
    flightRecords[0].mapY = 50;
    flightRecords[0].status = EVENT_AIR_KILL;
    flightRecords[0].unitId = 2;
    curRecordIdx = 0;
    slotInfoTable[(flightRecords[0].unitId & UNIT_ID_MASK) * 16] = kDebriefAirSlotFlag;
    popupVisible = 1;
    popupX = 12;
    popupY = 34;
    showEventPopup();
    {
        const int eventScreenX = mapToScreenX(static_cast<unsigned char>(flightRecords[0].mapX)) + mapViewX1;
        const int eventScreenY = mapToScreenY(static_cast<unsigned char>(flightRecords[0].mapY)) + mapViewY1;
        require(eventScreenX < kPopupThresholdX && eventScreenY < kPopupThresholdY,
                "showEventPopup test sample starts in the original top-left popup quadrant");
        require(popupVisible == 1 && popupX == eventScreenX + kPopupOffsetLow &&
                    popupY == eventScreenY + kPopupOffsetLow,
                "showEventPopup places top-left events down and right from the map marker");
        require(g_copyRectCalls == 3,
                "showEventPopup restores an existing popup then saves and draws the new popup");
        require(g_copySnapshots[0].srcPage == 1 && g_copySnapshots[0].srcX == 0 &&
                    g_copySnapshots[0].srcY == POPUP_SAVE_Y &&
                    g_copySnapshots[0].dstPage == 0 && g_copySnapshots[0].dstX == 12 &&
                    g_copySnapshots[0].dstY == 34,
                "showEventPopup restores the previous original popup rectangle first");
        require(g_copySnapshots[1].srcPage == 0 && g_copySnapshots[1].srcX == popupX &&
                    g_copySnapshots[1].srcY == popupY &&
                    g_copySnapshots[1].dstPage == 1 && g_copySnapshots[1].dstX == 0 &&
                    g_copySnapshots[1].dstY == POPUP_SAVE_Y,
                "showEventPopup saves the new map area to the original popup scratch slot");
        require(g_copySnapshots[2].srcPage == 1 &&
                    g_copySnapshots[2].srcX == popupSpriteX[kPopupAirFriendlySprite] &&
                    g_copySnapshots[2].srcY == popupSpriteY[kPopupAirFriendlySprite] &&
                    g_copySnapshots[2].dstPage == 0 &&
                    g_copySnapshots[2].dstX == popupX && g_copySnapshots[2].dstY == popupY &&
                    g_copySnapshots[2].width == POPUP_WIDTH &&
                    g_copySnapshots[2].height == POPUP_HEIGHT,
                "showEventPopup chooses the original friendly-air popup sprite");
    }

    resetDebriefState(comm, game);
    flightRecords[0].mapX = static_cast<char>(150);
    flightRecords[0].mapY = static_cast<char>(50);
    flightRecords[0].status = EVENT_AIR_KILL;
    flightRecords[0].unitId = 3;
    curRecordIdx = 0;
    showEventPopup();
    {
        const int eventScreenX = mapToScreenX(static_cast<unsigned char>(flightRecords[0].mapX)) + mapViewX1;
        const int eventScreenY = mapToScreenY(static_cast<unsigned char>(flightRecords[0].mapY)) + mapViewY1;
        require(eventScreenX >= kPopupThresholdX && eventScreenY < kPopupThresholdY,
                "showEventPopup test sample starts in the original top-right popup quadrant");
        require(popupX == eventScreenX + kPopupOffsetHighX &&
                    popupY == eventScreenY + kPopupOffsetLow,
                "showEventPopup places top-right events left and down from the map marker");
        require(g_copySnapshots[1].srcX == popupSpriteX[kPopupAirHostileSprite] &&
                    g_copySnapshots[1].srcY == popupSpriteY[kPopupAirHostileSprite],
                "showEventPopup chooses the original hostile-air popup sprite when slot flag is clear");
    }

    resetDebriefState(comm, game);
    flightRecords[0].mapX = static_cast<char>(150);
    flightRecords[0].mapY = static_cast<char>(150);
    flightRecords[0].status = EVENT_EJECTED;
    curRecordIdx = 0;
    showEventPopup();
    {
        const int eventScreenX = mapToScreenX(static_cast<unsigned char>(flightRecords[0].mapX)) + mapViewX1;
        const int eventScreenY = mapToScreenY(static_cast<unsigned char>(flightRecords[0].mapY)) + mapViewY1;
        require(eventScreenX >= kPopupThresholdX && eventScreenY >= kPopupThresholdY,
                "showEventPopup test sample starts in the original bottom-right popup quadrant");
        require(popupX == eventScreenX + kPopupOffsetHighX &&
                    popupY == eventScreenY + kPopupOffsetHighY,
                "showEventPopup places bottom-right events left and up from the map marker");
        require(g_copySnapshots[1].srcX == popupSpriteX[kPopupEjectStartSprite] &&
                    g_copySnapshots[1].srcY == popupSpriteY[kPopupEjectStartSprite] &&
                    ejectedFlag == 0,
                "showEventPopup uses the original first-record ejection sprite without setting ejectedFlag");
    }

    resetDebriefState(comm, game);
    flightRecords[1].mapX = 40;
    flightRecords[1].mapY = static_cast<char>(150);
    flightRecords[1].status = EVENT_EJECTED;
    curRecordIdx = 1;
    comm.landingType = LANDING_SAFE;
    showEventPopup();
    {
        const int eventScreenX = mapToScreenX(static_cast<unsigned char>(flightRecords[1].mapX)) + mapViewX1;
        const int eventScreenY = mapToScreenY(static_cast<unsigned char>(flightRecords[1].mapY)) + mapViewY1;
        require(eventScreenX < kPopupThresholdX && eventScreenY >= kPopupThresholdY,
                "showEventPopup test sample starts in the original bottom-left popup quadrant");
        require(popupX == eventScreenX + kPopupOffsetLow &&
                    popupY == eventScreenY + kPopupOffsetHighY &&
                    ejectedFlag == 1,
                "showEventPopup places bottom-left ejection events right and up and marks ejected");
        require(g_copySnapshots[1].srcX == popupSpriteX[kPopupEjectSafeSprite] &&
                    g_copySnapshots[1].srcY == popupSpriteY[kPopupEjectSafeSprite],
                "showEventPopup uses the original safe-landing ejection popup sprite");
    }

    resetDebriefState(comm, game);
    flightRecords[1].mapX = 40;
    flightRecords[1].mapY = 50;
    flightRecords[1].status = EVENT_EJECTED;
    curRecordIdx = 1;
    comm.landingType = LANDING_CRASHED;
    showEventPopup();
    require(g_copySnapshots[1].srcX == popupSpriteX[kPopupEjectCrashSprite] &&
                g_copySnapshots[1].srcY == popupSpriteY[kPopupEjectCrashSprite],
            "showEventPopup uses the original crashed-ejection popup sprite");
    resetDebriefState(comm, game);
    flightRecords[1].mapX = 40;
    flightRecords[1].mapY = 50;
    flightRecords[1].status = EVENT_EJECTED;
    curRecordIdx = 1;
    missionResult = 0;
    showEventPopup();
    require(g_copySnapshots[1].srcX == popupSpriteX[kPopupEjectMissionOkSprite] &&
                g_copySnapshots[1].srcY == popupSpriteY[kPopupEjectMissionOkSprite],
            "showEventPopup uses the original successful-mission ejection popup sprite");
    resetDebriefState(comm, game);
    flightRecords[1].mapX = 40;
    flightRecords[1].mapY = 50;
    flightRecords[1].status = EVENT_EJECTED;
    curRecordIdx = 1;
    missionResult = 1;
    showEventPopup();
    require(g_copySnapshots[1].srcX == popupSpriteX[kPopupEjectMissionFailedSprite] &&
                g_copySnapshots[1].srcY == popupSpriteY[kPopupEjectMissionFailedSprite],
            "showEventPopup uses the original failed-mission ejection popup sprite");

    for (auto [status, spriteIndex] : {
             std::pair{EVENT_AIR_KILL2, kPopupAirKill2Sprite},
             std::pair{EVENT_SAM_KILL, kPopupSamSprite},
             std::pair{EVENT_GROUND_KILL, kPopupGroundSprite},
             std::pair{EVENT_BOMB_HIT, kPopupBombSprite},
             std::pair{EVENT_WAYPOINT, kPopupWaypointSprite},
         }) {
        resetDebriefState(comm, game);
        flightRecords[0].mapX = 40;
        flightRecords[0].mapY = 50;
        flightRecords[0].status = static_cast<char>(status);
        curRecordIdx = 0;
        showEventPopup();
        require(g_copySnapshots[1].srcX == popupSpriteX[spriteIndex] &&
                    g_copySnapshots[1].srcY == popupSpriteY[spriteIndex],
                "showEventPopup maps each original event type to the expected popup sprite");
    }

    resetDebriefState(comm, game);
    MenuItem summaryItem = {};
    summaryItem.flags = MENUITEM_HAS_SPRITE | kDebriefMissionSummaryType;
    comm.weaponCount[0] = 1;
    comm.trainingFlag = 1;
    game.totalScore = kDebriefScoreTotal;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[0].mapX = 40;
    flightRecords[0].mapY = 50;
    flightRecords[1].status = 0;
    popupVisible = 1;
    popupX = kDebriefAnimatePopupX;
    popupY = kDebriefAnimatePopupY;
    drawMenuItem(&summaryItem, 0, inputPage);
    require(ejectedFlag == 1 && totalFlightRecords == 0 &&
                g_drawWrappedTextCalls >= 1 &&
                g_drawStringCalls >= 1 &&
                popupVisible == 0 &&
                g_copySnapshots[0].dstX == kDebriefAnimatePopupX &&
                g_copySnapshots[0].dstY == kDebriefAnimatePopupY,
            "drawMenuItem preserves the original training mission-summary rendering and popup-restore path");

    resetDebriefState(comm, game);
    summaryItem.flags = MENUITEM_HAS_SPRITE | kDebriefMissionSummaryType;
    comm.weaponCount[0] = 1;
    comm.trainingFlag = 0;
    game.totalScore = kDebriefScoreTotal;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[0].mapX = 40;
    flightRecords[0].mapY = 50;
    flightRecords[1].status = 0;
    drawMenuItem(&summaryItem, 0, inputPage);
    require(ejectedFlag == 1 && g_drawWrappedTextCalls >= 1 &&
                g_drawStringCalls >= 1,
            "drawMenuItem preserves the original recorded-career mission-summary rendering path");

    static char worldRefName[] = "BASE";
    static char worldObjectName[] = "HANGAR";
    static char plainWorldName[] = "TARGET";
    resetDebriefState(comm, game);
    worldStrings[kDebriefWorldRef] = worldRefName;
    worldStrings[kDebriefWorldObject] = worldObjectName;
    worldStrings[kDebriefWorldObject + 1] = plainWorldName;
    worldObjects[kDebriefWorldObject].unitRef = kDebriefWorldRef;
    worldObjects[kDebriefWorldObject].objectIdx = kDebriefWorldObject;
    worldObjects[kDebriefWorldObject + 1].objectIdx = kDebriefWorldObject + 1;
    std::strncpy(planeArray[kDebriefPlaneUnit].name, "MIG-23 X", sizeof(planeArray[kDebriefPlaneUnit].name));
    std::strncpy(samWeaponTable[kDebriefSamWeapon].name, "SA-2", sizeof(samWeaponTable[kDebriefSamWeapon].name));
    MenuItem detailItem = {};
    detailItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK | kDebriefEventDetailType;
    flightTimeTable[0] = kDebriefDrawMenuFlightTime;
    curRecordIdx = 0;
    for (auto [status, unit] : {
             std::pair<int, int>{EVENT_AIR_KILL, kDebriefWorldObject},
             std::pair<int, int>{EVENT_AIR_KILL2, kDebriefWorldObject + 1},
             std::pair<int, int>{EVENT_SAM_KILL, kDebriefPlaneUnit},
             std::pair<int, int>{EVENT_GROUND_KILL, kDebriefWorldObject + 1},
             std::pair<int, int>{EVENT_WAYPOINT, kDebriefWorldObject},
             std::pair<int, int>{EVENT_BOMB_HIT, kDebriefSamWeapon},
         }) {
        flightRecords[0].status = static_cast<char>(status | STATUS_PRIMARY_HIT | STATUS_SECONDARY_HIT);
        flightRecords[0].unitId = static_cast<char>(unit);
        flightRecords[0].mapX = 40;
        flightRecords[0].mapY = 50;
        drawMenuItem(&detailItem, 0, inputPage);
        require(g_drawWrappedTextCalls >= 1 && popupVisible == 1,
                "drawMenuItem preserves original per-event detail rendering and popup display");
        popupVisible = 0;
    }

    resetDebriefState(comm, game);
    worldStrings[kDebriefWorldObject] = worldObjectName;
    worldObjects[kDebriefWorldObject].unitRef = 0;
    worldObjects[kDebriefWorldObject].objectIdx = kDebriefWorldObject;
    detailItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK | kDebriefEventDetailType;
    flightTimeTable[0] = kDebriefDrawMenuFlightTime;
    curRecordIdx = 0;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[0].unitId = kDebriefWorldObject;
    flightRecords[0].mapX = 40;
    flightRecords[0].mapY = 50;
    drawMenuItem(&detailItem, 0, inputPage);
    require(g_drawWrappedTextCalls >= 1 && popupVisible == 1,
            "drawMenuItem preserves original waypoint detail text without a parent world reference");

    resetDebriefState(comm, game);
    worldStrings[kDebriefWorldObject] = worldObjectName;
    worldStrings[kDebriefWorldRef] = worldRefName;
    worldObjects[kDebriefWorldObject].unitRef = kDebriefWorldRef;
    worldObjects[kDebriefWorldObject].objectIdx = kDebriefWorldObject;
    targetBlock.waypointData = kDebriefWorldObject;
    detailItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK | kDebriefEventDetailType;
    curRecordIdx = 0;
    flightRecords[0].status = EVENT_EJECTED;
    flightRecords[0].unitId = kDebriefWorldObject;
    drawMenuItem(&detailItem, 0, inputPage);
    require(g_drawWrappedTextCalls >= 1,
            "drawMenuItem preserves original first-record takeoff/ejection detail path");

    resetDebriefState(comm, game);
    worldStrings[kDebriefWorldObject] = worldObjectName;
    worldObjects[kDebriefWorldObject].unitRef = 0;
    worldObjects[kDebriefWorldObject].objectIdx = kDebriefWorldObject;
    targetBlock.waypointData = kDebriefWorldObject;
    detailItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK | kDebriefEventDetailType;
    curRecordIdx = 0;
    flightRecords[0].status = EVENT_EJECTED;
    flightRecords[0].unitId = kDebriefWorldObject;
    drawMenuItem(&detailItem, 0, inputPage);
    require(g_drawWrappedTextCalls >= 1,
            "drawMenuItem preserves original first-record takeoff text without a parent world reference");

    resetDebriefState(comm, game);
    worldStrings[kDebriefWorldObject] = worldObjectName;
    worldObjects[kDebriefWorldObject].objectIdx = kDebriefWorldObject;
    detailItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK | kDebriefEventDetailType;
    comm.weaponCount[0] = 1;
    ejectedFlag = 1;
    curRecordIdx = 1;
    flightTimeTable[3] = kDebriefDrawMenuFlightTime;
    flightRecords[1].status = EVENT_GROUND_KILL;
    flightRecords[1].unitId = kDebriefWorldObject;
    drawMenuItem(&detailItem, 0, inputPage);
    require(ejectedFlag == 0 && curRecordIdx == 0 &&
                g_clearRectCalls >= 2 &&
                g_drawWrappedTextCalls >= 1,
            "drawMenuItem preserves the original post-ejection overall-score redraw before event detail");

    for (auto [landingType, survived, result] : {
             std::tuple{LANDING_CRASHED, 0, 0},
             std::tuple{LANDING_EJECTED, 0, 1},
             std::tuple{LANDING_EJECTED, 0, 0},
             std::tuple{LANDING_EJECTED, 1, 1},
             std::tuple{LANDING_SAFE, 0, 0},
         }) {
        resetDebriefState(comm, game);
        detailItem.flags = MENUITEM_HAS_SPRITE | MENUITEM_SPRITE_BLINK | kDebriefEventDetailType;
        comm.landingType = landingType;
        comm.bailoutSurvived = survived;
        missionResult = result;
        curRecordIdx = 1;
        flightTimeTable[3] = kDebriefDrawMenuFlightTime;
        flightRecords[1].status = EVENT_EJECTED;
        drawMenuItem(&detailItem, 0, inputPage);
        require(g_drawWrappedTextCalls >= 1,
                "drawMenuItem preserves original mission-end landing/ejection detail branches");
    }

    resetDebriefState(comm, game);
    comm.weaponCount[0] = 20;
    comm.landingType = LANDING_EJECTED;
    game.difficulty = 2;
    flightRecords[0].status = EVENT_WAYPOINT;
    flightRecords[1].status = EVENT_AIR_KILL | STATUS_PRIMARY_HIT;
    flightRecords[2].status = EVENT_AIR_KILL;
    flightRecords[2].unitId = 4;
    flightRecords[3].status = EVENT_AIR_KILL2;
    flightRecords[3].unitId = 5;
    unitTypeTable[5] = 0x40;
    flightRecords[4].status = EVENT_SAM_KILL;
    flightRecords[4].unitId = 6;
    planeArray[7].validFlag = -1;
    flightRecords[5].status = EVENT_SAM_KILL | STATUS_SECONDARY_HIT;
    flightRecords[6].status = EVENT_GROUND_KILL;
    flightRecords[6].mapX = static_cast<char>(kFlightRecordGroundClearX);
    flightRecords[6].mapY = static_cast<char>(kFlightRecordGroundClearY);
    flightRecords[7].status = EVENT_GROUND_KILL;
    flightRecords[7].mapX = static_cast<char>(kFlightRecordGroundBlockedX);
    flightRecords[7].mapY = static_cast<char>(kFlightRecordGroundBlockedY);
    gridFlags[gridIndexForRecord(kFlightRecordGroundBlockedX, kFlightRecordGroundBlockedY)] = 1;
    flightRecords[8].status = EVENT_EJECTED;
    const long rawScore =
        ((2 - 1 * 2) * kMaxScoredWeaponCount * kMissionScoreAirFactor) +
        ((1 - 1 * 2) * (game.difficulty + 1) * kMissionScoreSamFactor) +
        ((1 - 1 * 2) * kMaxScoredWeaponCount * kMissionScoreGroundFactor) +
        (kMaxScoredWeaponCount * kMissionScorePrimaryFactor) +
        (kMaxScoredWeaponCount * kMissionScoreSecondaryFactor);
    const long waypointScaled = rawScore * 2 / 3;
    require(calcMissionScore(8) ==
                waypointScaled * kMissionScoreEjectNumerator / kMissionScoreEjectDenominator,
            "calcMissionScore preserves original counters, weapon cap, waypoint scale, and eject penalty");
    require(primaryHit == 1 && secondaryHit == 1 && airKilled == 2 && airMissed == 1 &&
                samKilled == 1 && samMissed == 1 && groundKilled == 1 && groundMissed == 1,
            "calcMissionScore updates original debrief hit/miss counters");

    resetDebriefState(comm, game);
    comm.weaponCount[0] = 1;
    comm.landingType = LANDING_CRASHED;
    flightRecords[0].status = EVENT_AIR_KILL;
    flightRecords[0].unitId = 3;
    unitTypeTable[3] = 0x40;
    flightRecords[1].status = EVENT_EJECTED;
    require(calcMissionScore(1) == 0, "calcMissionScore floors negative ejected scores before crash penalty");

    resetDebriefState(comm, game);
    comm.weaponCount[0] = 2;
    game.difficulty = 0;
    flightRecords[0].status = EVENT_AIR_KILL | STATUS_SECONDARY_HIT;
    flightRecords[1].status = EVENT_AIR_KILL;
    flightRecords[1].unitId = 1;
    *reinterpret_cast<int *>(&slotInfoTable[1 * 16]) = 0x500;
    flightRecords[2].status = EVENT_SAM_KILL;
    flightRecords[2].unitId = 2;
    planeArray[3].validFlag = 0;
    flightRecords[3].status = EVENT_GROUND_KILL | STATUS_SECONDARY_HIT;
    flightRecords[4].status = EVENT_GROUND_KILL;
    flightRecords[4].unitId = 4;
    unitTypeTable[4] = 0x40;
    calcMissionScore(4);
    require(secondaryHit == 1 &&
                airKilled == 1 &&
                airMissed == 1 &&
                samKilled == 1 &&
                groundKilled == 1 &&
                groundMissed == 1,
            "calcMissionScore preserves original secondary, slot-miss, normal-SAM, and ground-unit miss classifications");

    uint8 worldBytes[] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8 copyOut[6] = {};
    worldBufCursor = worldBytes;
    readFromWorldBuf(copyOut, 2, 3, nullptr);
    require(std::memcmp(copyOut, worldBytes, sizeof(copyOut)) == 0,
            "readFromWorldBuf copies contiguous DOS world-buffer bytes");
    require(worldBufCursor == worldBytes + sizeof(copyOut),
            "readFromWorldBuf advances the world-buffer cursor by size times count");

    uint8 copyIn[] = {9, 8, 7, 6};
    uint8 writeArea[6] = {};
    worldBufCursor = writeArea + 1;
    writeToWorldBuf(copyIn, 2, 2, nullptr);
    require(writeArea[0] == 0 && std::memcmp(writeArea + 1, copyIn, sizeof(copyIn)) == 0 &&
                writeArea[5] == 0,
            "writeToWorldBuf writes exactly the requested DOS world-buffer span");
    require(worldBufCursor == writeArea + 1 + sizeof(copyIn),
            "writeToWorldBuf advances the world-buffer cursor by size times count");

    uint8 loadWriteSrc[] = {0xA1, 0xB2, 0xC3, 0xD4};
    uint8 loadWriteArea[sizeof(loadWriteSrc) + 2] = {};
    worldDataReady = kWorldDataNotReady;
    worldBufCursor = loadWriteArea + 1;
    loadWorldData(loadWriteSrc, kWorldLoadBytes);
    require(std::memcmp(loadWriteArea + 1, loadWriteSrc, sizeof(loadWriteSrc)) == 0 &&
                loadWriteArea[0] == 0 &&
                loadWriteArea[sizeof(loadWriteArea) - 1] == 0,
            "loadWorldData writes to the original world buffer when worldDataReady is clear");
    require(worldBufCursor == loadWriteArea + 1 + sizeof(loadWriteSrc),
            "loadWorldData advances the write cursor by the original byte count");

    char farCopy[16] = {};
    farStrcpy(farCopy, "WORLD");
    require(std::strcmp(farCopy, "WORLD") == 0, "farStrcpy copies through the terminating nul");

    int16 drawPage[8] = {};
    drawStringAtPos(drawPage, "EAGLE", kDrawStringX, kDrawStringY);
    require(g_drawStringCalls == 1 &&
                g_lastDrawStringPage == drawPage &&
                std::strcmp(g_lastDrawStringText, "EAGLE") == 0 &&
                drawPage[4] == kDrawStringX &&
                drawPage[5] == kDrawStringY,
            "drawStringAtPos sets original text coordinates before drawing");
    drawFarString(drawPage, "VIPER");
    require(g_drawStringCalls == 2 &&
                g_lastDrawStringPage == drawPage &&
                std::strcmp(g_lastDrawStringText, "VIPER") == 0,
            "drawFarString copies far text through a local draw buffer");

    resetDebriefState(comm, game);
    SDL_IOStream *directHandle = openFileWrapper("DIRECT.PIC", kFilePicMode);
    require(directHandle == reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFilePicHandle)) &&
                g_openFileCalls == 1 &&
                std::strcmp(g_lastOpenFileName, "DIRECT.PIC") == 0 &&
                g_lastOpenMode == kFilePicMode,
            "openFileWrapper forwards filename and mode to the original shared open routine");
    closeFileWrapper(directHandle);
    require(g_fileCloseCalls == 1 &&
                g_lastClosedFile == directHandle,
            "closeFileWrapper forwards the original file handle to fileClose");

    resetDebriefState(comm, game);
    openShowPic("SCREEN.PIC", kFilePicPage);
    require(g_openFileCalls == 1 &&
                std::strcmp(g_lastOpenFileName, "SCREEN.PIC") == 0 &&
                g_lastOpenMode == 0 &&
                g_showPicCalls == 1 &&
                g_lastShowPicHandle == reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFilePicHandle)) &&
                g_lastShowPicPage == kFilePicPage &&
                g_fileCloseCalls == 1 &&
                g_lastClosedFile == g_lastShowPicHandle,
            "openShowPic preserves the original open-show-close picture chain");

    resetDebriefState(comm, game);
    loadPic("SPRITE.PIC", kFilePicSegment);
    require(g_openFileCalls == 1 &&
                std::strcmp(g_lastOpenFileName, "SPRITE.PIC") == 0 &&
                g_lastOpenMode == 0 &&
                g_decodePicCalls == 1 &&
                g_lastDecodePicHandle == reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeFilePicHandle)) &&
                g_lastDecodeSegment == kFilePicSegment &&
                g_fileCloseCalls == 1 &&
                g_lastClosedFile == g_lastDecodePicHandle,
            "loadPic preserves the original open-decode-close sprite chain");

    resetDebriefState(comm, game);
    std::memset(comm.worldBuf, 0, sizeof(comm.worldBuf));
    std::memcpy(comm.worldBuf + kWorldStringsOffset, "ALPHA\0BRAVO\0", 12);
    setupWorldBufPtr();
    require(worldBufCursor == comm.worldBuf,
            "setupWorldBufPtr points the END world cursor at COMM worldBuf");
    loadWorldStrings();
    require(worldDataReady == kWorldDataReady &&
                std::strcmp(worldStrings[0], "ALPHA") == 0 &&
                std::strcmp(worldStrings[1], "BRAVO") == 0,
            "loadWorldStrings reads original packed world strings from COMM");
    restoreVideoMode();
    restoreInterrupts();
    outportByte(kOutportFixturePort, kOutportFixtureValue);

    std::cout << "utility_behavior_tests passed\n";
    return 0;
}
