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
int16 g_pageDesc[4] = {};
int16 *bufPtr = g_pageDesc;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum StartMainAutostartConstant : int {
    kInitialGfxInitResult = 0x3456,
    kGfxBufferPage = 2,
    kNoSoundSampleSegment = 0,
    kNoSoundVariant = 0,
    kAutostartDifficulty = 0,
    kAutostartTheater = 0,
    kAutostartMissionReady = 1,
    kAutostartCampaignMission = 0,
    kAutostartCampaignProgress = 0,
    kAutostartRandSeed = 12345,
    kAutostartMissionReadyTraining = 2,
    kJoyAxis0 = 0,
    kJoyAxis1 = 1,
    kExpectedFadeSteps = 8,
    kGfxGetValMcga = 0,
    kGfxGetValLoadPic = 1,
    kSpritePage = 2,
    kTrainingCleared = 0,
    kTrainingSet = 1,
    kStartExitCode = 12,
    kClearX1 = 0,
    kClearY1 = 0,
    kScreenMaxX = 319, // sttypes.h SCREEN_MAXX: last visible 320-wide column.
    kScreenMaxY = 199, // sttypes.h SCREEN_MAXY: last visible 200-high row.
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
int g_missionGenerateCalls = 0;
int g_restoreCalls = 0;
int g_fadeSteps = -1;
int g_openShowPicCalls = 0;
int g_openShowPicPage = -1;
std::string g_openShowPicName;
int g_loadPicCalls = 0;
int g_exportWorldCalls = 0;
std::string g_exportWorldName;
int g_clearKeyFlagsCalls = 0;
int g_clearRectCalls = 0;
int g_clearX1 = -1;
int g_clearY1 = -1;
int g_clearX2 = -1;
int g_clearY2 = -1;
int g_gfxGetValResult = kGfxGetValMcga;
int g_missionReadyAfterGenerate = kAutostartMissionReady;

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
    menuSprites = 0;
    exitCode = 0;
    comm.gfxInitResult = kInitialGfxInitResult;
    comm.needSplash = 1;
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
    g_missionGenerateCalls = 0;
    g_restoreCalls = 0;
    g_fadeSteps = -1;
    g_openShowPicCalls = 0;
    g_openShowPicPage = -1;
    g_openShowPicName.clear();
    g_loadPicCalls = 0;
    g_exportWorldCalls = 0;
    g_exportWorldName.clear();
    g_clearKeyFlagsCalls = 0;
    g_clearRectCalls = 0;
    g_clearX1 = -1;
    g_clearY1 = -1;
    g_clearX2 = -1;
    g_clearY2 = -1;
    g_gfxGetValResult = kGfxGetValMcga;
    g_missionReadyAfterGenerate = kAutostartMissionReady;
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

void missionGenerate(void) {
    ++g_missionGenerateCalls;
    gameData->missionReady = g_missionReadyAfterGenerate;
}

void restoreCbreakHandler(void) { ++g_restoreCalls; }

void FAR CDECL gfx_setFadeSteps(int steps) { g_fadeSteps = steps; }

int FAR CDECL gfx_getVal(void) { return g_gfxGetValResult; }

void openShowPic(const char *filename, int page) {
    ++g_openShowPicCalls;
    g_openShowPicName = filename ? filename : "";
    g_openShowPicPage = page;
}

void loadPic(const char *, int) { ++g_loadPicCalls; }

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

int main() {
    struct GameComm comm = {};
    struct Game game = {};
    resetStartMainState(comm, game);

    const int result = start_main();

    require(result == kStartExitCode,
            "DEBUG_AUTOSTART start_main should return the menu exit code");
    require(g_installCalls == kExpectedOneCall,
            "start_main installs the break handler before setup");
    require(g_storeBufPtrCalls == kExpectedOneCall &&
                g_storeBufPtrSegment == kInitialGfxInitResult &&
                g_storeBufPtrPage == kGfxBufferPage,
            "start_main publishes the launcher graphics buffer to page slot 2");
    require(g_initGraphicsCalls == kExpectedOneCall,
            "start_main initializes START graphics once");
    require(g_audioShutdownCalls == kExpectedOneCall &&
                g_audioSetupCalls == kExpectedOneCall &&
                g_audioSetupSegment == kNoSoundSampleSegment &&
                g_audioSetupVariant == kNoSoundVariant,
            "start_main resets audio to the no-sound setup");
    require(game.difficulty == kAutostartDifficulty &&
                game.theater == kAutostartTheater &&
                game.missionReady == kAutostartMissionReady &&
                game.isCampaignMission == kAutostartCampaignMission &&
                game.campaignProgress == kAutostartCampaignProgress &&
                game.rand == kAutostartRandSeed,
            "DEBUG_AUTOSTART initializes the hardcoded mission setup");
    require(joyAxes[kJoyAxis0] == JOY_CENTER &&
                joyAxes[kJoyAxis1] == JOY_CENTER,
            "DEBUG_AUTOSTART centers joystick axes");
    require(g_missionGenerateCalls == kExpectedOneCall,
            "DEBUG_AUTOSTART still generates the mission");
    require(g_restoreCalls == kExpectedOneCall,
            "start_main restores the break handler before returning");
    require(comm.needSplash == 0,
            "start_main clears needSplash after the first START pass");
    require(g_fadeSteps == kExpectedFadeSteps,
            "start_main uses the original sprite-load fade step count");
    require(g_openShowPicCalls == kExpectedOneCall &&
                g_openShowPicName == "f15.spr" &&
                g_openShowPicPage == kSpritePage &&
                g_loadPicCalls == 0,
            "MCGA path loads f15.spr through openShowPic page 2");
    require(g_exportWorldCalls == kExpectedOneCall &&
                g_exportWorldName == "temp.wld",
            "start_main exports the generated world through temp.wld");
    require(comm.trainingFlag == kTrainingCleared,
            "start_main clears restart and non-training mission flags");
    require(g_clearKeyFlagsCalls == kExpectedOneCall,
            "start_main clears BIOS/input key flags before exit");
    require(g_clearRectCalls == kExpectedOneCall &&
                g_clearX1 == kClearX1 &&
                g_clearY1 == kClearY1 &&
                g_clearX2 == kScreenMaxX &&
                g_clearY2 == kScreenMaxY,
            "start_main clears the full 320x200 START page before exit");

    resetStartMainState(comm, game);
    g_gfxGetValResult = kGfxGetValLoadPic;
    g_missionReadyAfterGenerate = kAutostartMissionReadyTraining;
    require(start_main() == kStartExitCode,
            "DEBUG_AUTOSTART start_main should return the menu exit code in loadPic mode");
    require(g_openShowPicCalls == 0 &&
                g_loadPicCalls == kExpectedOneCall,
            "non-MCGA path loads f15.spr through loadPic with the original graphics buffer");
    require(comm.trainingFlag == kTrainingSet,
            "start_main preserves original training flag export when generated missionReady exceeds one");

    std::cout << "start main autostart behavior tests passed\n";
    return 0;
}
