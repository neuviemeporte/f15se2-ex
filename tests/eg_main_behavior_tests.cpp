#include "comm.h"
#include "dos.h"
#include "egdata.h"
#include "gfx.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

void gfxInit(void);
void drawCockpit(void);
void runGameSession(void);
int egame_main(void);
void doNothing3(void);
void doNothing4(void);

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;
uint8 joyAxes[2] = {};
int16 exitCode = 0;
char aRegn_xxx[] = "regn.xxx";
char aLb_xxx[] = "lb.xxx";
char aPg_xxx[] = "pg.xxx";
char aVn_xxx[] = "vn.xxx";
char aMe_xxx[] = "me.xxx";
char aNc_xxx[] = "nc.xxx";
char aCe_xxx[] = "ce.xxx";
char aJp_xxx[] = "jp.xxx";
char aNa_xxx[] = "na.xxx";

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EgMainOriginalConstant : int {
    kPage0 = 0,
    kPage1 = 1,
    kPage1Segment = 0x4321,
    kGfxInitResult = 0x2468,
    kGfxBufferPage = 2,
    kTheaterLibya = 0,
    kTheaterVietnam = 2,
    kDacModeFlagEnabled = 1,
    kDacModeFlagDisabled = 0,
    kDacModeCodeMcga = 3,
    kDacModeCodeEga = 0,
    kWorldGroundColor = 0x2A,
    kCockpitPicPage = 1,
    kCockpitCopySrcPage = 1,
    kCockpitCopyDstPage0 = 0,
    kCockpitCopyDstPage2 = 2,
    kCockpitCopyY = 96,
    kCockpitCopyWidth = 320,
    kCockpitCopyHeight = 104,
    kFadeStepsLowTheater = 12,
    kFadeStepsHighTheater = 16,
    kDacAnimCount = 1,
    kWaitFrameSyncFrames = 2,
    kVideoModeText = 3,
    kNonTextExitCode = 7,
    kJoyCenter = 0x80,
    kTacIndicatorBaseColor = 3,
    kExpectedTwoCalls = 2,
    kExpectedOneCall = 1,
    kExpectedNoCalls = 0,
    kTestFailureExitCode = 1,
};

int g_allocPageCalls = 0;
int g_storeBufPtrCalls = 0;
int g_allocPages[2] = {};
int g_storeSegments[2] = {};
int g_storePages[2] = {};
int g_initMissionStringsCalls = 0;
int g_load15Calls = 0;
int g_loadRegionCalls = 0;
int g_loadDgtlCalls = 0;
int g_setupDacCalls = 0;
int g_setDacCalls = 0;
int g_lastDac = -1;
int g_waitRetraceCalls = 0;
int g_openPicCalls = 0;
char g_lastOpenPicName[32] = {};
int g_lastOpenPicPage = -1;
int g_copyRectCalls = 0;
int g_copyRectDstPages[2] = {};
int g_gfxModeFlag = kDacModeFlagEnabled;
int g_gfxModeCode = kDacModeCodeMcga;
int g_installCbreakCalls = 0;
int g_restoreCbreakCalls = 0;
int g_copyJoystickCalls = 0;
int g_restoreJoystickCalls = 0;
int g_gfxInitOverlayCalls = 0;
int g_setFadeStepsCalls = 0;
int g_lastFadeSteps = -1;
int g_setupInstrumentCalls = 0;
int g_audioShutdownCalls = 0;
int g_audioSetupCalls = 0;
int g_timerHookCalls = 0;
int g_setTimerCalls = 0;
int g_restoreTimerCalls = 0;
int g_setInt9Calls = 0;
int g_restoreInt9Calls = 0;
int g_runGameLoopCalls = 0;
int g_moveDataCalls = 0;
int g_setDacAnimCountCalls = 0;
int g_lastDacAnimCount = -1;
int g_waitFrameSyncCalls = 0;
int g_lastWaitFrameSyncFrames = -1;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetEgMainState() {
    g_allocPageCalls = 0;
    g_storeBufPtrCalls = 0;
    g_allocPages[0] = g_allocPages[1] = -1;
    g_storeSegments[0] = g_storeSegments[1] = -1;
    g_storePages[0] = g_storePages[1] = -1;
    g_initMissionStringsCalls = 0;
    g_load15Calls = 0;
    g_loadRegionCalls = 0;
    g_loadDgtlCalls = 0;
    g_setupDacCalls = 0;
    g_setDacCalls = 0;
    g_lastDac = -1;
    g_waitRetraceCalls = 0;
    g_openPicCalls = 0;
    std::memset(g_lastOpenPicName, 0, sizeof(g_lastOpenPicName));
    g_lastOpenPicPage = -1;
    g_copyRectCalls = 0;
    g_copyRectDstPages[0] = g_copyRectDstPages[1] = -1;
    g_gfxModeFlag = kDacModeFlagEnabled;
    g_gfxModeCode = kDacModeCodeMcga;
    g_installCbreakCalls = 0;
    g_restoreCbreakCalls = 0;
    g_copyJoystickCalls = 0;
    g_restoreJoystickCalls = 0;
    g_gfxInitOverlayCalls = 0;
    g_setFadeStepsCalls = 0;
    g_lastFadeSteps = -1;
    g_setupInstrumentCalls = 0;
    g_audioShutdownCalls = 0;
    g_audioSetupCalls = 0;
    g_timerHookCalls = 0;
    g_setTimerCalls = 0;
    g_restoreTimerCalls = 0;
    g_setInt9Calls = 0;
    g_restoreInt9Calls = 0;
    g_runGameLoopCalls = 0;
    g_moveDataCalls = 0;
    g_setDacAnimCountCalls = 0;
    g_lastDacAnimCount = -1;
    g_waitFrameSyncCalls = 0;
    g_lastWaitFrameSyncFrames = -1;
}

} // namespace

int FAR CDECL gfx_allocPage(int pageNum) {
    require(g_allocPageCalls < kExpectedTwoCalls,
            "gfxInit should allocate exactly the original two pages");
    g_allocPages[g_allocPageCalls++] = pageNum;
    return pageNum == kPage1 ? kPage1Segment : 0;
}

void FAR CDECL gfx_storeBufPtr(uint16 seg, int pageIdx) {
    require(g_storeBufPtrCalls < kExpectedTwoCalls,
            "gfxInit should store exactly the original two page buffers");
    g_storeSegments[g_storeBufPtrCalls] = seg;
    g_storePages[g_storeBufPtrCalls] = pageIdx;
    ++g_storeBufPtrCalls;
}

void gfxInit(void) {
    gfx_allocPage(kPage0);
    int page1Seg = gfx_allocPage(kPage1);
    gfx_storeBufPtr(static_cast<uint16>(page1Seg), kPage1);
    gfx_storeBufPtr(static_cast<uint16>(commData->gfxInitResult), kGfxBufferPage);
}

void initMissionStrings(void) { ++g_initMissionStringsCalls; }
void load15Flt3d3(void) { ++g_load15Calls; }
void loadRegion3D(void) { ++g_loadRegionCalls; }
int loadF15DgtlBin(void) {
    ++g_loadDgtlCalls;
    return kGfxInitResult;
}
void setupDac(void) { ++g_setupDacCalls; }
int FAR CDECL gfx_getModeFlag(void) { return g_gfxModeFlag; }
int FAR CDECL gfx_getModecode(void) { return g_gfxModeCode; }
void FAR CDECL gfx_setDac(uint16 dac) {
    ++g_setDacCalls;
    g_lastDac = dac;
}
void FAR CDECL gfx_waitRetrace(void) { ++g_waitRetraceCalls; }
void openBlitClosePic(const char *filename, int page) {
    ++g_openPicCalls;
    std::strncpy(g_lastOpenPicName, filename, sizeof(g_lastOpenPicName) - 1);
    g_lastOpenPicPage = page;
}
void FAR CDECL gfx_copyRect(int srcPage, uint16 srcX, uint16 srcY, int dstPage,
                            uint16 dstX, uint16 dstY, int width, int height) {
    require(srcPage == kCockpitCopySrcPage && srcX == 0 && srcY == kCockpitCopyY &&
                dstX == 0 && dstY == kCockpitCopyY &&
                width == kCockpitCopyWidth && height == kCockpitCopyHeight,
            "drawCockpit preserves the original cockpit lower-panel copy rectangle");
    require(g_copyRectCalls < kExpectedTwoCalls, "drawCockpit copies cockpit lower panel to two pages");
    g_copyRectDstPages[g_copyRectCalls++] = dstPage;
}
void installCBreakHandler(void) { ++g_installCbreakCalls; }
void restoreCbreakHandler(void) { ++g_restoreCbreakCalls; }
void FAR copyJoystickData(uint8 FAR *) { ++g_copyJoystickCalls; }
int FAR restoreJoystickData(uint8 FAR *) {
    ++g_restoreJoystickCalls;
    return 0;
}
void gfx_initOverlay(void) { ++g_gfxInitOverlayCalls; }
void FAR CDECL gfx_setFadeSteps(int steps) {
    ++g_setFadeStepsCalls;
    g_lastFadeSteps = steps;
}
void FAR CDECL setupInstrumentLayoutFar(void) { ++g_setupInstrumentCalls; }
int FAR CDECL audio_shutdown(void) {
    ++g_audioShutdownCalls;
    return 0;
}
int FAR CDECL audio_setup(int16, int16) {
    ++g_audioSetupCalls;
    return 0;
}
void setTimerTickHook(void(FAR *)(void)) { ++g_timerHookCalls; }
void egAdvanceFrameTick(void) {}
void setTimerIrqHandler(void) { ++g_setTimerCalls; }
void restoreTimerIrqHandler(void) { ++g_restoreTimerCalls; }
int setInt9Handler(void) {
    ++g_setInt9Calls;
    return 0;
}
int restoreInt9Handler(void) {
    ++g_restoreInt9Calls;
    return 0;
}
void runGameLoop(void) { ++g_runGameLoopCalls; }
void moveDataFar(void) { ++g_moveDataCalls; }
void FAR CDECL gfx_setDacAnimCount(uint16 count) {
    ++g_setDacAnimCountCalls;
    g_lastDacAnimCount = count;
}
void waitFrameSync(int frames) {
    ++g_waitFrameSyncCalls;
    g_lastWaitFrameSyncFrames = frames;
}
int main() {
    struct GameComm comm = {};
    struct Game game = {};

    resetEgMainState();
    comm.gfxInitResult = kGfxInitResult;
    commData = &comm;
    gameData = &game;
    gfxInit();

    require(g_allocPageCalls == kExpectedTwoCalls &&
                g_allocPages[0] == kPage0 &&
                g_allocPages[1] == kPage1,
            "gfxInit preserves the original page 0/page 1 allocation order");
    require(g_storeBufPtrCalls == kExpectedTwoCalls &&
                g_storeSegments[0] == kPage1Segment &&
                g_storePages[0] == kPage1 &&
                g_storeSegments[1] == kGfxInitResult &&
                g_storePages[1] == kGfxBufferPage,
            "gfxInit stores the original allocated page and commData graphics buffer slots");

    doNothing3();
    doNothing4();

    resetEgMainState();
    game.theater = kTheaterVietnam;
    g_world3dData[47] = kWorldGroundColor;
    drawCockpit();
    require(g_initMissionStringsCalls == kExpectedOneCall &&
                g_load15Calls == kExpectedOneCall &&
                std::strcmp(regnStr, scenarioPlh[kTheaterVietnam]) == 0 &&
                g_loadRegionCalls == kExpectedOneCall &&
                f15DgtlResult == kGfxInitResult &&
                g_horizonGroundColor == kWorldGroundColor &&
                g_dacSupported == kDacModeFlagEnabled &&
                g_setupDacCalls == kExpectedOneCall,
            "drawCockpit preserves the original mission/string/region/DAC setup sequence");
    require(g_setDacCalls == kExpectedOneCall &&
                g_lastDac == 1 &&
                g_waitRetraceCalls == kExpectedOneCall &&
                g_openPicCalls == kExpectedOneCall &&
                std::strcmp(g_lastOpenPicName, "256pit.PIC") == 0 &&
                g_lastOpenPicPage == kCockpitPicPage &&
                g_copyRectCalls == kExpectedTwoCalls &&
                g_copyRectDstPages[0] == kCockpitCopyDstPage0 &&
                g_copyRectDstPages[1] == kCockpitCopyDstPage2,
            "drawCockpit preserves original MCGA cockpit picture and lower-panel copies");

    resetEgMainState();
    game.theater = kTheaterVietnam;
    g_gfxModeFlag = kDacModeFlagDisabled;
    g_gfxModeCode = kDacModeCodeEga;
    drawCockpit();
    require(g_dacSupported == kDacModeFlagDisabled &&
                g_setupDacCalls == kExpectedNoCalls &&
                std::strcmp(g_lastOpenPicName, "cockpit.PIC") == 0 &&
                g_lastOpenPicPage == kCockpitPicPage,
            "drawCockpit preserves original non-MCGA cockpit picture path without DAC setup");

    resetEgMainState();
    comm.setupUseJoy = 0;
    runGameSession();
    require(g_audioShutdownCalls == kExpectedTwoCalls &&
                g_audioSetupCalls == kExpectedOneCall &&
                g_timerHookCalls == kExpectedTwoCalls &&
                g_setTimerCalls == kExpectedOneCall &&
                g_restoreTimerCalls == kExpectedOneCall &&
                g_setInt9Calls == kExpectedOneCall &&
                g_restoreInt9Calls == kExpectedOneCall &&
                g_runGameLoopCalls == kExpectedOneCall &&
                g_moveDataCalls == kExpectedOneCall,
            "runGameSession preserves the original audio/timer/keyboard/game-loop bracket");
    require(g_setDacAnimCountCalls == kExpectedOneCall &&
                g_lastDacAnimCount == kDacAnimCount &&
                g_waitFrameSyncCalls == kExpectedOneCall &&
                g_lastWaitFrameSyncFrames == kWaitFrameSyncFrames,
            "runGameSession preserves the original DAC animation count and post-loop wait");

    resetEgMainState();
    std::memset(g_tacmapIndicators, 0, sizeof(int16) * 23);
    g_initPhase = 99;
    g_missionEndedFlag[0] = g_missionEndedFlag[1] = 1;
    g_eventLogCount = 7;
    g_ejectState = 5;
    exitCode = 0;
    comm.setupUseJoy = 0;
    comm.gfxInitResult = kGfxInitResult;
    game.theater = kTheaterLibya;
    egame_main();
    require(g_initPhase == 0 &&
                g_missionEndedFlag[0] == 0 &&
                g_missionEndedFlag[1] == 0 &&
                g_eventLogCount == 0 &&
                g_ejectState == 0 &&
                g_tacmapIndicators[7] == kTacIndicatorBaseColor &&
                g_tacmapIndicators[12] == kTacIndicatorBaseColor &&
                g_tacmapIndicators[17] == kTacIndicatorBaseColor &&
                g_tacmapIndicators[22] == kTacIndicatorBaseColor,
            "egame_main resets the original per-mission state before startup");
    require(joyAxes[0] == kJoyCenter &&
                joyAxes[1] == kJoyCenter &&
                g_installCbreakCalls == kExpectedOneCall &&
                g_restoreCbreakCalls == kExpectedOneCall &&
                g_gfxInitOverlayCalls == kExpectedOneCall &&
                g_lastFadeSteps == kFadeStepsLowTheater &&
                gfxBufPtr == kGfxInitResult &&
                g_setupInstrumentCalls == kExpectedOneCall &&
                regs.h.ah == 0 &&
                regs.h.al == kVideoModeText,
            "egame_main preserves original keyboard-mode startup, fade, graphics-buffer, and text-mode exit register setup");

    resetEgMainState();
    exitCode = kNonTextExitCode;
    comm.setupUseJoy = 1;
    comm.gfxInitResult = kGfxInitResult;
    game.theater = kTheaterVietnam;
    egame_main();
    require(g_copyJoystickCalls == kExpectedOneCall &&
                g_restoreJoystickCalls == kExpectedOneCall &&
                g_lastFadeSteps == kFadeStepsHighTheater,
            "egame_main preserves original joystick restore path and high-theater fade count");

    std::cout << "eg_main_behavior_tests passed\n";
    return 0;
}
