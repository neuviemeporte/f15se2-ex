#include "dosfunc.h"
#include "dos.h"

#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

int test_intdos(union REGS *inregs, union REGS *outregs);
int test_intdosx(union REGS *inregs, union REGS *outregs, struct SREGS *segregs);

#define intdos test_intdos
#define intdosx test_intdosx
#include "../src/dosfunc.c"
#undef intdosx
#undef intdos

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum DosfuncBehaviorConstant : int {
    kParagraphCount = 3,
    kBytesPerParagraph = 16,
    kFillByte = 0xa5,
    kSeedRegisterAx = 0x1234,
    kSeedSegmentDs = 0xabcd,
    kBellCharacter = 7,
    kDosLoadFunction = 0x4B,
    kDosSysvarsFunction = 0x52,
    kDosLoadExec = 0,
    kDosLoadNoExec = 1,
    kDosLoadOverlay = 3,
    kDosInvalidFunction = 1,
    kDosFormatInvalid = 0x0B,
    kDosCarryClear = 0,
    kDosCarrySet = 1,
    kLoadSegment = 0x2345,
    kProgramPsp = 0x1111,
    kFcb1Offset = 0x5C,
    kFcb2Offset = 0x6C,
    kSysvarsSegment = 0x1357,
    kSysvarsOffset = 0x2468,
    kItoaDecimalValue = 1234,
    kItoaHexValue = 0x2a,
    kItoaUnsupportedBase = 8,
    kItoaBufferSize = 16,
    kTestFailureExitCode = 1,
};

int g_intdosCalls = 0;
int g_intdosxCalls = 0;
union REGS g_lastDosRegs = {};
struct SREGS g_lastDosSregs = {};
int g_nextDosCarry = kDosCarryClear;
int g_nextDosAx = 0;
int g_nextIntdosReturn = 0;
uint16 g_nextSysvarsEs = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetDosRecorder() {
    g_intdosCalls = 0;
    g_intdosxCalls = 0;
    std::memset(&g_lastDosRegs, 0, sizeof(g_lastDosRegs));
    std::memset(&g_lastDosSregs, 0, sizeof(g_lastDosSregs));
    g_nextDosCarry = kDosCarryClear;
    g_nextDosAx = 0;
    g_nextIntdosReturn = 0;
    g_nextSysvarsEs = 0;
}

} // namespace

int test_intdos(union REGS *inregs, union REGS *outregs) {
    ++g_intdosCalls;
    g_lastDosRegs = *inregs;
    *outregs = *inregs;
    outregs->x.cflag = static_cast<unsigned short>(g_nextDosCarry);
    outregs->x.ax = static_cast<unsigned short>(g_nextDosAx);
    return g_nextIntdosReturn;
}

int test_intdosx(union REGS *inregs, union REGS *outregs, struct SREGS *segregs) {
    ++g_intdosxCalls;
    g_lastDosRegs = *inregs;
    *outregs = *inregs;
    outregs->x.bx = static_cast<unsigned short>(g_nextDosAx);
    segregs->es = g_nextSysvarsEs;
    g_lastDosSregs = *segregs;
    return 0;
}

void log_error(const char *, ...) {}

int main() {
    union REGS inRegs = {};
    union REGS outRegs = {};
    struct SREGS segRegs = {};

    void *mem = dos_alloc(kParagraphCount);
    require(mem != nullptr,
            "native dos_alloc should return host memory for non-zero paragraphs");

    const int byteCount = kParagraphCount * kBytesPerParagraph;
    std::memset(mem, kFillByte, byteCount);
    const auto *bytes = static_cast<const unsigned char *>(mem);
    for (int i = 0; i < byteCount; ++i) {
        require(bytes[i] == kFillByte,
                "native dos_alloc memory should be writable for requested size");
    }

    inRegs.x.ax = kSeedRegisterAx;
    segRegs.ds = kSeedSegmentDs;
    require(intdos(&inRegs, &outRegs) == 0 &&
                intdosx(&inRegs, &outRegs, &segRegs) == 0 &&
                putch(kBellCharacter) == 0,
            "compat64 DOS interrupt and character stubs preserve native no-op return values");
    segread(&segRegs);
    require(segRegs.ds == kSeedSegmentDs,
            "compat64 segread preserves the caller-provided segment register snapshot");

    {
        char decimalBuf[kItoaBufferSize] = {};
        char hexBuf[kItoaBufferSize] = {};
        char unsupportedBuf[kItoaBufferSize] = "unchanged";
        require(std::strcmp(itoa(kItoaDecimalValue, decimalBuf, 10), "1234") == 0 &&
                    std::strcmp(itoa(kItoaHexValue, hexBuf, 16), "2a") == 0 &&
                    std::strcmp(itoa(kItoaDecimalValue, unsupportedBuf, kItoaUnsupportedBase), "") == 0,
                "compat64 itoa preserves original decimal, lowercase hex, and unsupported-base behavior");
    }

    resetDosRecorder();
    _psp = kProgramPsp;
    char commandTail[] = "/C F15";
    require(loadprog("EGAME.EXE", kLoadSegment, kDosLoadExec, commandTail) == 0 &&
                g_intdosCalls == 1 &&
                g_lastDosRegs.h.ah == kDosLoadFunction &&
                g_lastDosRegs.h.al == kDosLoadExec &&
                g_lastDosRegs.x.dx == 0 &&
                g_lastDosRegs.x.bx == 0 &&
                exeLoadParams.envSegment == 0 &&
                exeLoadParams.fcb1Offset == kFcb1Offset &&
                exeLoadParams.fcb1Segment == kProgramPsp &&
                exeLoadParams.fcb2Offset == kFcb2Offset &&
                exeLoadParams.fcb2Segment == kProgramPsp,
            "loadprog builds the original EXEC parameter block and DOS 4B00h request");

    resetDosRecorder();
    require(loadprog("EGAME.EXE", kLoadSegment, kDosLoadNoExec, commandTail) == 0 &&
                g_lastDosRegs.h.ah == kDosLoadFunction &&
                g_lastDosRegs.h.al == kDosLoadNoExec &&
                exeLoadParams.envSegment == 0,
            "loadprog preserves the original load-no-exec request shape");

    resetDosRecorder();
    require(loadprog("GFX.EXE", kLoadSegment, kDosLoadOverlay, commandTail) == 0 &&
                g_lastDosRegs.h.ah == kDosLoadFunction &&
                g_lastDosRegs.h.al == kDosLoadOverlay &&
                ovlLoadParams.segment == kLoadSegment &&
                ovlLoadParams.reloc == kLoadSegment,
            "loadprog builds the original overlay load parameter block");

    resetDosRecorder();
    g_nextDosCarry = kDosCarrySet;
    g_nextIntdosReturn = kDosFormatInvalid;
    require(loadprog("BAD.EXE", kLoadSegment, kDosLoadExec, commandTail) == kDosFormatInvalid,
            "loadprog returns the original DOS error code when carry is set");

    resetDosRecorder();
    require(loadprog("BAD.EXE", kLoadSegment, 0xff, commandTail) == kDosInvalidFunction &&
                g_intdosCalls == 0,
            "loadprog rejects unsupported load types before issuing an interrupt");

    resetDosRecorder();
    g_nextDosAx = kSysvarsOffset;
    g_nextSysvarsEs = kSysvarsSegment;
    void *sysvars = dos_sysvars();
    require(g_intdosxCalls == 1 &&
                g_lastDosRegs.h.ah == kDosSysvarsFunction &&
                reinterpret_cast<std::uintptr_t>(sysvars) ==
                    ((static_cast<std::uintptr_t>(kSysvarsSegment) << 16) |
                     static_cast<std::uintptr_t>(kSysvarsOffset)),
            "dos_sysvars returns the original ES:BX system-variable pointer");

    std::free(mem);
    std::cout << "dosfunc behavior tests passed\n";
    return 0;
}
