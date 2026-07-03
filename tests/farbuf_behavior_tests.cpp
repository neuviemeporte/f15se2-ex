#include "egdata.h"
#include "egtypes.h"

#include <cstdlib>
#include <iostream>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum FarbufOriginalConstant : int {
    kAircraftModelsOffset = AIRCRAFT_MODELS_OFFSET,
    kAircraftModelsBytes = 0x520C,
    kWorldByte = 0x12,
    kAircraftByte = 0x34,
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
    require(g_aircraftModels - g_world3dData == kAircraftModelsOffset,
            "g_aircraftModels keeps the original fixed offset from g_world3dData");

    g_world3dData[0] = static_cast<char>(kWorldByte);
    g_aircraftModels[0] = static_cast<char>(kAircraftByte);
    require(static_cast<unsigned char>(g_world3dData[0]) == kWorldByte &&
                static_cast<unsigned char>(g_world3dData[kAircraftModelsOffset]) == kAircraftByte,
            "g_world3dData exposes the original region buffer followed by aircraft models");

    g_aircraftModels[kAircraftModelsBytes - 1] = static_cast<char>(kAircraftByte);
    require(static_cast<unsigned char>(g_world3dData[kAircraftModelsOffset + kAircraftModelsBytes - 1]) ==
                    kAircraftByte,
            "g_world3dData includes the original full aircraft model buffer tail");

    std::cout << "farbuf_behavior_tests passed\n";
    return 0;
}
