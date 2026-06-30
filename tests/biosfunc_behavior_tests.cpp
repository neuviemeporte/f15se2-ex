#include "inttype.h"

#include <cstdlib>
#include <iostream>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum BiosfuncBehaviorConstant : int {
    kLowMemorySegment = 0,
    kBiosDataAreaKeyFlagsOffset = 0x417,
    kLowMemoryBytes = 0x500,
    kKeyFlagsSeed = 0xffff,
    kTestFailureExitCode = 1,
};

uint8 g_lowMemory[kLowMemoryBytes] = {};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

} // namespace

// biosfunc.c computes the BIOS Data Area address with MK_FP(0, 0x417). A host
// process cannot safely map that low address, so this target redirects MK_FP
// before including the real implementation body.
#define POINTERS_H
#define NEAR
#define FAR
#define CDECL
#define MK_FP(segment, offset) \
    (g_lowMemory + (((unsigned long)(segment) << 16) | (unsigned long)(offset)))

#include "../src/biosfunc.c"

int main() {
    auto *keyFlags = reinterpret_cast<uint16 *>(
        MK_FP(kLowMemorySegment, kBiosDataAreaKeyFlagsOffset));

    *keyFlags = kKeyFlagsSeed;
    bios_clearkeyflags();

    require(*keyFlags == 0,
            "bios_clearkeyflags clears the BIOS keyboard flag word");

    std::cout << "biosfunc behavior tests passed\n";
    return 0;
}
