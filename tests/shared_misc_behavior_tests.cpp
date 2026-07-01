#include "common.h"
#include "inttype.h"

#include <dos.h>

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <iostream>

#include "../src/shared/miscimpl.c"

void pollJoystick(void);
void far copyJoystickData(uint8 *ptr);
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
    kTimeOfDayStubValue = 0,
    kBufferFillByte = 0x5A,
    kBufferFillCount = 3,
    kCopyUnchanged0 = 0x11,
    kCopyUnchanged1 = 0x22,
    kCopyUnchanged2 = 0x33,
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
    uint8 joyData[] = {kCopyUnchanged0, kCopyUnchanged1, kCopyUnchanged2};
    char text[32] = "F-15";
    char fill[5] = {};

    installCBreakHandler();
    restoreCbreakHandler();
    pollJoystick();
    doNothing2("ignored", 0, 0, 0);

    copyJoystickData(joyData);
    require(joyData[0] == kCopyUnchanged0 &&
                joyData[1] == kCopyUnchanged1 &&
                joyData[2] == kCopyUnchanged2,
            "copyJoystickData preserves the original no-op native rewrite behavior");

    inRegs[0] = kInputAl;
    inRegs[1] = kInputAh;
    intDispatch(kInterruptNumber, inRegs, outRegs);
    require(outRegs[0] == kInputAl && outRegs[1] == kInputAh,
            "intDispatch forwards AL/AH through the compatibility interrupt stub");

    require(loadOverlay("MISC.EXE") == kOverlayLoadSuccess,
            "loadOverlay preserves the original native rewrite success stub");
    require(doFcbSearch() == kFcbSearchFailure,
            "doFcbSearch preserves the original native rewrite failure stub");
    require(getTimeOfDay() == kTimeOfDayStubValue,
            "getTimeOfDay preserves the original native rewrite zero stub");

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
