#include "egdata.h"
#include "egtypes.h"
#include "struct.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// Reach egsys.c's file-local interpolation internals (camera/object snapshot +
// lerp helpers) directly. The rest of the game is linked from f15se2_core, so
// the externals these reference resolve to the real implementations.
#include "../src/egsys.c"

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EgSysInternalOriginalConstant : int {
    kTestFailureExitCode = 1,
    kFrameRateScalingFour = 4,
    kFrameRateScalingZero = 0,
    kHalfNumerator = 1,
    kHalfDenominator = 2,
    kTeleportGuard = 0x2000,
    kAliveFlag = 2,
    kSimObjectClampCount = 20,
    kProjectileSlot = 2,
    kProjectilePrevTtl = 10,
    kProjectileNextTtl = 9,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void seedCameraState(int base) {
    g_ViewX = base + 10;
    g_ViewY = base + 20;
    g_viewZ = static_cast<int16>(base + 30);
    g_ourHead = static_cast<int16>(base + 40);
    g_ourPitch = base + 50;
    g_ourRoll = static_cast<int16>(base + 60);
    g_viewX_ = static_cast<int16>(base + 70);
    g_viewY_ = static_cast<int16>(base + 80);
    g_crashCamX = static_cast<int16>(base + 90);
    g_crashCamY = static_cast<int16>(base + 100);
    g_crashCamZ = static_cast<int16>(base + 110);
    g_wreckX = static_cast<int16>(base + 120);
    g_wreckY = static_cast<int16>(base + 130);
    g_wreckAlt = static_cast<int16>(base + 140);
}

void clearObjects() {
    std::memset(g_simObjects, 0, sizeof(struct SimObject) * 20);
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 12);
}

} // namespace

int main() {
    CamSnapshot prev = {};
    CamSnapshot next = {};
    SimObjSnap simPrev[SIM_OBJ_MAX] = {};
    SimObjSnap simNext[SIM_OBJ_MAX] = {};
    ProjSnap projPrev[PROJ_MAX] = {};
    ProjSnap projNext[PROJ_MAX] = {};

    require(iabs32(-123456) == 123456 && iabs32(123456) == 123456,
            "iabs32 preserves signed absolute value behavior");
    require(lerpLinear(100, 200, kHalfNumerator, kHalfDenominator) == 150,
            "lerpLinear uses truncating integer interpolation");
    require(lerpAngle(0x0100, 0xFF00, kHalfNumerator, kHalfDenominator) == 0,
            "lerpAngle follows the original shortest signed 16-bit arc");

    g_frameRateScaling = kFrameRateScalingFour;
    require(simStepNsNow() == NS_PER_SEC / kFrameRateScalingFour,
            "simStepNsNow divides one second by frameRateScaling");
    g_frameRateScaling = kFrameRateScalingZero;
    require(simStepNsNow() == NS_PER_SEC,
            "simStepNsNow clamps zero scaling to one step per second");

    seedCameraState(1000);
    camCapture(&prev);
    seedCameraState(2000);
    camCapture(&next);
    camApplyInterp(&prev, &next, kHalfNumerator, kHalfDenominator);
    require(g_ViewX == 1510 &&
                g_ViewY == 1520 &&
                g_viewZ == 1530 &&
                g_ourPitch == 1550 &&
                g_viewX_ == 1570 &&
                g_crashCamZ == 1610 &&
                g_wreckX == 1620 &&
                g_wreckAlt == 1640,
            "camApplyInterp interpolates camera, map, crash, and wreck state");
    camRestore(&next);
    require(g_ViewX == 2010 &&
                g_viewY_ == 2080 &&
                g_wreckAlt == 2140,
            "camRestore restores the authoritative next camera snapshot");

    prev.head = 0x0100;
    next.head = 0xFF00;
    prev.pitch = 0x0100;
    next.pitch = 0xFF00;
    prev.roll = 0x0100;
    next.roll = 0xFF00;
    prev.wreckAlt = 100;
    next.wreckAlt = 100;
    camApplyInterp(&prev, &next, kHalfNumerator, kHalfDenominator);
    require(g_ourHead == 0 &&
                g_ourPitch == 0 &&
                g_ourRoll == 0,
            "camApplyInterp interpolates angles through 16-bit wraparound");

    prev.wreckX = 0;
    prev.wreckY = 0;
    prev.wreckAlt = 100;
    next.wreckX = kTeleportGuard;
    next.wreckY = 0;
    next.wreckAlt = 80;
    g_wreckX = 77;
    g_wreckY = 88;
    g_wreckAlt = 99;
    camApplyInterp(&prev, &next, kHalfNumerator, kHalfDenominator);
    require(g_wreckX == 77 &&
                g_wreckY == 88 &&
                g_wreckAlt == 99,
            "camApplyInterp skips wreck interpolation across teleport-sized jumps");

    clearObjects();
    g_groundUnitCount = -1;
    require(simObjCount() == 0, "simObjCount clamps negative object count to zero");
    g_groundUnitCount = 50;
    require(simObjCount() == kSimObjectClampCount,
            "simObjCount clamps object count to the snapshot array capacity");

    g_groundUnitCount = 1;
    g_simObjects[0].worldX = 1000;
    g_simObjects[0].worldY = 2000;
    g_simObjects[0].posX = 31;
    g_simObjects[0].posY = 62;
    g_simObjects[0].alt = 300;
    g_simObjects[0].heading.w = 0x0100;
    g_simObjects[0].pitch = 0x0200;
    g_simObjects[0].bank.w = 0x0300;
    g_simObjects[0].flags.b[0] = kAliveFlag;
    g_projectiles[kProjectileSlot].mapX = 100;
    g_projectiles[kProjectileSlot].mapY = 200;
    g_projectiles[kProjectileSlot].fineX = 100 << 5;
    g_projectiles[kProjectileSlot].fineY = 200 << 5;
    g_projectiles[kProjectileSlot].alt = 300;
    g_projectiles[kProjectileSlot].worldX = 0x0100;
    g_projectiles[kProjectileSlot].worldY = 0x0200;
    g_projectiles[kProjectileSlot].ttl = kProjectilePrevTtl;
    objCapture(simPrev, projPrev);

    g_simObjects[0].worldX = 1200;
    g_simObjects[0].worldY = 2400;
    g_simObjects[0].posX = 37;
    g_simObjects[0].posY = 75;
    g_simObjects[0].alt = 500;
    g_simObjects[0].heading.w = 0x0300;
    g_simObjects[0].pitch = 0x0400;
    g_simObjects[0].bank.w = 0x0500;
    g_simObjects[0].flags.b[0] = kAliveFlag;
    g_projectiles[kProjectileSlot].mapX = 300;
    g_projectiles[kProjectileSlot].mapY = 600;
    g_projectiles[kProjectileSlot].fineX = 300 << 5;
    g_projectiles[kProjectileSlot].fineY = 600 << 5;
    g_projectiles[kProjectileSlot].alt = 900;
    g_projectiles[kProjectileSlot].worldX = 0x0300;
    g_projectiles[kProjectileSlot].worldY = 0x0400;
    g_projectiles[kProjectileSlot].ttl = kProjectileNextTtl;
    objCapture(simNext, projNext);

    objApplyInterp(simPrev, simNext, projPrev, projNext, kHalfNumerator, kHalfDenominator);
    require(g_simObjects[0].worldX == 1100 &&
                g_simObjects[0].worldY == 2200 &&
                g_simObjects[0].posX == static_cast<uint16>(1100 >> 5) &&
                g_simObjects[0].posY == static_cast<uint16>(2200 >> 5) &&
                g_simObjects[0].alt == 400 &&
                g_simObjects[0].heading.w == 0x0200 &&
                g_projectiles[kProjectileSlot].mapX == 200 &&
                g_projectiles[kProjectileSlot].mapY == 400 &&
                g_projectiles[kProjectileSlot].alt == 600 &&
                g_projectiles[kProjectileSlot].worldX == 0x0200,
            "objApplyInterp interpolates alive objects and one-step projectiles");

    objRestore(simNext, projNext);
    require(g_simObjects[0].worldX == 1200 &&
                g_simObjects[0].posX == 37 &&
                g_projectiles[kProjectileSlot].mapX == 300 &&
                g_projectiles[kProjectileSlot].ttl == kProjectileNextTtl,
            "objRestore restores authoritative object and projectile snapshots");

    simPrev[0].alive = 0;
    g_simObjects[0].worldX = 777;
    objApplyInterp(simPrev, simNext, projPrev, projNext, kHalfNumerator, kHalfDenominator);
    require(g_simObjects[0].worldX == 777,
            "objApplyInterp skips inactive previous sim objects");
    simPrev[0].alive = 1;
    simPrev[0].worldX = 0;
    simPrev[0].worldY = 0;
    simNext[0].alive = 1;
    simNext[0].worldX = kTeleportGuard;
    simNext[0].worldY = 0;
    g_simObjects[0].worldX = 888;
    objApplyInterp(simPrev, simNext, projPrev, projNext, kHalfNumerator, kHalfDenominator);
    require(g_simObjects[0].worldX == 888,
            "objApplyInterp skips original teleport-sized object movement");
    projNext[kProjectileSlot].ttl = kProjectilePrevTtl;
    g_projectiles[kProjectileSlot].mapX = 555;
    objApplyInterp(simNext, simNext, projPrev, projNext, kHalfNumerator, kHalfDenominator);
    require(g_projectiles[kProjectileSlot].mapX == 555,
            "objApplyInterp skips projectiles whose ttl did not decrement by one");

    std::cout << "egsys_internal_behavior_tests passed\n";
    return 0;
}
