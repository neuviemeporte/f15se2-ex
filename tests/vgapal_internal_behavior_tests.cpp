#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

// Include the C library headers before replacing abort(), so the macro does not
// rewrite system declarations while vgapal.c is pulled into this test unit.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct VgapalAbort {};

[[noreturn]] void vgapalAbortTrap() {
    throw VgapalAbort{};
}

} // namespace

#define main vgapal_internal_program_main
#define abort() vgapalAbortTrap()
#include "../src/vgapal.c"
#undef abort
#undef main

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum VgapalInternalConstant : int {
    kPaletteEntryCount = 256,
    kPaletteComponentCount = kPaletteEntryCount * 3,
    kClassicVgaMaxChannel = 63,
    kFirstInvalidClassicChannel = 64,
    kInvalidPaletteIndex = -1,
    kInvalidRunStartState = 8,
    kInvalidRunChannel = 3,
    kExpectedSuccess = 0,
    kExpectedFailure = 1,
    kTestFailureExitCode = 1,
    kStdoutFd = 1,
    kStderrFd = 2,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetPaletteState() {
    std::memset(pal, 0, sizeof(pal));
    pal_written = 0;
}

template <typename Fn>
void expectAbort(Fn fn, const char *message) {
    bool aborted = false;
    try {
        fn();
    } catch (const VgapalAbort &) {
        aborted = true;
    }
    require(aborted, message);
}

void silenceChildOutput() {
    FILE *out = std::freopen("/dev/null", "w", stdout);
    FILE *err = std::freopen("/dev/null", "w", stderr);
    require(out != nullptr && err != nullptr,
            "child should be able to silence vgapal stdout/stderr");
}

void runIsolated(void (*fn)(), const char *message) {
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for fresh vgapal static state");
    if (child == 0) {
        silenceChildOutput();
        fn();
        std::exit(kExpectedSuccess);
    }

    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for isolated vgapal case");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kExpectedSuccess, message);
}

void levelUpRejectsIncompletePalette() {
    resetPaletteState();
    expectAbort([] {
        levelUp();
    }, "levelUp preserves the original full-palette precondition");
}

void levelUpRejectsAlreadyUpconvertedComponent() {
    resetPaletteState();
    pal_written = kPaletteEntryCount;
    pal[0] = kFirstInvalidClassicChannel;
    expectAbort([] {
        levelUp();
    }, "levelUp preserves the original 64-level component precondition");
}

void levelUpRejectsSecondCall() {
    resetPaletteState();
    pal_written = kPaletteEntryCount;
    levelUp();
    expectAbort([] {
        levelUp();
    }, "levelUp preserves the original one-shot conversion guard");
}

void mainRejectsInvalidArgcArgv() {
    expectAbort([] {
        (void)vgapal_internal_program_main(0, nullptr);
    }, "vgapal main preserves the original invalid argc/argv abort");
}

void mainRejectsNullLevelsPointer() {
    char program[] = "vgapal";
    char *argv[] = {program, nullptr};
    expectAbort([&] {
        (void)vgapal_internal_program_main(2, argv);
    }, "vgapal main preserves the original null levels-argument abort");
}

} // namespace

int main() {
    resetPaletteState();
    expectAbort([] {
        addColor(kInvalidPaletteIndex, 0, 0);
    }, "addColor preserves the original component-range guard");

    resetPaletteState();
    pal_written = kPaletteEntryCount;
    expectAbort([] {
        addColor(0, 0, 0);
    }, "addColor preserves the original palette-capacity guard");

    resetPaletteState();
    pal_written = kInvalidPaletteIndex;
    expectAbort([] {
        addColor(0, 0, 0);
    }, "addColor preserves the original negative write-index guard");

    resetPaletteState();
    expectAbort([] {
        add16Color(0, 0, 0, kFirstInvalidClassicChannel);
    }, "add16Color preserves the original 64-level argument guard");

    resetPaletteState();
    expectAbort([] {
        (void)addRun(kInvalidRunStartState, 1, 0, 0, 0, 0, kClassicVgaMaxChannel);
    }, "addRun preserves the original start-state guard");

    resetPaletteState();
    expectAbort([] {
        (void)addRun(0, kInvalidRunChannel, 0, 0, 0, 0, kClassicVgaMaxChannel);
    }, "addRun preserves the original one-channel guard");

    resetPaletteState();
    expectAbort([] {
        (void)addRun(0, 1, 0, 0, 0, 0, kFirstInvalidClassicChannel);
    }, "addRun preserves the original 64-level argument guard");

    resetPaletteState();
    expectAbort([] {
        addCycle(0, 0, 0, 0, kFirstInvalidClassicChannel);
    }, "addCycle preserves the original 64-level argument guard");

    resetPaletteState();
    expectAbort([] {
        printPalette(0);
    }, "printPalette preserves the original full-palette precondition");

    runIsolated(levelUpRejectsIncompletePalette,
                "isolated levelUp incomplete-palette case should pass");
    runIsolated(levelUpRejectsAlreadyUpconvertedComponent,
                "isolated levelUp invalid-component case should pass");
    runIsolated(levelUpRejectsSecondCall,
                "isolated levelUp one-shot case should pass");
    runIsolated(mainRejectsInvalidArgcArgv,
                "isolated vgapal invalid argc/argv case should pass");
    runIsolated(mainRejectsNullLevelsPointer,
                "isolated vgapal null levels-argument case should pass");

    // Keep one non-aborting internal run here too: it proves the direct include
    // uses the same public builder/printer path as the CLI behavior tests.
    resetPaletteState();
    buildPalette();
    require(pal_written == kPaletteEntryCount &&
                static_cast<int>(sizeof(pal)) == kPaletteComponentCount,
            "buildPalette preserves the original full 256-entry palette size");

    std::cout << "vgapal internal behavior tests passed\n";
    return kExpectedSuccess;
}
