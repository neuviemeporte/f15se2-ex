// enfile.c RNG helpers. srandInit / enSeedRandom are real and tested directly.
// enSeedRandom is made deterministic by shadowing getTimeOfDay with a fixed
// value (link order lets this TU's definition win over shared/miscimpl.c).
#include "endata.h"
#include "inttype.h"
#include "shared/common.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>

extern void srandInit(int seed);
extern void enSeedRandom(void);

namespace {

enum EndFileOriginalConstant : int {
    kInitialRandSeed = 0x1111,
    kInitialRandState = 0x2222,
    kSeedValue = 0x3456,
    kTimeOfDaySeed = 0x4567,
};

int g_timeCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int getTimeOfDay(void) {
    ++g_timeCalls;
    return kTimeOfDaySeed;
}

int main() {
    randSeed = kInitialRandSeed;
    randState = kInitialRandState;
    srandInit(kSeedValue);
    require(randSeed == kSeedValue && randState == 0,
            "srandInit stores the original seed and resets randState");

    randSeed = kInitialRandSeed;
    randState = kInitialRandState;
    g_timeCalls = 0;
    enSeedRandom();
    require(randSeed == kTimeOfDaySeed && randState == 0 && g_timeCalls == 1,
            "enSeedRandom preserves original seedRandom behavior using getTimeOfDay");

    std::cout << "end_file_behavior_tests passed\n";
    return 0;
}
