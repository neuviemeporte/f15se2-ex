#include "comm.h"
#include "endata.h"
#include "endbrf.h"
#include "endtypes.h"
#include "gfx.h"
#include "slot.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern uint8 animExitFlag;

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;
uint8 timerCounter = 0;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EndDebriefOriginalConstant : int {
    kTheaterIndex = 0,
    kOpenReadMode = 0,
    kSpriteBufferHandle = 0x2468,
    kTheaterFadeSteps = 9,
    kIconsFadeSteps = 8,
    kIconsPage = 1,
    kTitlePageColor = 0,
    kMenuPageColor = 6,
    kTitleX = 106,
    kTitleY = 1,
    kMenuX = 236,
    kMenuY0 = 150,
    kMenuY1 = 160,
    kMenuCount = 2,
    kMenuCursorX = 250,
    kMenuCursorY = 151,
    kSelectedExitItem = 1,
    kMenuActiveState = 2,
    kInitialRecordIndex = 0,
    kEjectedFlagSet = 1,
    kMissionScore = 1234,
    kExistingLastScore = 100,
    kExpectedLastScore = kMissionScore,
    kExistingTotalScore = 2000,
    kExpectedTotalScore = kExistingTotalScore + kMissionScore,
    kTrainingFlagOff = 0,
    kLandingCrashed = 1,
    kCrashCampaignProgress = 2,
    kExpectedOneCall = 1,
    kExpectedTwoCalls = 2,
    kExpectedThreeCalls = 3,
    kExpectedFourCalls = 4,
    kLandingKilled = 2,
    kBailoutDidNotSurvive = 0,
    kRankHighBeforeCampaignLoss = 2,
    kCampaignLossProgress = 1,
    kTrainingFlagOn = 1,
    kScenarioPromptY = 90,
    kRetryPromptY = 100,
    kInsertKeyCalls = 2,
    kSelectedAnimateItem = 0,
    kJoystickDebounceReads = 6,
    kJoystickDebounceYields = 12,
    kMaxRecordedDebriefCalls = 8,
    kExpectedNoCalls = 0,
    kTestFailureExitCode = 1,
};

int g_openCalls = 0;
int g_fileCloseCalls = 0;
int g_waitRetraceCalls = 0;
int g_setFadeStepsCalls = 0;
int g_allocSpriteBufCalls = 0;
int g_loadPicCalls = 0;
int g_openShowPicCalls = 0;
int g_clearRectCalls = 0;
int g_blitSpriteCalls = 0;
int g_drawStringAtCalls = 0;
int g_drawStringCenteredCalls = 0;
int g_commitPageCalls = 0;
int g_flipPageCalls = 0;
int g_setTimerCalls = 0;
int g_processMenuCalls = 0;
int g_selectMenuCalls = 0;
int g_animateCalls = 0;
int g_restoreTimerCalls = 0;
int g_calcScoreCalls = 0;
int g_freeSpriteBufCalls = 0;
int g_getKeyCalls = 0;
int g_timerYieldCalls = 0;
int g_miscReadJoystickCalls = 0;
const char *g_openNames[kMaxRecordedDebriefCalls] = {};
const char *g_loadPicName = nullptr;
const char *g_openShowPicName = nullptr;
const char *g_drawStrings[kMaxRecordedDebriefCalls] = {};
const char *g_centeredStrings[kMaxRecordedDebriefCalls] = {};
int g_centeredYs[kMaxRecordedDebriefCalls] = {};
int g_fadeSteps[kMaxRecordedDebriefCalls] = {};
int g_openModes[kMaxRecordedDebriefCalls] = {};
int g_loadPicSegment = 0;
int g_openShowPicPage = 0;
int g_lastClearRect[4] = {};
int g_blitBufPtrs[2] = {};
int g_drawXs[kMaxRecordedDebriefCalls] = {};
int g_drawYs[kMaxRecordedDebriefCalls] = {};
int g_processItemCount = 0;
int g_processCursorX = 0;
int g_processCursorY = 0;
int g_freedSpriteBuf = 0;
int g_openFailTheaterCount = 0;
int g_openFailIconsCount = 0;
int g_selectSequence[kMaxRecordedDebriefCalls] = {};
int g_selectSequenceLen = 1;
int g_joystickReadSequence[kMaxRecordedDebriefCalls] = {};
int g_joystickReadSequenceLen = 0;
int g_joystickReadIndex = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetDebriefState() {
    g_openCalls = 0;
    g_fileCloseCalls = 0;
    g_waitRetraceCalls = 0;
    g_setFadeStepsCalls = 0;
    g_allocSpriteBufCalls = 0;
    g_loadPicCalls = 0;
    g_openShowPicCalls = 0;
    g_clearRectCalls = 0;
    g_blitSpriteCalls = 0;
    g_drawStringAtCalls = 0;
    g_drawStringCenteredCalls = 0;
    g_commitPageCalls = 0;
    g_flipPageCalls = 0;
    g_setTimerCalls = 0;
    g_processMenuCalls = 0;
    g_selectMenuCalls = 0;
    g_animateCalls = 0;
    g_restoreTimerCalls = 0;
    g_calcScoreCalls = 0;
    g_freeSpriteBufCalls = 0;
    g_getKeyCalls = 0;
    g_timerYieldCalls = 0;
    g_miscReadJoystickCalls = 0;
    std::memset(g_openNames, 0, sizeof(g_openNames));
    std::memset(g_openModes, 0, sizeof(g_openModes));
    std::memset(g_fadeSteps, 0, sizeof(g_fadeSteps));
    std::memset(g_drawStrings, 0, sizeof(g_drawStrings));
    std::memset(g_centeredStrings, 0, sizeof(g_centeredStrings));
    std::memset(g_centeredYs, 0, sizeof(g_centeredYs));
    std::memset(g_drawXs, 0, sizeof(g_drawXs));
    std::memset(g_drawYs, 0, sizeof(g_drawYs));
    std::memset(g_blitBufPtrs, 0, sizeof(g_blitBufPtrs));
    g_loadPicName = nullptr;
    g_openShowPicName = nullptr;
    g_loadPicSegment = 0;
    g_openShowPicPage = 0;
    g_processItemCount = 0;
    g_processCursorX = 0;
    g_processCursorY = 0;
    g_freedSpriteBuf = 0;
    g_openFailTheaterCount = 0;
    g_openFailIconsCount = 0;
    std::memset(g_selectSequence, 0, sizeof(g_selectSequence));
    g_selectSequence[0] = kSelectedExitItem;
    g_selectSequenceLen = 1;
    std::memset(g_joystickReadSequence, 0, sizeof(g_joystickReadSequence));
    g_joystickReadSequenceLen = 0;
    g_joystickReadIndex = 0;
    animExitFlag = 0;
    timerCounter = 0;
    curRecordIdx = 99;
    ejectedFlag = 0;
    missionScore = 0;
    debriefMenuItems[0].state = 0;
    debriefMenuItems[1].state = 0;
}

} // namespace

SDL_IOStream *openFile(const char *filename, int mode) {
    static SDL_IOStream *const kTheaterHandle = reinterpret_cast<SDL_IOStream *>(0x1000);
    static SDL_IOStream *const kIconsHandle = reinterpret_cast<SDL_IOStream *>(0x2000);
    require(g_openCalls < kMaxRecordedDebriefCalls,
            "debriefMainLoop open-call recorder capacity");
    g_openNames[g_openCalls] = filename;
    g_openModes[g_openCalls] = mode;
    ++g_openCalls;
    if (std::strcmp(filename, theaterSprFiles[gameData->theater]) == 0) {
        if (g_openFailTheaterCount > 0) {
            --g_openFailTheaterCount;
            return nullptr;
        }
        return kTheaterHandle;
    }
    if (std::strcmp(filename, "dbicons.spr") == 0) {
        if (g_openFailIconsCount > 0) {
            --g_openFailIconsCount;
            return nullptr;
        }
        return kIconsHandle;
    }
    return nullptr;
}

void fileClose(SDL_IOStream *) {
    ++g_fileCloseCalls;
}

void FAR CDECL gfx_waitRetrace(void) {
    ++g_waitRetraceCalls;
}

void FAR CDECL gfx_setFadeSteps(int steps) {
    require(g_setFadeStepsCalls < kMaxRecordedDebriefCalls,
            "debriefMainLoop fade-step recorder capacity");
    g_fadeSteps[g_setFadeStepsCalls++] = steps;
}

int FAR CDECL gfx_allocSpriteBuf(void) {
    ++g_allocSpriteBufCalls;
    return kSpriteBufferHandle;
}

void loadPic(const char *filename, int segment) {
    ++g_loadPicCalls;
    g_loadPicName = filename;
    g_loadPicSegment = segment;
}

void openShowPic(const char *filename, int page) {
    ++g_openShowPicCalls;
    g_openShowPicName = filename;
    g_openShowPicPage = page;
}

void clearRect(int16 *, int y1, int x1, int x2, int y2) {
    ++g_clearRectCalls;
    g_lastClearRect[0] = y1;
    g_lastClearRect[1] = x1;
    g_lastClearRect[2] = x2;
    g_lastClearRect[3] = y2;
}

int FAR CDECL gfx_blitSprite(struct SpriteParams *spritePtr) {
    require(g_blitSpriteCalls < kExpectedTwoCalls,
            "debriefMainLoop should blit the map and status bar sprites");
    g_blitBufPtrs[g_blitSpriteCalls++] = spritePtr->bufPtr;
    return 0;
}

void drawStringAt(int16 *, const char *string, int x, int y) {
    require(g_drawStringAtCalls < kMaxRecordedDebriefCalls,
            "debriefMainLoop drawStringAt recorder capacity");
    g_drawStrings[g_drawStringAtCalls] = string;
    g_drawXs[g_drawStringAtCalls] = x;
    g_drawYs[g_drawStringAtCalls] = y;
    ++g_drawStringAtCalls;
}

void drawStringCentered(int16 *, const char *string, int, int y, int) {
    require(g_drawStringCenteredCalls < kMaxRecordedDebriefCalls,
            "debriefMainLoop drawStringCentered recorder capacity");
    g_centeredStrings[g_drawStringCenteredCalls] = string;
    g_centeredYs[g_drawStringCenteredCalls] = y;
    ++g_drawStringCenteredCalls;
}

void FAR CDECL gfx_commitPage(void) {
    ++g_commitPageCalls;
}

void FAR CDECL gfx_flipPage(void) {
    ++g_flipPageCalls;
}

void setTimerIrqHandler(void) {
    ++g_setTimerCalls;
}

void processMenuItems(MenuItem *, int, int itemCount, int cursorStartX, int cursorStartY, int16 *) {
    ++g_processMenuCalls;
    g_processItemCount = itemCount;
    g_processCursorX = cursorStartX;
    g_processCursorY = cursorStartY;
}

int selectMenuItem(MenuItem *, int, int, int16 *, int16 *) {
    ++g_selectMenuCalls;
    int idx = g_selectMenuCalls - 1;
    if (idx < g_selectSequenceLen) return g_selectSequence[idx];
    return kSelectedExitItem;
}

void animateFlightPath(int16 *) {
    ++g_animateCalls;
}

int FAR CDECL misc_readJoystick(int16) {
    ++g_miscReadJoystickCalls;
    if (g_joystickReadIndex < g_joystickReadSequenceLen) {
        return g_joystickReadSequence[g_joystickReadIndex++];
    }
    return 0;
}

void restoreTimerIrqHandler(void) {
    ++g_restoreTimerCalls;
}

long calcMissionScore(int) {
    ++g_calcScoreCalls;
    return kMissionScore;
}

void FAR CDECL gfx_freeSpriteBuf(int segment) {
    ++g_freeSpriteBufCalls;
    g_freedSpriteBuf = segment;
}

int FAR CDECL misc_getKey(void) {
    ++g_getKeyCalls;
    return 0;
}

void timerYield(void) {
    ++g_timerYieldCalls;
    ++timerCounter;
}

int main() {
    struct GameComm comm = {};
    struct Game game = {};
    int16 pageA[8] = {};
    int16 pageB[8] = {};
    int16 cursor[4] = {};

    resetDebriefState();
    comm.setupUseJoy = 0;
    comm.trainingFlag = kTrainingFlagOff;
    comm.landingType = kLandingCrashed;
    game.theater = kTheaterIndex;
    game.lastScore = kExistingLastScore;
    game.totalScore = kExistingTotalScore;
    commData = &comm;
    gameData = &game;
    debriefPage = pageA;
    debriefPage2 = pageB;
    cursorBoundsPtr = cursor;

    debriefMainLoop();

    require(g_openCalls == kExpectedTwoCalls &&
                std::strcmp(g_openNames[0], theaterSprFiles[kTheaterIndex]) == 0 &&
                std::strcmp(g_openNames[1], "dbicons.spr") == 0 &&
                g_openModes[0] == kOpenReadMode &&
                g_openModes[1] == kOpenReadMode &&
                g_fileCloseCalls == kExpectedTwoCalls,
            "debriefMainLoop opens and closes the original theater and icon resources");
    require(g_waitRetraceCalls == kExpectedFourCalls &&
                g_setFadeStepsCalls == kExpectedTwoCalls &&
                g_fadeSteps[0] == kTheaterFadeSteps &&
                g_fadeSteps[1] == kIconsFadeSteps,
            "debriefMainLoop preserves the original wait/fade sequence");
    require(g_allocSpriteBufCalls == kExpectedOneCall &&
                g_loadPicCalls == kExpectedOneCall &&
                std::strcmp(g_loadPicName, theaterSprFiles[kTheaterIndex]) == 0 &&
                g_loadPicSegment == kSpriteBufferHandle,
            "debriefMainLoop loads the theater sprites into the allocated sprite buffer");
    require(g_openShowPicCalls == kExpectedOneCall &&
                std::strcmp(g_openShowPicName, "dbicons.spr") == 0 &&
                g_openShowPicPage == kIconsPage,
            "debriefMainLoop opens the original DB icon PIC on page 1");

    require(spriteMapAreaDef.bufPtr == kSpriteBufferHandle &&
                spriteStatusBarDef.bufPtr == kSpriteBufferHandle &&
                spriteAirDef.bufPtr == kSpriteBufferHandle &&
                spriteAirBlinkDef.bufPtr == kSpriteBufferHandle &&
                spriteSamDef.bufPtr == kSpriteBufferHandle &&
                spriteSamBlinkDef.bufPtr == kSpriteBufferHandle &&
                spriteGroundDef.bufPtr == kSpriteBufferHandle &&
                spriteGroundBlinkDef.bufPtr == kSpriteBufferHandle &&
                spriteBombDef.bufPtr == kSpriteBufferHandle &&
                spriteBombBlinkDef.bufPtr == kSpriteBufferHandle &&
                spriteWaypointDef.bufPtr == kSpriteBufferHandle &&
                spriteWaypointBlinkDef.bufPtr == kSpriteBufferHandle,
            "debriefMainLoop propagates the original theater sprite buffer to every debrief sprite");
    require(g_clearRectCalls == kExpectedOneCall &&
                g_lastClearRect[0] == 0 &&
                g_lastClearRect[1] == 0 &&
                g_lastClearRect[2] == 319 &&
                g_lastClearRect[3] == 199 &&
                g_blitSpriteCalls == kExpectedTwoCalls &&
                g_blitBufPtrs[0] == kSpriteBufferHandle &&
                g_blitBufPtrs[1] == kSpriteBufferHandle,
            "debriefMainLoop clears the debrief page and blits the original map/status sprites");
    require(debriefPage[2] == kMenuPageColor &&
                g_drawStringAtCalls == kExpectedThreeCalls &&
                std::strcmp(g_drawStrings[0], "  MISSION DEBRIEFING") == 0 &&
                g_drawXs[0] == kTitleX &&
                g_drawYs[0] == kTitleY &&
                g_drawStrings[1] == debriefMenuStrings[0] &&
                g_drawStrings[2] == debriefMenuStrings[1] &&
                g_drawXs[1] == kMenuX &&
                g_drawXs[2] == kMenuX &&
                g_drawYs[1] == kMenuY0 &&
                g_drawYs[2] == kMenuY1,
            "debriefMainLoop draws the original title and two menu labels");
    require(g_commitPageCalls == kExpectedOneCall &&
                g_flipPageCalls == kExpectedOneCall &&
                g_setTimerCalls == kExpectedOneCall &&
                g_processMenuCalls == kExpectedOneCall &&
                g_processItemCount == kMenuCount &&
                g_processCursorX == kMenuCursorX &&
                g_processCursorY == kMenuCursorY &&
                debriefMenuItems[0].state == kMenuActiveState &&
                g_selectMenuCalls == kExpectedOneCall &&
                g_animateCalls == kExpectedNoCalls,
            "debriefMainLoop enters the original menu loop and exits immediately when exit is selected");
    require(g_restoreTimerCalls == kExpectedOneCall &&
                g_calcScoreCalls == kExpectedOneCall &&
                g_freeSpriteBufCalls == kExpectedOneCall &&
                g_freedSpriteBuf == kSpriteBufferHandle,
            "debriefMainLoop restores timer, scores mission, and frees the sprite buffer");
    require(ejectedFlag == kEjectedFlagSet &&
                curRecordIdx == kInitialRecordIndex &&
                missionScore == kMissionScore &&
                game.hallOfFameEligible == kMissionScore &&
                game.lastScore == kExpectedLastScore &&
                game.totalScore == kExpectedTotalScore &&
                game.campaignProgress == kCrashCampaignProgress,
            "debriefMainLoop preserves original post-mission score and campaign updates");

    resetDebriefState();
    comm = {};
    game = {};
    comm.trainingFlag = kTrainingFlagOff;
    comm.landingType = kLandingCrashed;
    game.theater = kTheaterIndex;
    game.lastScore = kExistingLastScore;
    game.totalScore = kExistingTotalScore;
    g_openFailTheaterCount = 1;
    g_openFailIconsCount = 1;
    debriefMainLoop();
    require(g_openCalls == kExpectedFourCalls &&
                g_getKeyCalls == kInsertKeyCalls &&
                g_drawStringCenteredCalls == kExpectedFourCalls &&
                std::strcmp(g_centeredStrings[0], "Please insert scenario disk") == 0 &&
                std::strcmp(g_centeredStrings[1], "<Press a key when ready>") == 0 &&
                std::strcmp(g_centeredStrings[2], "Please insert F15 Disk A") == 0 &&
                std::strcmp(g_centeredStrings[3], "<Press a key when ready>") == 0 &&
                g_centeredYs[0] == kScenarioPromptY &&
                g_centeredYs[1] == kRetryPromptY &&
                g_centeredYs[2] == kScenarioPromptY &&
                g_centeredYs[3] == kRetryPromptY,
            "debriefMainLoop preserves original scenario and Disk A retry prompts");

    resetDebriefState();
    comm = {};
    game = {};
    comm.setupUseJoy = 1;
    comm.trainingFlag = kTrainingFlagOff;
    comm.landingType = kLandingCrashed;
    game.theater = kTheaterIndex;
    game.lastScore = kExistingLastScore;
    game.totalScore = kExistingTotalScore;
    g_selectSequence[0] = kSelectedAnimateItem;
    g_selectSequence[1] = kSelectedExitItem;
    g_selectSequenceLen = 2;
    animExitFlag = 1;
    g_joystickReadSequence[0] = 1;
    g_joystickReadSequence[1] = 0;
    g_joystickReadSequence[2] = 1;
    g_joystickReadSequence[3] = 0;
    g_joystickReadSequenceLen = 4; /* One pressed/released pair before and after the first debounce delay. */
    debriefMainLoop();
    require(g_animateCalls == kExpectedOneCall &&
                g_processMenuCalls == kExpectedTwoCalls &&
                g_selectMenuCalls == kExpectedTwoCalls,
            "debriefMainLoop preserves original animate-menu path before exit");
    require(g_miscReadJoystickCalls == kJoystickDebounceReads &&
                g_timerYieldCalls == kJoystickDebounceYields,
            "debriefMainLoop preserves original joystick debounce waits");

    resetDebriefState();
    comm = {};
    game = {};
    comm.trainingFlag = kTrainingFlagOn;
    comm.landingType = kLandingCrashed;
    game.theater = kTheaterIndex;
    game.lastScore = kExistingLastScore;
    game.totalScore = kExistingTotalScore;
    debriefMainLoop();
    require(game.hallOfFameEligible == 0 &&
                game.lastScore == kExistingLastScore &&
                game.totalScore == kExistingTotalScore &&
                game.campaignProgress == 0,
            "debriefMainLoop preserves original training-mode no-score update path");

    resetDebriefState();
    comm = {};
    game = {};
    comm.trainingFlag = kTrainingFlagOff;
    comm.landingType = kLandingKilled;
    comm.bailoutSurvived = kBailoutDidNotSurvive;
    game.theater = kTheaterIndex;
    game.lastScore = kExistingLastScore;
    game.totalScore = kExistingTotalScore;
    game.rankHigh = kRankHighBeforeCampaignLoss;
    debriefMainLoop();
    require(game.rankHigh == kRankHighBeforeCampaignLoss + 1 &&
                game.campaignProgress == kCampaignLossProgress,
            "debriefMainLoop preserves original killed-without-bailout campaign loss update");

    // This literal names the old transient title color; final page color is asserted above.
    (void)kTitlePageColor;

    std::cout << "end_debrief_behavior_tests passed\n";
    return 0;
}
