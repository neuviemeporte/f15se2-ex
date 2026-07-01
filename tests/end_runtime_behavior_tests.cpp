#include "comm.h"
#include "const.h"
#include "endata.h"
#include "endtypes.h"
#include "gfx.h"
#include "inttype.h"

#include <cstdlib>
#include <iostream>
#include "posix_test_compat.h"

void enInitGraphics(void);
int end_main(void);
void checkQuitFlag(void);

struct GameComm *commData = nullptr;
uint8 quitFlag = 0;
uint8 joyAxisY = 0;
uint8 joyAxisX = 0;
int hasVgaMode = 0;
void *gfxBufSeg = nullptr;
void *vgaBufSeg = nullptr;
void *vgaBufSeg2 = nullptr;
int vgaBufOffset = -1;
int missionResult = 0;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EndRuntimeOriginalConstant : int {
    kInitPage = 0,
    kDacPalette = 1,
    kStoredPage = 1,
    kGfxInitResult = 0x3456,
    kAuxBufSize = 0x2345,
    kAuxBufMarker = 0x7100,
    kVgaBufMarker = 0x7200,
    kKeyboardOnly = 0,
    kJoystickEnabled = 1,
    kLandingNormal = 0,
    kLandingEjected = 2,
    kInitialMissionResult = 3,
    kDebriefExitCode = 0x23,
    kQuitFlagSet = 1,
    kChildExitOk = 0,
    kExpectedOneCall = 1,
    kExpectedThreeCalls = 3,
    kTestFailureExitCode = 1,
};

int g_seedCalls = 0;
int g_setPageCalls = 0;
int g_allocPageCalls = 0;
int g_getCurPageCalls = 0;
int g_setDacCalls = 0;
int g_storeBufPtrCalls = 0;
int g_lastSetPage = -1;
int g_lastAllocPage = -1;
int g_lastGetCurPage = -1;
int g_lastDac = -1;
int g_lastStoreSeg = -1;
int g_lastStorePage = -1;
int g_clearKeybufCalls = 0;
int g_installCbreakCalls = 0;
int g_copyJoystickCalls = 0;
int g_loadWorldStringsCalls = 0;
int g_getAuxBufSizeCalls = 0;
int g_allocBufferCalls = 0;
int g_computeMissionResultCalls = 0;
int g_debriefMainLoopCalls = 0;
int g_showAwardsCalls = 0;
int g_restoreCbreakCalls = 0;
int g_cleanupCalls = 0;
int g_miscClearKeyFlagsCalls = 0;
int g_lastAllocSize = -1;
uint8 FAR *g_lastJoystickData = nullptr;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetEndRuntimeState() {
    g_seedCalls = 0;
    g_setPageCalls = 0;
    g_allocPageCalls = 0;
    g_getCurPageCalls = 0;
    g_setDacCalls = 0;
    g_storeBufPtrCalls = 0;
    g_lastSetPage = -1;
    g_lastAllocPage = -1;
    g_lastGetCurPage = -1;
    g_lastDac = -1;
    g_lastStoreSeg = -1;
    g_lastStorePage = -1;
    g_clearKeybufCalls = 0;
    g_installCbreakCalls = 0;
    g_copyJoystickCalls = 0;
    g_loadWorldStringsCalls = 0;
    g_getAuxBufSizeCalls = 0;
    g_allocBufferCalls = 0;
    g_computeMissionResultCalls = 0;
    g_debriefMainLoopCalls = 0;
    g_showAwardsCalls = 0;
    g_restoreCbreakCalls = 0;
    g_cleanupCalls = 0;
    g_miscClearKeyFlagsCalls = 0;
    g_lastAllocSize = -1;
    g_lastJoystickData = nullptr;
    quitFlag = 0;
    joyAxisX = joyAxisY = 0;
    hasVgaMode = 0;
    gfxBufSeg = nullptr;
    vgaBufSeg = nullptr;
    vgaBufSeg2 = nullptr;
    vgaBufOffset = -1;
    missionResult = 0;
}

} // namespace

void enSeedRandom(void) {
    ++g_seedCalls;
}

void FAR CDECL gfx_setPageN(uint16 pageNum) {
    ++g_setPageCalls;
    g_lastSetPage = pageNum;
}

int FAR CDECL gfx_allocPage(int pageNum) {
    ++g_allocPageCalls;
    g_lastAllocPage = pageNum;
    return 0;
}

void FAR CDECL gfx_getCurPage(int pageNum) {
    ++g_getCurPageCalls;
    g_lastGetCurPage = pageNum;
}

void FAR CDECL gfx_setDac(uint16 palIdx) {
    ++g_setDacCalls;
    g_lastDac = palIdx;
}

void FAR CDECL gfx_storeBufPtr(uint16 seg, int pageIdx) {
    ++g_storeBufPtrCalls;
    g_lastStoreSeg = seg;
    g_lastStorePage = pageIdx;
}

void FAR CDECL misc_clearKeyFlags(void) { ++g_miscClearKeyFlagsCalls; }
void clearKeybuf(void) { ++g_clearKeybufCalls; }
void installCBreakHandler(void) { ++g_installCbreakCalls; }
void restoreCbreakHandler(void) { ++g_restoreCbreakCalls; }
void cleanup(void) { ++g_cleanupCalls; }
void FAR copyJoystickData(uint8 FAR *joyData) {
    ++g_copyJoystickCalls;
    g_lastJoystickData = joyData;
}
void loadWorldStrings(void) { ++g_loadWorldStringsCalls; }
int FAR CDECL gfx_getAuxBufSize(void) {
    ++g_getAuxBufSizeCalls;
    return kAuxBufSize;
}
void *allocBuffer(int size) {
    ++g_allocBufferCalls;
    g_lastAllocSize = size;
    return reinterpret_cast<void *>(
        static_cast<uintptr_t>(g_allocBufferCalls == 1 ? kAuxBufMarker : kVgaBufMarker));
}
void computeMissionResult(void) { ++g_computeMissionResultCalls; }
void debriefMainLoop(void) { ++g_debriefMainLoopCalls; }
void showPostMissionAwards(void) { ++g_showAwardsCalls; }

int main() {
    struct GameComm comm = {};

    resetEndRuntimeState();
    comm.gfxInitResult = kGfxInitResult;
    commData = &comm;
    enInitGraphics();

    require(g_seedCalls == kExpectedOneCall,
            "enInitGraphics seeds the original END random state first");
    require(g_setPageCalls == kExpectedOneCall &&
                g_lastSetPage == kInitPage,
            "enInitGraphics selects the original initial graphics page");
    require(g_allocPageCalls == kExpectedOneCall &&
                g_lastAllocPage == kInitPage,
            "enInitGraphics allocates the original initial graphics page");
    require(g_getCurPageCalls == kExpectedOneCall &&
                g_lastGetCurPage == kInitPage,
            "enInitGraphics queries the original current page descriptor");
    require(g_setDacCalls == kExpectedOneCall &&
                g_lastDac == kDacPalette,
            "enInitGraphics loads the original END DAC palette");
    require(g_storeBufPtrCalls == kExpectedOneCall &&
                g_lastStoreSeg == kGfxInitResult &&
                g_lastStorePage == kStoredPage,
            "enInitGraphics stores commData->gfxInitResult in the original page slot");

    resetEndRuntimeState();
    quitFlag = 0;
    checkQuitFlag();
    require(g_cleanupCalls == 0 && g_restoreCbreakCalls == 0,
            "checkQuitFlag preserves original no-op behavior when quitFlag is clear");

    resetEndRuntimeState();
    comm = {};
    commData = &comm;
    comm.setupUseJoy = kKeyboardOnly;
    comm.landingType = kLandingEjected;
    comm.gfxInitResult = kGfxInitResult;
    hasVgaMode = 1;
    require(end_main() == kDebriefExitCode,
            "end_main returns the original debrief exit code");
    require(g_miscClearKeyFlagsCalls == kExpectedOneCall &&
                g_clearKeybufCalls == kExpectedThreeCalls &&
                g_installCbreakCalls == kExpectedOneCall &&
                g_restoreCbreakCalls == kExpectedOneCall,
            "end_main preserves the original key-buffer and C-break handler sequence");
    require(joyAxisX == JOY_CENTER &&
                joyAxisY == JOY_CENTER &&
                g_copyJoystickCalls == 0,
            "end_main centers joystick axes when the original setupUseJoy flag is clear");
    require(g_loadWorldStringsCalls == kExpectedOneCall &&
                g_getAuxBufSizeCalls == kExpectedOneCall &&
                g_allocBufferCalls == 2 &&
                g_lastAllocSize == VGA_BUF_SIZE &&
                gfxBufSeg == reinterpret_cast<void *>(static_cast<uintptr_t>(kAuxBufMarker)) &&
                vgaBufSeg == reinterpret_cast<void *>(static_cast<uintptr_t>(kVgaBufMarker)) &&
                vgaBufSeg2 == vgaBufSeg &&
                vgaBufOffset == 0,
            "end_main allocates original aux and VGA buffers and resets VGA offset");
    require(missionResult == kInitialMissionResult &&
                g_computeMissionResultCalls == kExpectedOneCall &&
                g_debriefMainLoopCalls == kExpectedOneCall &&
                g_showAwardsCalls == kExpectedOneCall,
            "end_main initializes mission result, computes ejection/crash result, runs debrief, and shows awards");

    resetEndRuntimeState();
    comm = {};
    commData = &comm;
    comm.setupUseJoy = kJoystickEnabled;
    comm.landingType = kLandingNormal;
    comm.gfxInitResult = kGfxInitResult;
    hasVgaMode = 0;
    require(end_main() == kDebriefExitCode,
            "end_main returns the original debrief exit code in joystick setup mode");
    require(g_copyJoystickCalls == kExpectedOneCall &&
                g_lastJoystickData == comm.joyData &&
                joyAxisX == 0 &&
                joyAxisY == 0,
            "end_main copies original joystick calibration data when setupUseJoy is enabled");
    require(g_allocBufferCalls == kExpectedOneCall &&
                g_lastAllocSize == kAuxBufSize &&
                vgaBufSeg == nullptr &&
                vgaBufSeg2 == nullptr &&
                vgaBufOffset == -1,
            "end_main skips original VGA allocation when VGA mode is unavailable");
    require(missionResult == kInitialMissionResult &&
                g_computeMissionResultCalls == 0,
            "end_main leaves the original default mission result when landingType does not request recompute");

    resetEndRuntimeState();
    quitFlag = kQuitFlagSet;
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for END quitFlag exit behavior");
    if (child == 0) {
        checkQuitFlag();
        std::exit(kTestFailureExitCode);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for END quitFlag exit child");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
            "checkQuitFlag preserves original cleanup, C-break restore, and exit path");

    std::cout << "end_runtime_behavior_tests passed\n";
    return 0;
}
