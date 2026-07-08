#include "comm.h"
#include "const.h"
#include "endata.h"
#include "inttype.h"
#include "slot.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

extern void waitForKeyOrJoy(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EndInputOriginalConstant : int {
    kKeyboardOnly = 0,
    kJoystickEnabled = 1,
    kNoKeyBuffered = 0,
    kKeyBuffered = 1,
    kJoystickIdle = 0,
    kKeyboardSafeKey = 0x1234,
    kMaxScriptSteps = 8,
    kJoystickButtonAxis = 0,
    kExpectedSingleCall = 1,
    kExpectedNoCalls = 0,
    kExpectedTwoCalls = 2,
    kTwoStepScript = 2,
    kThreeStepScript = 3,
    kFourStepScript = 4,
    kQuitFlagSet = 1,
    kChildExitOk = 0,
    kTestFailureExitCode = 1,
};

int g_checkScript[kMaxScriptSteps] = {};
int g_checkScriptLen = 0;
int g_checkScriptPos = 0;
int g_joystickScript[kMaxScriptSteps] = {};
int g_joystickScriptLen = 0;
int g_joystickScriptPos = 0;
int g_getKeyResult = kKeyboardSafeKey;
int g_getKeyCalls = 0;
int g_joystickCalls = 0;
int g_cleanupCalls = 0;
int g_restoreCbreakCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void setCheckScript(const int *values, int count) {
    require(count <= kMaxScriptSteps, "test key-buffer script capacity");
    std::memset(g_checkScript, 0, sizeof(g_checkScript));
    for (int idx = 0; idx < count; ++idx) g_checkScript[idx] = values[idx];
    g_checkScriptLen = count;
    g_checkScriptPos = 0;
}

void setJoystickScript(const int *values, int count) {
    require(count <= kMaxScriptSteps, "test joystick script capacity");
    std::memset(g_joystickScript, 0, sizeof(g_joystickScript));
    for (int idx = 0; idx < count; ++idx) g_joystickScript[idx] = values[idx];
    g_joystickScriptLen = count;
    g_joystickScriptPos = 0;
}

void resetInputState(struct GameComm &comm) {
    std::memset(&comm, 0, sizeof(comm));
    commData = &comm;
    quitFlag = 0;
    g_checkScriptLen = 0;
    g_checkScriptPos = 0;
    g_joystickScriptLen = 0;
    g_joystickScriptPos = 0;
    g_getKeyResult = kKeyboardSafeKey;
    g_getKeyCalls = 0;
    g_joystickCalls = 0;
    g_cleanupCalls = 0;
    g_restoreCbreakCalls = 0;
}

} // namespace

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;

int16 FAR CDECL misc_checkKeyBuf(void) {
    if (g_checkScriptPos < g_checkScriptLen) return g_checkScript[g_checkScriptPos++];
    return kNoKeyBuffered;
}

int16 FAR CDECL misc_getKey(void) {
    ++g_getKeyCalls;
    return g_getKeyResult;
}

int16 FAR CDECL misc_readJoystick(int16 axis) {
    require(axis == kJoystickButtonAxis, "waitForKeyOrJoy reads original joystick button axis");
    ++g_joystickCalls;
    if (g_joystickScriptPos < g_joystickScriptLen) return g_joystickScript[g_joystickScriptPos++];
    return kJoystickIdle;
}

void cleanup(void) { ++g_cleanupCalls; }
void restoreCbreakHandler(void) { ++g_restoreCbreakCalls; }

int main() {
    struct GameComm comm = {};

    resetInputState(comm);
    comm.setupUseJoy = kKeyboardOnly;
    waitForKeyOrJoy();
    require(g_getKeyCalls == kExpectedSingleCall && g_joystickCalls == kExpectedNoCalls &&
                g_cleanupCalls == kExpectedNoCalls,
            "waitForKeyOrJoy reads one key in original keyboard-only mode");

    resetInputState(comm);
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kNoKeyBuffered, kNoKeyBuffered};
        setCheckScript(keyStates, kTwoStepScript);
    }
    waitForKeyOrJoy();
    require(g_getKeyCalls == kExpectedSingleCall && g_joystickCalls == kExpectedNoCalls,
            "waitForKeyOrJoy falls through to keyboard when joystick mode sees buffered key sentinel");

    resetInputState(comm);
    comm.setupUseJoy = kJoystickEnabled;
    {
        const int keyStates[] = {kKeyBuffered, kKeyBuffered, kNoKeyBuffered, kNoKeyBuffered};
        const int joystickStates[] = {kJoystickIdle, kJoystickIdle};
        setCheckScript(keyStates, kFourStepScript);
        setJoystickScript(joystickStates, kTwoStepScript);
    }
    waitForKeyOrJoy();
    require(g_joystickCalls == kExpectedTwoCalls && g_getKeyCalls == kExpectedSingleCall,
            "waitForKeyOrJoy polls joystick while original key-buffer check stays nonzero");

    // The Alt-Q / quitFlag exit paths call exit(); observing them needs forked children (POSIX-only).
#if !defined(_WIN32)
    resetInputState(comm);
    g_getKeyResult = KEYCODE_ALTQ;
    pid_t child = fork();
    require(child >= 0, "test should be able to fork for Alt-Q exit behavior");
    if (child == 0) {
        waitForKeyOrJoy();
        std::exit(kTestFailureExitCode);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for Alt-Q exit child");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
            "waitForKeyOrJoy preserves original Alt-Q cleanup and exit path");

    resetInputState(comm);
    quitFlag = kQuitFlagSet;
    child = fork();
    require(child >= 0, "test should be able to fork for quitFlag exit behavior");
    if (child == 0) {
        waitForKeyOrJoy();
        std::exit(kTestFailureExitCode);
    }
    status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for quitFlag exit child");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
            "waitForKeyOrJoy preserves original quitFlag restore/exit path");

    resetInputState(comm);
    comm.setupUseJoy = kJoystickEnabled;
    quitFlag = kQuitFlagSet;
    {
        const int keyStates[] = {kKeyBuffered, kKeyBuffered, kKeyBuffered};
        const int joystickStates[] = {kKeyBuffered};
        setCheckScript(keyStates, kThreeStepScript);
        setJoystickScript(joystickStates, kExpectedSingleCall);
    }
    child = fork();
    require(child >= 0, "test should be able to fork for joystick buffered quit behavior");
    if (child == 0) {
        waitForKeyOrJoy();
        std::exit(kTestFailureExitCode);
    }
    status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for joystick buffered quit child");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
            "waitForKeyOrJoy preserves original joystick buffered goto-done quit path");
#endif

    std::cout << "end_input_behavior_tests passed\n";
    return 0;
}
