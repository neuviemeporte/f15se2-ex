// Mission-generation behaviour, de-generated to the deterministic core.
//
// The original auto-generated test was a 2600-line all-spy harness that pinned
// the exact DOS I/O / gfx / pilot-roster call sequence of the whole START
// mission generator. That machinery is gone or rewritten (the comm-bridge
// world transfer became worldxfer, moveDst/setMoveDstComm7A were deleted) and
// the file/input paths block or need on-disk assets, so they can't run headless.
//
// What is still worth guarding is the pure math the generator relies on:
// distance approximation, bearing computation, value clamping, and the grid-
// reference formatting shown in the briefing. Those are deterministic (no RNG
// sensitivity, no I/O), so they link the real code and assert exact results.
#include "comm.h"
#include "const.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

int approxDistance(int dx, int dy);
int calcBearing(int dx, int dy);
int clampValue(int val, int lo, int hi);
char *formatGridRef(int16 wx, int16 wy, int16 theater);

namespace {

enum StartOriginalConstant : int {
    kLibyaTheater = 0,
    kNorthCapeTheater = 5,
    kBearingMask = 0xFFFF,
    // 45-degree diagonals: byte-exact outputs of the original bearing polynomial.
    kBearingNortheast = 0x204D,
    kBearingSoutheast = 0x5FB3,
    kBearingSouthwest = 0xA04D,
    kBearingNorthwest = 0xDFB3,
    kDistanceCap = 0x7FFF,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

int bearing16(int dx, int dy) { return calcBearing(dx, dy) & kBearingMask; }

// Reference reimplementation of formatGridRef (reads gameData->theater), used to
// cross-check the real function without pinning a hand-computed string.
std::string expectedGridRef(int16 wx, int16 wy, int theater) {
    char buf[5] = {};
    int gridOffX = 0, gridOffY = 0;
    switch (theater) {
    case 0: std::strcpy(buf, "TD00"); gridOffX = 6; gridOffY = 4; break;
    case 1: std::strcpy(buf, "JZ00"); break;
    case 2: std::strcpy(buf, "XV00"); break;
    case 3: std::strcpy(buf, "ES00"); break;
    case 4: std::strcpy(buf, "WX00"); break;
    case 5: std::strcpy(buf, "CC00"); gridOffX = 3; gridOffY = 5; break;
    case 6: std::strcpy(buf, "HZ00"); break;
    default: return "";
    }
    int gx = (((wx >> WORLD_COORD_SHIFT) * 20) >> 10) + gridOffX;
    while (gx > 9) { gx -= 10; buf[0]++; }
    buf[2] += static_cast<char>(gx);
    int gy = (((wy >> WORLD_COORD_SHIFT) * 20) >> 10) + gridOffY;
    while (gy > 9) { gy -= 10; buf[1]--; }
    buf[3] += static_cast<char>(9 - gy);
    return buf;
}

std::string gridRef(int16 wx, int16 wy, int theater, struct Game &game) {
    game.theater = static_cast<int16>(theater);
    gameData = &game;
    return std::string(formatGridRef(wx, wy, static_cast<int16>(theater)));
}

} // namespace

int main() {
    // approxDistance: half the shorter axis plus the longer, capped at 0x7fff.
    require(approxDistance(30, 40) == 55 && approxDistance(40, 30) == 55,
            "approxDistance is symmetric in its axes");
    require(approxDistance(-30, -40) == 55,
            "approxDistance ignores sign");
    require(approxDistance(30000, 30000) == kDistanceCap,
            "approxDistance saturates at the original 0x7fff ceiling");

    // calcBearing: cardinal directions land on the exact 16-bit compass points.
    require(bearing16(0, 1) == BEARING_NORTH, "calcBearing north");
    require(bearing16(1, 0) == BEARING_EAST, "calcBearing east");
    require(bearing16(0, -1) == BEARING_SOUTH, "calcBearing south");
    require(bearing16(-1, 0) == BEARING_WEST, "calcBearing west");
    // 45-degree diagonals reproduce the original bearing polynomial per quadrant.
    require(bearing16(100, 100) == kBearingNortheast, "calcBearing northeast diagonal");
    require(bearing16(100, -100) == kBearingSoutheast, "calcBearing southeast diagonal");
    require(bearing16(-100, -100) == kBearingSouthwest, "calcBearing southwest diagonal");
    require(bearing16(-100, 100) == kBearingNorthwest, "calcBearing northwest diagonal");

    // clampValue: hi cap, pass-through, lo floor, and the original underflow wrap.
    require(clampValue(20, -10, 10) == 10, "clampValue caps above hi");
    require(clampValue(5, -10, 10) == 5, "clampValue passes in-range values");
    require(clampValue(-20, -10, 10) == -10, "clampValue floors above the -16384 wrap");
    require(clampValue(-16384, -10, 10) == 10,
            "clampValue wraps to hi at/below the original -0x4000 threshold");

    // formatGridRef: briefing grid label, cross-checked against the reference.
    struct Game game = {};
    require(gridRef(0x2000, 0x3000, kLibyaTheater, game) ==
                expectedGridRef(0x2000, 0x3000, kLibyaTheater),
            "formatGridRef matches the Libya grid layout");
    require(gridRef(0, 0, kLibyaTheater, game) ==
                expectedGridRef(0, 0, kLibyaTheater),
            "formatGridRef handles the Libya origin cell");
    require(gridRef(0x2000, 0x3000, kNorthCapeTheater, game) ==
                expectedGridRef(0x2000, 0x3000, kNorthCapeTheater),
            "formatGridRef matches the North Cape grid layout");

    std::cout << "start_behavior_tests passed\n";
    return 0;
}
