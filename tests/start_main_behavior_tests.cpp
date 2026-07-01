#include "comm.h"
#include "const.h"
#include "gfx.h"
#include "inttype.h"
#include "pointers.h"
#include "slot.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

int start_main(void);

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;
uint8 joyAxes[8] = {};
uint8 joyReady[4] = {};
uint16 menuSprites = 0;
int16 exitCode = 0;
int16 missionPick = -1;
int16 missionPickPad = -1;
int16 g_pageDesc[4] = {};
int16 *bufPtr = g_pageDesc;
uint8 timerCounter = 0;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum StartMainConstant : int {
    kInitialGfxInitResult = 0x3456,
    kGfxBufferPage = 2,
    kNoSoundSampleSegment = 0,
    kNoSoundVariant = 0,
    kNoSplash = 0,
    kNeedJoystickSetup = 1,
    kNoJoystickSetup = 0,
    kTrainingCleared = 0,
    kTrainingSet = 1,
    kNormalMissionReady = 1,
    kTrainingMissionReady = 2,
    kCampaignMission = 1,
    kCampaignReset = 0,
    kCampaignAlreadyInProgress = 1,
    kRepeatMissionSelected = 1,
    kRepeatMissionDeclined = 0,
    kMissionPickRepeatSentinel = -1,
    kExpectedFadeSteps = 8,
    kIntroLabsFadeSteps = 5,
    kIntroAdvertFadeSteps = 15,
    kIntroTitleFadeSteps = 1,
    kIntroLowResDac = 4,
    kIntroFinalLowResDac = 0,
    kIntroFinalHiResDac = 2,
    kIntroExpectedSkipAndTitleKeyReads = 2,
    kIntroTimerStepPastTimeout = 200,
    kIntroKeyNeverPressed = -1,
    kIntroKeyPressedFirstPoll = 1,
    kIntroKeyPressedAdvertPoll = 2,
    kGfxGetValMcga = 0,
    kGfxGetValLoadPic = 1,
    kSpritePage = 2,
    kSpriteHandle = 0x2222,
    kIntroPicPage = 0,
    kIntroExpectedPicCalls = 4,
    kJoyAxis0 = 0,
    kJoyAxis1 = 1,
    kJoyDataBytes = 20,
    kJoyReady = 1,
    kStartExitCode = 12,
    kClearX1 = 0,
    kClearY1 = 0,
    kScreenMaxX = 319, // sttypes.h SCREEN_MAXX: last visible 320-wide column.
    kScreenMaxY = 199, // sttypes.h SCREEN_MAXY: last visible 200-high row.
    kInitialTheaterLibya = 0,
    kInitialTheaterKuril = 3,
    kInitialTheaterOther = 4,
    kInitialDifficultyGreen = 0,
    kInitialDifficultyMax = 3,
    kAdvancedTheaterWrapped = 0,
    kAdvancedDifficulty = 1,
    kPreservedRandomSeed = 0x1357,
    kRandSeed = 1234,
    kFirstCRandAfterSeedWord = 8718, // int16 storage of glibc rand() after srand(1234).
    kExpectedOneCall = 1,
    kTestFailureExitCode = 1,
};

int g_installCalls = 0;
int g_storeBufPtrCalls = 0;
int g_storeBufPtrSegment = -1;
int g_storeBufPtrPage = -1;
int g_initGraphicsCalls = 0;
int g_audioShutdownCalls = 0;
int g_audioSetupCalls = 0;
int g_audioSetupSegment = -1;
int g_audioSetupVariant = -1;
int g_clearKeybufCalls = 0;
int g_copyJoystickCalls = 0;
uint8 g_copiedJoyData[kJoyDataBytes] = {};
int g_allocSpriteCalls = 0;
int g_pilotSelectCalls = 0;
int g_pilotSelectNeedSplash = -1;
int g_missionSelectCalls = 0;
int g_askRepeatCalls = 0;
int g_askRepeatResult = kRepeatMissionDeclined;
int g_missionDecodeCalls = 0;
int g_missionGenerateCalls = 0;
int g_printMissionCalls = 0;
int g_checkDiskCalls = 0;
int g_restoreCalls = 0;
int g_fadeSteps = -1;
int g_fadeStepHistory[8] = {};
int g_fadeStepCalls = 0;
int g_gfxGetValResult = kGfxGetValMcga;
int g_openShowPicCalls = 0;
int g_openShowPicPage = -1;
std::string g_openShowPicName;
std::string g_openShowPicNames[8];
int g_openShowPicPages[8] = {};
int g_loadPicCalls = 0;
int g_loadPicSegment = -1;
std::string g_loadPicName;
int g_exportWorldCalls = 0;
std::string g_exportWorldName;
int g_clearKeyFlagsCalls = 0;
int g_clearRectCalls = 0;
int g_clearX1 = -1;
int g_clearY1 = -1;
int g_clearX2 = -1;
int g_clearY2 = -1;
int g_introUnexpectedCalls = 0;
int g_commitPageCalls = 0;
int g_setTimerCalls = 0;
int g_checkKeyCalls = 0;
int g_checkKeyTimerStep = 0;
int g_checkKeyReturnZeroOnCall = kIntroKeyNeverPressed;
int g_getKeyCalls = 0;
int g_waitRetraceCalls = 0;
int g_flipPageCalls = 0;
int g_videoHiResCalls = 0;
bool g_videoHiResResult = false;
int g_showPic640Calls = 0;
std::string g_showPic640Name;
int g_setDacCalls = 0;
int g_lastDac = -1;
int g_waitMdaCalls = 0;
int g_waitMdaIter = -1;
int g_audioIntroCalls = 0;
int g_restoreTimerCalls = 0;
int g_setMode13Calls = 0;
int g_missionReadyAfterGenerate = kNormalMissionReady;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetStartMainState(struct GameComm &comm, struct Game &game) {
    std::memset(&comm, 0, sizeof(comm));
    std::memset(&game, 0, sizeof(game));
    std::memset(joyAxes, 0, sizeof(joyAxes));
    std::memset(joyReady, 0, sizeof(joyReady));
    std::memset(g_copiedJoyData, 0, sizeof(g_copiedJoyData));
    for (int i = 0; i < kJoyDataBytes; ++i) {
        comm.joyData[i] = static_cast<uint8>(0xa0 + i);
    }
    menuSprites = 0;
    exitCode = 0;
    timerCounter = 0;
    comm.gfxInitResult = kInitialGfxInitResult;
    comm.needSplash = kNoSplash;
    comm.trainingFlag = 1;
    commData = &comm;
    gameData = &game;
    g_installCalls = 0;
    g_storeBufPtrCalls = 0;
    g_storeBufPtrSegment = -1;
    g_storeBufPtrPage = -1;
    g_initGraphicsCalls = 0;
    g_audioShutdownCalls = 0;
    g_audioSetupCalls = 0;
    g_audioSetupSegment = -1;
    g_audioSetupVariant = -1;
    g_clearKeybufCalls = 0;
    g_copyJoystickCalls = 0;
    g_allocSpriteCalls = 0;
    g_pilotSelectCalls = 0;
    g_pilotSelectNeedSplash = -1;
    g_missionSelectCalls = 0;
    g_askRepeatCalls = 0;
    g_askRepeatResult = kRepeatMissionDeclined;
    g_missionDecodeCalls = 0;
    g_missionGenerateCalls = 0;
    g_printMissionCalls = 0;
    g_checkDiskCalls = 0;
    g_restoreCalls = 0;
    g_fadeSteps = -1;
    std::memset(g_fadeStepHistory, 0, sizeof(g_fadeStepHistory));
    g_fadeStepCalls = 0;
    g_gfxGetValResult = kGfxGetValMcga;
    g_openShowPicCalls = 0;
    g_openShowPicPage = -1;
    g_openShowPicName.clear();
    for (std::string &name : g_openShowPicNames) {
        name.clear();
    }
    std::memset(g_openShowPicPages, 0, sizeof(g_openShowPicPages));
    g_loadPicCalls = 0;
    g_loadPicSegment = -1;
    g_loadPicName.clear();
    g_exportWorldCalls = 0;
    g_exportWorldName.clear();
    g_clearKeyFlagsCalls = 0;
    g_clearRectCalls = 0;
    g_clearX1 = g_clearY1 = g_clearX2 = g_clearY2 = -1;
    g_introUnexpectedCalls = 0;
    g_commitPageCalls = 0;
    g_setTimerCalls = 0;
    g_checkKeyCalls = 0;
    g_checkKeyTimerStep = 0;
    g_checkKeyReturnZeroOnCall = kIntroKeyNeverPressed;
    g_getKeyCalls = 0;
    g_waitRetraceCalls = 0;
    g_flipPageCalls = 0;
    g_videoHiResCalls = 0;
    g_videoHiResResult = false;
    g_showPic640Calls = 0;
    g_showPic640Name.clear();
    g_setDacCalls = 0;
    g_lastDac = -1;
    g_waitMdaCalls = 0;
    g_waitMdaIter = -1;
    g_audioIntroCalls = 0;
    g_restoreTimerCalls = 0;
    g_setMode13Calls = 0;
    g_missionReadyAfterGenerate = kNormalMissionReady;
}

} // namespace

void installCBreakHandler(void) { ++g_installCalls; }

void FAR CDECL gfx_storeBufPtr(uint16 seg, int pageIdx) {
    ++g_storeBufPtrCalls;
    g_storeBufPtrSegment = seg;
    g_storeBufPtrPage = pageIdx;
}

void initGraphics(void) { ++g_initGraphicsCalls; }

int FAR CDECL audio_shutdown(void) {
    ++g_audioShutdownCalls;
    return 0;
}

int FAR CDECL audio_setup(int16 sampleDataSeg, int16 variantSel) {
    ++g_audioSetupCalls;
    g_audioSetupSegment = sampleDataSeg;
    g_audioSetupVariant = variantSel;
    return 0;
}

void clearKeybuf(void) { ++g_clearKeybufCalls; }

void FAR copyJoystickData(uint8 FAR *ptr) {
    ++g_copyJoystickCalls;
    std::memcpy(g_copiedJoyData, ptr, sizeof(g_copiedJoyData));
}

int gfx_allocSpriteBuf(void) {
    ++g_allocSpriteCalls;
    return kSpriteHandle;
}

void pilotSelect(int16 needSplash) {
    ++g_pilotSelectCalls;
    g_pilotSelectNeedSplash = needSplash;
}

void missionSelect(void) {
    ++g_missionSelectCalls;
    missionPick = kMissionPickRepeatSentinel;
}

int askRepeatMission(void) {
    ++g_askRepeatCalls;
    return g_askRepeatResult;
}

void missionDecode(void) { ++g_missionDecodeCalls; }

void missionGenerate(void) {
    ++g_missionGenerateCalls;
    gameData->missionReady = g_missionReadyAfterGenerate;
}

void printMission(void) { ++g_printMissionCalls; }
void checkDiskA(void) { ++g_checkDiskCalls; }
void restoreCbreakHandler(void) { ++g_restoreCalls; }

void FAR CDECL gfx_setFadeSteps(int steps) {
    g_fadeSteps = steps;
    if (g_fadeStepCalls < static_cast<int>(sizeof(g_fadeStepHistory) / sizeof(g_fadeStepHistory[0]))) {
        g_fadeStepHistory[g_fadeStepCalls] = steps;
    }
    ++g_fadeStepCalls;
}
int FAR CDECL gfx_getVal(void) { return g_gfxGetValResult; }

void openShowPic(const char *filename, int page) {
    ++g_openShowPicCalls;
    g_openShowPicName = filename ? filename : "";
    g_openShowPicPage = page;
    if (g_openShowPicCalls <= static_cast<int>(sizeof(g_openShowPicPages) / sizeof(g_openShowPicPages[0]))) {
        const int slot = g_openShowPicCalls - 1;
        g_openShowPicNames[slot] = filename ? filename : "";
        g_openShowPicPages[slot] = page;
    }
}

void loadPic(const char *filename, int segment) {
    ++g_loadPicCalls;
    g_loadPicName = filename ? filename : "";
    g_loadPicSegment = segment;
}

void exportWorldToComm(const char *filename) {
    ++g_exportWorldCalls;
    g_exportWorldName = filename ? filename : "";
}

void FAR CDECL misc_clearKeyFlags(void) { ++g_clearKeyFlagsCalls; }

void clearRect(int16 *, int x1, int y1, int x2, int y2) {
    ++g_clearRectCalls;
    g_clearX1 = x1;
    g_clearY1 = y1;
    g_clearX2 = x2;
    g_clearY2 = y2;
}

void FAR CDECL gfx_commitPage(void) {
    ++g_introUnexpectedCalls;
    ++g_commitPageCalls;
}
void setTimerIrqHandler(void) {
    ++g_introUnexpectedCalls;
    ++g_setTimerCalls;
}
int FAR CDECL misc_checkKeyBuf(void) {
    ++g_introUnexpectedCalls;
    ++g_checkKeyCalls;
    timerCounter = static_cast<uint8>(timerCounter + g_checkKeyTimerStep);
    return (g_checkKeyCalls == g_checkKeyReturnZeroOnCall) ? 0 : 1;
}
int FAR CDECL misc_getKey(void) {
    ++g_introUnexpectedCalls;
    ++g_getKeyCalls;
    return 0;
}
void FAR CDECL gfx_waitRetrace(void) {
    ++g_introUnexpectedCalls;
    ++g_waitRetraceCalls;
}
void FAR CDECL gfx_flipPage(void) {
    ++g_introUnexpectedCalls;
    ++g_flipPageCalls;
}
bool video_setHiRes(void) {
    ++g_introUnexpectedCalls;
    ++g_videoHiResCalls;
    return g_videoHiResResult;
}
void showPic640(const char *filename) {
    ++g_introUnexpectedCalls;
    ++g_showPic640Calls;
    g_showPic640Name = filename ? filename : "";
}
void FAR CDECL gfx_setDac(uint16 dac) {
    ++g_introUnexpectedCalls;
    ++g_setDacCalls;
    g_lastDac = dac;
}
void waitMdaCgaStatus(int16 iter) {
    ++g_introUnexpectedCalls;
    ++g_waitMdaCalls;
    g_waitMdaIter = iter;
}
int FAR CDECL audio_playIntro(void) {
    ++g_introUnexpectedCalls;
    ++g_audioIntroCalls;
    return 0;
}
void restoreTimerIrqHandler(void) {
    ++g_introUnexpectedCalls;
    ++g_restoreTimerCalls;
}
void FAR CDECL gfx_setMode13(void) {
    ++g_introUnexpectedCalls;
    ++g_setMode13Calls;
}

int main() {
    struct GameComm comm = {};
    struct Game game = {};
    resetStartMainState(comm, game);
    comm.setupUseJoy = kNeedJoystickSetup;
    comm.trainingFlag = kTrainingCleared;
    game.theater = kInitialTheaterLibya;
    game.difficulty = kInitialDifficultyGreen;
    game.campaignProgress = kCampaignAlreadyInProgress;
    game.rand = kPreservedRandomSeed;
    g_askRepeatResult = kRepeatMissionSelected;

    const int result = start_main();

    require(result == kStartExitCode,
            "normal start_main should return the menu exit code");
    require(g_introUnexpectedCalls == 0,
            "needSplash clear skips the original intro/title sequence");
    require(g_installCalls == kExpectedOneCall &&
                g_storeBufPtrCalls == kExpectedOneCall &&
                g_storeBufPtrSegment == kInitialGfxInitResult &&
                g_storeBufPtrPage == kGfxBufferPage &&
                g_initGraphicsCalls == kExpectedOneCall,
            "normal start_main performs the original common setup");
    require(g_audioShutdownCalls == kExpectedOneCall &&
                g_audioSetupCalls == kExpectedOneCall &&
                g_audioSetupSegment == kNoSoundSampleSegment &&
                g_audioSetupVariant == kNoSoundVariant,
            "normal start_main resets audio to the original no-sound setup");
    require(game.theater == kInitialTheaterLibya &&
                game.difficulty == kInitialDifficultyGreen,
            "normal start_main leaves theater and difficulty unchanged while campaign progress is active");
    require(g_clearKeybufCalls == kExpectedOneCall &&
                g_copyJoystickCalls == kExpectedOneCall &&
                std::memcmp(g_copiedJoyData, comm.joyData, kJoyDataBytes) == 0 &&
                joyReady[0] == kJoyReady,
            "normal start_main clears keys, copies joystick calibration, and marks joystick ready");
    require(g_allocSpriteCalls == kExpectedOneCall &&
                menuSprites == kSpriteHandle &&
                g_pilotSelectCalls == kExpectedOneCall &&
                g_pilotSelectNeedSplash == kNoSplash &&
                g_missionSelectCalls == kExpectedOneCall,
            "normal start_main allocates menu sprites and runs pilot/mission selection");
    require(game.missionReady == kNormalMissionReady &&
                game.isCampaignMission == kCampaignMission &&
                game.campaignProgress == kCampaignReset,
            "normal start_main sets the original mission-ready campaign flags");
    require(g_askRepeatCalls == kExpectedOneCall &&
                game.rand == kPreservedRandomSeed,
            "normal start_main preserves the previous random seed when repeat mission is selected");
    require(g_missionDecodeCalls == kExpectedOneCall &&
                g_missionGenerateCalls == kExpectedOneCall &&
                g_printMissionCalls == kExpectedOneCall &&
                g_checkDiskCalls == kExpectedOneCall,
            "normal start_main decodes, generates, briefs, and checks disk in original order surface");
    require(g_restoreCalls == kExpectedOneCall &&
                comm.needSplash == 0 &&
                g_fadeSteps == kExpectedFadeSteps,
            "normal start_main restores break handling and prepares original sprite fade");
    require(g_openShowPicCalls == kExpectedOneCall &&
                g_openShowPicName == "f15.spr" &&
                g_openShowPicPage == kSpritePage &&
                g_loadPicCalls == 0,
            "normal MCGA path loads f15.spr through openShowPic page 2");
    require(g_exportWorldCalls == kExpectedOneCall &&
                g_exportWorldName == "temp.wld" &&
                comm.trainingFlag == kTrainingCleared,
            "normal start_main exports temp.wld and clears restart/training flags for a normal mission");
    require(g_clearKeyFlagsCalls == kExpectedOneCall &&
                g_clearRectCalls == kExpectedOneCall &&
                g_clearX1 == kClearX1 &&
                g_clearY1 == kClearY1 &&
                g_clearX2 == kScreenMaxX &&
                g_clearY2 == kScreenMaxY,
            "normal start_main clears key flags and the full START page before exit");

    resetStartMainState(comm, game);
    comm.setupUseJoy = kNoJoystickSetup;
    comm.trainingFlag = kTrainingCleared;
    game.theater = kInitialTheaterKuril;
    game.difficulty = kInitialDifficultyGreen;
    game.campaignProgress = kCampaignReset;
    g_gfxGetValResult = kGfxGetValLoadPic;
    g_missionReadyAfterGenerate = kTrainingMissionReady;
    std::srand(kRandSeed);
    require(start_main() == kStartExitCode,
            "normal start_main should return the menu exit code in loadPic mode");
    require(joyAxes[kJoyAxis0] == JOY_CENTER &&
                joyAxes[kJoyAxis1] == JOY_CENTER &&
                g_copyJoystickCalls == 0,
            "normal start_main centers joystick axes when setupUseJoy is clear");
    require(game.theater == kAdvancedTheaterWrapped &&
                game.difficulty == kAdvancedDifficulty,
            "normal start_main advances campaign theater and difficulty with original wrap behavior");
    require(g_askRepeatCalls == 0 &&
                game.rand == kFirstCRandAfterSeedWord,
            "normal start_main draws a fresh C rand seed when repeat-mission conditions are not met");
    require(g_openShowPicCalls == 0 &&
                g_loadPicCalls == kExpectedOneCall &&
                g_loadPicName == "f15.spr" &&
                g_loadPicSegment == kInitialGfxInitResult,
            "normal non-MCGA path loads f15.spr through loadPic with the graphics buffer");
    require(comm.trainingFlag == kTrainingSet &&
                1,
            "normal start_main exports the original training flag when missionReady exceeds one");

    resetStartMainState(comm, game);
    comm.needSplash = 1;
    comm.setupUseJoy = kNoJoystickSetup;
    comm.trainingFlag = kTrainingCleared;
    g_checkKeyTimerStep = kIntroTimerStepPastTimeout;
    g_videoHiResResult = false;
    require(start_main() == kStartExitCode,
            "splash start_main should return the menu exit code after the intro path");
    require(g_introUnexpectedCalls > 0 &&
                g_commitPageCalls == 3 &&
                g_setTimerCalls == kExpectedOneCall &&
                g_restoreTimerCalls == kExpectedOneCall,
            "splash start_main brackets the original intro/title sequence with timer setup and restore");
    require(g_fadeStepCalls == 4 &&
                g_fadeStepHistory[0] == kIntroLabsFadeSteps &&
                g_fadeStepHistory[1] == kIntroAdvertFadeSteps &&
                g_fadeStepHistory[2] == kIntroTitleFadeSteps &&
                g_fadeStepHistory[3] == kExpectedFadeSteps,
            "splash start_main applies the original labs, advert, title, and sprite fade steps");
    require(g_openShowPicCalls == kIntroExpectedPicCalls &&
                g_openShowPicNames[0] == "labs.pic" &&
                g_openShowPicPages[0] == kIntroPicPage &&
                g_openShowPicNames[1] == "adv.pic" &&
                g_openShowPicNames[2] == "title16.pic" &&
                g_openShowPicNames[3] == "f15.spr" &&
                g_openShowPicPages[3] == kSpritePage,
            "splash start_main displays the original labs, advert, title, and sprite pictures");
    require(g_checkKeyCalls == 3 &&
                g_getKeyCalls == kExpectedOneCall &&
                g_waitRetraceCalls == 2 &&
                g_flipPageCalls == kExpectedOneCall,
            "splash start_main preserves the original timeout polling and page-flip cadence");
    require(g_videoHiResCalls == kExpectedOneCall &&
                g_showPic640Calls == 0 &&
                g_setDacCalls == 2 &&
                g_lastDac == kIntroFinalLowResDac &&
                g_waitMdaCalls == kExpectedOneCall &&
                g_waitMdaIter == kIntroLowResDac &&
                g_audioIntroCalls == kExpectedOneCall &&
                g_setMode13Calls == kExpectedOneCall,
            "splash start_main preserves the original low-res title DAC/audio/mode restore path");

    resetStartMainState(comm, game);
    comm.needSplash = 1;
    comm.setupUseJoy = kNoJoystickSetup;
    comm.trainingFlag = kTrainingCleared;
    g_checkKeyReturnZeroOnCall = kIntroKeyPressedFirstPoll;
    g_videoHiResResult = true;
    require(start_main() == kStartExitCode,
            "splash start_main should return the menu exit code when labs is skipped by key");
    require(g_checkKeyCalls == kExpectedOneCall &&
                g_getKeyCalls == kIntroExpectedSkipAndTitleKeyReads &&
                g_openShowPicCalls == 2 &&
                g_openShowPicNames[0] == "labs.pic" &&
                g_openShowPicNames[1] == "f15.spr",
            "splash start_main preserves original labs key-skip without showing advert or low-res title");
    require(g_videoHiResCalls == kExpectedOneCall &&
                g_showPic640Calls == kExpectedOneCall &&
                g_showPic640Name == "Title640.Pic" &&
                g_setDacCalls == kExpectedOneCall &&
                g_lastDac == kIntroFinalHiResDac,
            "splash start_main preserves original hi-res title picture and final DAC");
    require(g_fadeStepCalls == 2 &&
                g_fadeStepHistory[0] == kIntroLabsFadeSteps &&
                g_fadeStepHistory[1] == kExpectedFadeSteps,
            "splash start_main preserves original fade sequence when labs is key-skipped");

    resetStartMainState(comm, game);
    comm.needSplash = 1;
    comm.setupUseJoy = kNoJoystickSetup;
    comm.trainingFlag = kTrainingCleared;
    g_checkKeyTimerStep = kIntroTimerStepPastTimeout;
    g_checkKeyReturnZeroOnCall = kIntroKeyPressedAdvertPoll;
    g_videoHiResResult = false;
    require(start_main() == kStartExitCode,
            "splash start_main should return the menu exit code when advert is skipped by key");
    require(g_checkKeyCalls == kIntroKeyPressedAdvertPoll &&
                g_getKeyCalls == kIntroExpectedSkipAndTitleKeyReads &&
                g_openShowPicCalls == kIntroExpectedPicCalls &&
                g_openShowPicNames[0] == "labs.pic" &&
                g_openShowPicNames[1] == "adv.pic" &&
                g_openShowPicNames[2] == "title16.pic" &&
                g_openShowPicNames[3] == "f15.spr",
            "splash start_main preserves original advert key-skip into title handling");
    require(g_fadeStepCalls == 4 &&
                g_fadeStepHistory[0] == kIntroLabsFadeSteps &&
                g_fadeStepHistory[1] == kIntroAdvertFadeSteps &&
                g_fadeStepHistory[2] == kIntroTitleFadeSteps &&
                g_fadeStepHistory[3] == kExpectedFadeSteps,
            "splash start_main preserves original fade sequence when advert is key-skipped");

    std::cout << "start main behavior tests passed\n";
    return 0;
}
