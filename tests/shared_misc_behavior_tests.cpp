#include "common.h"
#include "inttype.h"

#include "dos_compat.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <iostream>

// Internal-access pattern: pull the shared misc translation unit in directly so
// the test can reach its file-scope helpers. pollJoystick/copyJoystickData moved
// to joystick.c (SDL gamepad) and are covered by the joystick/overlay tests.
#include "../src/shared/miscimpl.c"

void doNothing2(const char *msg, int a, int b, int c);
int loadOverlay(const char *filename);
int doFcbSearch(void);
void mystrcat(char *dst, const char *src);
int mystrlen(const char *s);
void nearmemset(void *dst, char val, int count);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SharedMiscOriginalConstant : int {
    kInterruptNumber = 0x10,
    kInputAl = 0x03,
    kInputAh = 0x00,
    kOverlayLoadSuccess = 0,
    kFcbSearchFailure = -1,
    kBufferFillByte = 0x5A,
    kBufferFillCount = 3,
    kLogBufferSize = 64,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

} // namespace

int main() {
    uint8 inRegs[0xe] = {};
    uint8 outRegs[0xe] = {};
    char text[32] = "F-15";
    char fill[5] = {};

    installCBreakHandler();
    restoreCbreakHandler();
    doNothing2("ignored", 0, 0, 0);

    inRegs[0] = kInputAl;
    inRegs[1] = kInputAh;
    intDispatch(kInterruptNumber, inRegs, outRegs);
    require(outRegs[0] == kInputAl && outRegs[1] == kInputAh,
            "intDispatch forwards AL/AH through the compatibility interrupt stub");

    require(loadOverlay("MISC.EXE") == kOverlayLoadSuccess,
            "loadOverlay preserves the original native rewrite success stub");
    require(doFcbSearch() == kFcbSearchFailure,
            "doFcbSearch preserves the original native rewrite failure stub");
    // getTimeOfDay is wall-clock derived (BIOS 18.2 Hz tick low word). A prior
    // regression stubbed it to a constant 0, which froze the RNG seed and
    // disabled night missions. Verify it tracks the clock, not a constant.
    {
        unsigned long t0 = static_cast<unsigned long>(std::time(nullptr));
        int actual = getTimeOfDay();
        unsigned long t1 = static_cast<unsigned long>(std::time(nullptr));
        int expected0 = static_cast<int>(
            static_cast<unsigned long>(static_cast<double>(t0) * 18.2065) & 0xFFFF);
        int expected1 = static_cast<int>(
            static_cast<unsigned long>(static_cast<double>(t1) * 18.2065) & 0xFFFF);
        require(actual == expected0 || actual == expected1,
                "getTimeOfDay derives the BIOS tick count from the wall clock");
    }

    mystrcat(text, "E");
    require(std::strcmp(text, "F-15E") == 0 &&
                mystrlen(text) == static_cast<int>(std::strlen(text)),
            "mystrcat/mystrlen preserve the original C library wrapper behavior");

    nearmemset(fill, static_cast<char>(kBufferFillByte), kBufferFillCount);
    require(static_cast<unsigned char>(fill[0]) == kBufferFillByte &&
                static_cast<unsigned char>(fill[1]) == kBufferFillByte &&
                static_cast<unsigned char>(fill[2]) == kBufferFillByte &&
                fill[3] == 0,
            "nearmemset preserves the original near-buffer memset wrapper behavior");

    std::remove("NOASM.LOG");
    miscdbg("debug line");
    {
        char logText[kLogBufferSize] = {};
        FILE *log = std::fopen("NOASM.LOG", "rb");
        require(log != nullptr, "miscdbg creates the original NOASM.LOG debug file");
        const size_t bytesRead = std::fread(logText, 1, sizeof(logText) - 1, log);
        std::fclose(log);
        std::remove("NOASM.LOG");
        require(bytesRead == std::strlen("debug line\r\n") &&
                    std::memcmp(logText, "debug line\r\n", bytesRead) == 0,
                "miscdbg appends the original CRLF-terminated debug line");
    }

    std::cout << "shared_misc_behavior_tests passed\n";
    return 0;
}
