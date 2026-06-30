#include "comm.h"
#include "gfx.h"
#include "inttype.h"

#include <cstdlib>
#include <iostream>
#include <cstdarg>
#include <sys/wait.h>
#include <unistd.h>

void game_init(void);
int f15_program_main(int argc, char *argv[]);

// The test target renames f15.c's program entry point; keep this file's test
// entry point normal.
#ifdef main
#undef main
#endif

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum LauncherRuntimeConstant : int {
    kVgaNoSoundGfxInitPage = 2,
    kAllocatedGfxBufferSegment = 0x5a5a,
    kInitialNeedSplash = 1,
    kJoystickDisabled = 0,
    kDefaultDetailLevel = 3,
    kProgramExitOk = 0,
    kRetMenu = 0x0c,
    kExpectedFatalExit = 77,
    kExpectedOneCall = 1,
    kExpectedTwoCalls = 2,
    kTestFailureExitCode = 1,
};

int g_initStateCalls = 0;
int g_setMode13Calls = 0;
int g_allocPageCalls = 0;
int g_allocPageArg = -1;
int g_videoInitCalls = 0;
int g_videoShutdownCalls = 0;
int g_startMainCalls = 0;
int g_egameMainCalls = 0;
int g_endMainCalls = 0;
int g_logSetAppCalls = 0;
int g_startResults[4] = {};
int g_egameResults[4] = {};
int g_endResults[4] = {};
int g_expectLogCritical = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetLauncherCallState() {
    g_initStateCalls = g_setMode13Calls = g_allocPageCalls = 0;
    g_videoInitCalls = g_videoShutdownCalls = 0;
    g_startMainCalls = g_egameMainCalls = g_endMainCalls = 0;
    g_logSetAppCalls = 0;
    for (int idx = 0; idx < 4; ++idx) {
        g_startResults[idx] = 0;
        g_egameResults[idx] = 0;
        g_endResults[idx] = 0;
    }
    g_expectLogCritical = 0;
}

void expectFatalLauncherArg(char *programName, char *arg, const char *message) {
    pid_t pid = fork();
    require(pid >= 0, "fork should be available for launcher fatal-path tests");
    if (pid == 0) {
        char *argv[] = {programName, arg, nullptr};
        g_expectLogCritical = 1;
        (void)f15_program_main(2, argv);
        std::exit(kTestFailureExitCode);
    }
    int status = 0;
    require(waitpid(pid, &status, 0) == pid, "launcher fatal-path child should exit");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kExpectedFatalExit, message);
}

} // namespace

void gfx_initState(void) {
    ++g_initStateCalls;
}

void gfx_setMode13(void) {
    ++g_setMode13Calls;
}

int FAR CDECL gfx_allocPage(int pageNum) {
    ++g_allocPageCalls;
    g_allocPageArg = pageNum;
    return kAllocatedGfxBufferSegment;
}

void gfx_videoInit(void) { ++g_videoInitCalls; }
void gfx_videoShutdown(void) { ++g_videoShutdownCalls; }
void log_set_app(const char *) { ++g_logSetAppCalls; }
void log_critical(const char *, ...) {
    if (g_expectLogCritical) {
        std::exit(kExpectedFatalExit);
    }
    std::cerr << "failed: launcher test reached LogCritical\n";
    std::exit(kTestFailureExitCode);
}
int start_main(void) {
    int idx = g_startMainCalls++;
    return g_startResults[idx];
}
int egame_main(void) {
    int idx = g_egameMainCalls++;
    return g_egameResults[idx];
}
int end_main(void) {
    int idx = g_endMainCalls++;
    return g_endResults[idx];
}

int main() {
    game_init();

    require(commData != nullptr, "game_init should publish the COMM buffer");
    require(gameData != nullptr, "game_init should publish the GAME buffer");
    require(commData->needSplash == kInitialNeedSplash,
            "merged launcher keeps original first-pass splash flag");
    require(commData->setupUseJoy == kJoystickDisabled,
            "merged launcher preserves no-joystick setup");
    require(commData->setupDetail == kDefaultDetailLevel,
            "merged launcher preserves default detail level");
    require(g_initStateCalls == kExpectedOneCall,
            "game_init initializes the graphics slot state once");
    require(g_setMode13Calls == kExpectedOneCall,
            "game_init switches to mode 13h once");
    require(g_allocPageCalls == kExpectedOneCall &&
                g_allocPageArg == kVgaNoSoundGfxInitPage,
            "game_init allocates the original shared graphics buffer page");
    require(static_cast<uint16>(commData->gfxInitResult) ==
                kAllocatedGfxBufferSegment,
            "game_init stores the graphics buffer segment in COMM");

    resetLauncherCallState();
    char programName[] = "f15se2";
    char *argv[] = {programName, nullptr};
    require(f15_program_main(1, argv) == kProgramExitOk,
            "f15_program_main returns success after the original merged launcher loop exits");
    require(g_videoInitCalls == kExpectedOneCall &&
                g_videoShutdownCalls == kExpectedOneCall,
            "f15_program_main brackets the launcher loop with original video init/shutdown");
    require(g_startMainCalls == kExpectedOneCall &&
                g_egameMainCalls == 0 &&
                g_endMainCalls == 0,
            "f15_program_main exits after START when no debug flag forces the next program");
    require(g_initStateCalls == kExpectedOneCall &&
                g_setMode13Calls == kExpectedOneCall &&
                g_allocPageCalls == kExpectedOneCall,
            "f15_program_main calls game_init once before running START");
    require(g_logSetAppCalls >= kExpectedTwoCalls,
            "f15_program_main switches log context through the original launcher path");

    resetLauncherCallState();
    char debugMenuArg[] = "/d1";
    char *debugMenuArgv[] = {programName, debugMenuArg, nullptr};
    require(f15_program_main(2, debugMenuArgv) == kProgramExitOk,
            "f15_program_main accepts the original debug-menu command-line flag");
    require(g_startMainCalls == kExpectedOneCall &&
                g_egameMainCalls == kExpectedOneCall &&
                g_endMainCalls == 0,
            "debug-menu mode continues from START into EGAME before normal flight exit");

    resetLauncherCallState();
    char debugFlightArg[] = "/d12";
    char *debugFlightArgv[] = {programName, debugFlightArg, nullptr};
    require(f15_program_main(2, debugFlightArgv) == kProgramExitOk,
            "f15_program_main accepts combined original debug menu/flight flags");
    require(g_startMainCalls == kExpectedOneCall &&
                g_egameMainCalls == kExpectedOneCall &&
                g_endMainCalls == kExpectedOneCall,
            "debug-flight mode continues from EGAME into END before normal debrief exit");

    resetLauncherCallState();
    char debugDebriefArg[] = "/d3";
    char *debugDebriefArgv[] = {programName, debugDebriefArg, nullptr};
    g_startResults[0] = kRetMenu;
    g_egameResults[0] = 1; /* Nonzero keeps the original launcher moving into END. */
    require(f15_program_main(2, debugDebriefArgv) == kProgramExitOk,
            "f15_program_main accepts the original debug-debrief command-line flag");
    require(g_startMainCalls == kExpectedTwoCalls &&
                g_egameMainCalls == kExpectedOneCall &&
                g_endMainCalls == kExpectedOneCall,
            "debug-debrief mode loops back once, then exits through START when menu debug is off");

    resetLauncherCallState();
    char badPrefixArg[] = "/x";
    expectFatalLauncherArg(programName, badPrefixArg,
                           "f15_program_main rejects non-debug launcher arguments");

    resetLauncherCallState();
    char badSelectorArg[] = "/d9";
    expectFatalLauncherArg(programName, badSelectorArg,
                           "f15_program_main rejects unknown original debug selector characters");

    std::cout << "launcher runtime behavior tests passed\n";
    return 0;
}
