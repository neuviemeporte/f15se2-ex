#include "egcode.h"
#include "egdata.h"
#include "egmath.h"
#include "egflight.h"
#include "comm.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

extern const int16 g_angleLut[];
extern int rangeApprox(int deltaX, int deltaY);
extern int sinMul(int angle, int value);
extern int randomRange(int maxVal);
extern unsigned signedRatio16(int numerator, int denominator);
extern int valueToAngle(int value);
extern int complementAngle(int value);
extern int FAR CDECL hudSine(int angle);
extern int FAR CDECL hudPitchScale(int ap);
extern void UpdateThrottleState(void);
extern void drawFuelGauge(void);
extern void drawVectorShape(const int16 *shapeData);
extern void applyRotationDelta(const int16 *matA, const int16 *matB);
extern void computeAttitudeAngles(void);
extern void rebuildOrientation(void);
extern void waitForKeyPress(void);
extern void renderFrame(void);
extern void stepFlightModel(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum OriginalMathConstant : int {
    kAngleQuarterTurn = 0x4000,
    kAngleHalfTurn = 0x8000,
    kAngleThreeQuarterTurn = 0xC000,
    kAngleFractionMask = 0xFF,
    kSineRound = 0x80,
    kWordDegreeStep = 0x100,
    kAsinTableShift = 9,
    kQ15ProductShift = 15,
    kQ15RoundBitShift = 14,
    kRangeMax = 0x7FFF,
    kRandomScaleShift = 15,
    kClampWrapFloor = -0x4000,
    kBearingCurveBase = 0x2800,
    kBearingCurveCenter = 0x1333,
    kBearingCurveScale = 0x0B00,
    kHudThrottleX1 = 212,
    kHudThrottleTop = 127,
    kHudThrottleX2 = 222,
    kHudThrottleBottom = 175,
    kHudThrottleNormalColor = 0x0c,
    kHudThrottleAfterburnerColor = 0x0e,
    kHudFuelX1 = 5,
    kHudFuelTop = 109,
    kHudFuelX2 = 10,
    kHudFuelBottom = 152,
    kHudFuelNormalColor = 2,
    kHudFuelLowColor = 14,
    kHudFuelLowThreshold = 2000,
    kHudClearColor = 0,
    kThrottleMilitary = 90,
    kThrottleAfterburner = 120,
    kFuelNormal = 3000,
    kFuelLow = 1000,
    kGaugeThrottleDivisor = 3,
    kGaugeFuelDivisor = 250,
    kQ15Identity = 32767,
    kQ15ZeroAngleProduct = 32766,
    kRotationDirtyPeriod = 8,
    kShapeColorIndex = 3,
    kShapeColorValue = 12,
    kShapeStartX = 10,
    kShapeStartY = 20,
    kShapeEndX = 30,
    kShapeEndY = 40,
    kShapeTerminator = -1,
    kPauseKey = 0x1900,
    kResumeKey = 0x1234,
    kSavedFrameTiming = 77,
    kRenderViewX = 0x1000,
    kRenderViewY = 0x2000,
    kRenderViewZ = 300,
    kRenderHead = 0x1111,
    kRenderPitch = -0x0222,
    kRenderRoll = 0x0333,
    kRenderRearViewKey = 0x41,
    kRenderLeftViewKey = 0x42,
    kRenderRightViewKey = 0x43,
    kRenderLeftExternalKey = 0x85,
    kRenderNorthExternalKey = 0x86,
    kRenderChaseExternalKey = 0x87,
    kRenderTargetRearKey = 0x88,
    kRenderDirectorKey = 0x89,
    kRenderTargetOrbitKey = 0x8b,
    kRenderTrailingViewKey = 0x84,
    kRenderTrailingFrameTick = 9,
    kRenderTrailingFrameScale = 4,
    kRenderTrailingPreviousSlot = 6,
    kRenderTrailingCurrentSlot = 7,
    kRenderTrailingHalfAlphaQ12 = 0x0800,
    kRenderExternalCamMin = 2,
    kRenderInitialCamDist = 1,
    kRenderHudVisible = 1,
    kRenderHudBottom = 97,
    kRenderHiddenHudBottom = 200,
    kRenderSidePanelBottom = 96,
    kRenderWorldFlipBase = 0x100000,
    kRenderExternalOffsetBase = 0x18,
    kRenderChaseAltBase = 4,
    kRenderLowDetailGroundColor = 3,
    kRenderLowDetailSkyColor = 0x0b,
    kRenderMissileSlot = 2,
    kRenderMissileTtl = 5,
    kRenderMissileMapX = 100,
    kRenderMissileMapY = 120,
    kRenderMissileAlt = 300,
    kRenderSimObjectViewBase = 0x20,
    kRenderGroundTargetViewBase = 0x40,
    kRenderSimObjectDeltaX = 400,
    kRenderSimObjectDeltaY = 800,
    kRenderSimObjectDeltaAlt = 64,
    kRenderGroundWeaponType = 2,
    kRenderGroundTargetMapY = 140,
    kRenderCarrierTargetAlt = 200,
    kRenderCarrierDeckMinAlt = 132,
    kRenderProjectileHighPitch = 0x5000,
    kRenderProjectilePitchAfterFlip = 0x3400,
    kRenderProjectileHeadingAfterFlip = 0x9111,
    kRenderCrashViewKey = 0x8c,
    kRenderCrashCamX = 77,
    kRenderCrashCamY = 88,
    kRenderCrashCamZ = 99,
    kRenderCrashViewPitch = 0xf400,
    kRenderEgaMode = 2,
    kStepFrameScale = 10,
    kStepKeyboardCenter = 128,
    kStepFuelStart = 3000,
    kStepTheaterDesertStorm = 6,
    kStepTheaterOdd = 1,
    kStepTheaterEven = 0,
    kStepDifficultyNormal = 1,
    kStepBrakeFlag = 8,
    kStepGearFlag = 1,
    kStepCarrierFlag = 0x200,
    kStepCarrierHeadingHighByte = 0x84,
    kStepFullThrottle = 100,
    kStepAxisMax = 255,
    kKbhitNoKey = 0,
    kKbhitHasKey = 1,
    kStepKeyThrottleDown = 0x0C2D,
    kStepKeyThrottleUp = 0x0D3D,
    kStepKeyShiftThrottleUp = 0x0D2B,
    kStepKeyAfterburner = 0x1E61,
    kStepKeyShiftMinus = 0x0C5F,
    kStepKeyBrakeToggle = 0x3062,
    kStepKeyAltJoystick = 0x2400,
    kStepKeyAltQuit = 0x1000,
    kStepKeyAltBriefing = 0x3000,
    kStepThrottleDelta = 10,
    kStepThrottleLowDelta = 5,
    kStepAfterburnerThrottle = 0x90,
    kStepJoystickCalibCooldown = 40,
    kStepThrottleSound = 14,
    kStepBrakeReleaseSound = 28,
    kStepLandingGearSound = 32,
    kStepThrottleCutSound = 16,
    kExpectedTwoCalls = 2,
    kStepSnapshotSlot = 0,
    kStepSnapshotRingCount = 16,
    kAttitudeWideComponent = 0x6000,
    kAttitudeNarrowComponent = 0x0400,
    kAttitudePitchQuarterTurnInput = -32767,
    kStepGunHitsThrottleLimit = 20,
    kStepGunHitsClampedThrottle = 0x90 - kStepGunHitsThrottleLimit * 4,
    kStepNoFuelBurnFrame = 1,
    kStepFuelDepletionStart = 1,
    kStepFuelBurnFrame = kStepFrameScale * 2,
    kStepEjectGroundState = 2,
    kStepEjectCrashSound = 2,
    kStepSmokeHitTimer = -8,
    kStepAutopilotTargetX = 120,
    kStepAutopilotTargetY = 150,
    kStepAutopilotViewX = 100,
    kStepAutopilotViewY = 100,
    kStepAutopilotAltitude = 1500,
    kStepAutopilotViewZ = 1000,
    kStepAutopilotMissionTick = 5,
    kStepAutopilotWaypointBearing = 0x1000,
    kStepAutopilotKnots = 400,
    kStepAutopilotThrottleMin = 35,
    kStepAutopilotThrottleMax = 80,
    kStepWorldCoordShift = 5,
    kStepAutopilotFarHeading = 0x7000,
    kStepLandingGroundAlt = 100,
    kStepGroundBrakeSlowVelocity = 500,
    kStepGunHitsThrottleFloor = 40,
    kStepAamLockedTargetX = 34,
    kStepAamLockedTargetY = 56,
    kStepHighAltitudeMid = 0x3000,
    kStepHighAltitudeTop = 0x5000,
    kStepLandingTouchdownSound = 12,
    kStepLandingCrashSound = 0,
    kStepLandingCrashOutcome = 5,
};

struct RectCall {
    int x1;
    int y1;
    int x2;
    int y2;
};

int g_testDrawColor = -1;
int g_testSetDrawColorCalls = 0;
RectCall g_testRects[8] = {};
int g_testRectCalls = 0;
int g_testGfxColor = -1;
int g_testGfxSetColorCalls = 0;
int g_testResetSpanCalls = 0;
int g_testClipEdgeCalls = 0;
int g_testFlushSpanCalls = 0;
int g_testAudioEngineOffCalls = 0;
int g_testUpdateEngineSoundCalls = 0;
int g_testLoadPaletteCalls = 0;
int g_testRender3DCalls = 0;
int g_testRenderCamX = 0;
int g_testRenderCamY = 0;
int g_testRenderCamZ = 0;
long g_testRenderWorldX = 0;
long g_testRenderWorldY = 0;
long g_testRenderWorldZ = 0;
int g_testRenderClipHeight = 0;
int g_testBuildRotationCalls = 0;
int g_testBuildAngleX = 0;
int g_testBuildAngleY = 0;
int g_testBuildAngleZ = 0;
int g_testCopyRectCalls = 0;
int g_testModeCode = 3;
int g_testWeaponAmmoCalls = 0;
int g_testWeaponMarkerCalls = 0;
int g_testLastWeaponMarker = -1;
int g_testRedrawTacMapCalls = 0;
int g_testPanelBoxCalls = 0;
int g_testBlitCalls = 0;
char g_testLastPicName[32] = {};
int g_testMakeSoundCalls = 0;
int g_testLastSoundId = -1;
int g_testLastSoundPriority = -1;
int g_testEnginePitchCalls = 0;
int g_testLastEngineKnots = -1;
int g_testLastEngineThrust = -1;
int g_testTempMessageCalls = 0;
char g_testLastTempMessage[64] = {};
int g_testJoystickCalibCalls = 0;
int g_testReadJoystickCalls = 0;
int g_testFinalizeMissionCalls = 0;
int g_testFinalizeMissionOutcome = -1;
int g_testExitSlowMotionCalls = 0;
int g_testKeys[4] = {};
int g_testKeyCount = 0;
int g_testKeyIndex = 0;
int g_testKbhitResults[8] = {};
int g_testKbhitCount = 0;
int g_testKbhitIndex = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void resetFlightDrawRecorders() {
    g_testDrawColor = -1;
    g_testSetDrawColorCalls = 0;
    std::memset(g_testRects, 0, sizeof(g_testRects));
    g_testRectCalls = 0;
    g_testGfxColor = -1;
    g_testGfxSetColorCalls = 0;
    g_testResetSpanCalls = 0;
    g_testClipEdgeCalls = 0;
    g_testFlushSpanCalls = 0;
    g_lineX1 = g_lineY1 = g_lineX2 = g_lineY2 = 0;
}

void resetFlightInputRecorders() {
    g_testAudioEngineOffCalls = 0;
    g_testUpdateEngineSoundCalls = 0;
    std::memset(g_testKeys, 0, sizeof(g_testKeys));
    g_testKeyCount = 0;
    g_testKeyIndex = 0;
    std::memset(g_testKbhitResults, 0, sizeof(g_testKbhitResults));
    g_testKbhitCount = 0;
    g_testKbhitIndex = 0;
}

void resetRenderRecorders() {
    g_testLoadPaletteCalls = 0;
    g_testRender3DCalls = 0;
    g_testRenderCamX = g_testRenderCamY = g_testRenderCamZ = 0;
    g_testRenderWorldX = g_testRenderWorldY = g_testRenderWorldZ = 0;
    g_testRenderClipHeight = 0;
    g_testBuildRotationCalls = 0;
    g_testBuildAngleX = g_testBuildAngleY = g_testBuildAngleZ = 0;
    g_testCopyRectCalls = 0;
    g_testModeCode = 3;
    g_testWeaponAmmoCalls = 0;
    g_testWeaponMarkerCalls = 0;
    g_testLastWeaponMarker = -1;
    g_testRedrawTacMapCalls = 0;
    g_testPanelBoxCalls = 0;
    g_testBlitCalls = 0;
    std::memset(g_testLastPicName, 0, sizeof(g_testLastPicName));
}

void resetStepRecorders() {
    g_testMakeSoundCalls = 0;
    g_testLastSoundId = -1;
    g_testLastSoundPriority = -1;
    g_testEnginePitchCalls = 0;
    g_testLastEngineKnots = -1;
    g_testLastEngineThrust = -1;
    g_testTempMessageCalls = 0;
    g_testLastTempMessage[0] = '\0';
    g_testJoystickCalibCalls = 0;
    g_testReadJoystickCalls = 0;
    g_testFinalizeMissionCalls = 0;
    g_testFinalizeMissionOutcome = -1;
    g_testExitSlowMotionCalls = 0;
    resetFlightDrawRecorders();
}

int sar32(int32 value, int count) {
    return value >= 0
        ? static_cast<int>(static_cast<uint32>(value) >> count)
        : -static_cast<int>((static_cast<uint32>(-value) + ((1u << count) - 1u)) >> count);
}

int expectedSine(int angle) {
    const unsigned wrapped = static_cast<uint16>(angle);
    const int idx = (wrapped >> 8) & kAngleFractionMask;
    const int frac = wrapped & kAngleFractionMask;
    const int v0 = g_angleLut[idx];
    const int v1 = g_angleLut[idx + 1];
    const int32 step = static_cast<int32>(v1 - v0) * frac;
    return v0 + sar32(step + kSineRound, 8);
}

int expectedFixedMulQ14(int a, int b) {
    const int32 p = static_cast<int32>(a) * static_cast<int32>(b);
    return sar32(p, kQ15ProductShift) + (sar32(p, kQ15RoundBitShift) & 1);
}

int expectedAbs16(int value) {
    /* Original DOS int abs(-32768) overflows and leaves the value negative. */
    const int wordValue = static_cast<int16>(value);
    return wordValue == static_cast<int16>(0x8000) ? wordValue : std::abs(wordValue);
}

int expectedClampRange(int value, int minVal, int maxVal) {
    if (value > maxVal) return maxVal;
    if (value >= minVal) return value;
    return value <= kClampWrapFloor ? maxVal : minVal;
}

int expectedRangeApprox(int dx, int dy) {
    dx = expectedAbs16(dx);
    dy = expectedAbs16(dy);
    const int32 dist = dx > dy ? static_cast<int32>(dy >> 1) + dx
                               : static_cast<int32>(dx >> 1) + dy;
    return dist > kRangeMax ? kRangeMax : static_cast<int>(dist);
}

int expectedBearing(int deltaX, int deltaY) {
    if (deltaX == 0) return deltaY > 0 ? 0 : kAngleHalfTurn;
    if (deltaY == 0) return deltaX > 0 ? kAngleQuarterTurn : kAngleThreeQuarterTurn;

    const int absX = expectedAbs16(deltaX);
    const int absY = expectedAbs16(deltaY);
    const bool swapped = absX > absY;
    const int32 numer = static_cast<int32>(swapped ? absY : absX) << 14;
    const int denom = swapped ? absX : absY;
    const int ratio = static_cast<int>(numer / denom);
    const int angle = static_cast<int>(
        ((kBearingCurveBase - (((long)std::abs(kBearingCurveCenter - ratio) * kBearingCurveScale) >> 14)) *
         static_cast<long>(ratio)) >> 14);

    if (deltaX > 0) {
        return deltaY > 0 ? (swapped ? kAngleQuarterTurn - angle : angle)
                          : (swapped ? angle + kAngleQuarterTurn : kAngleHalfTurn - angle);
    }
    return deltaY > 0 ? (swapped ? angle + kAngleThreeQuarterTurn : -angle)
                      : (swapped ? kAngleThreeQuarterTurn - angle : angle + kAngleHalfTurn);
}

uint16 expectedSignedRatio16(int numerator, int denominator) {
    const bool negative = (numerator < 0) != (denominator < 0);
    const uint32 absNumerator = static_cast<uint32>(numerator < 0 ? -numerator : numerator);
    const uint32 absDenominator = static_cast<uint32>(denominator < 0 ? -denominator : denominator);
    const uint16 magnitude = static_cast<uint16>(((absNumerator << 16) / absDenominator) >> 1);
    return negative ? static_cast<uint16>(-static_cast<int>(magnitude)) : magnitude;
}

int expectedValueToAngle(int value) {
    if (value == 0x8000) return kAngleThreeQuarterTurn;
    const int magnitude = std::abs(value);
    int angle = 0;
    for (int tableIndex = (magnitude >> kAsinTableShift) + 1; tableIndex >= 0; --tableIndex) {
        if (g_angleLut[tableIndex] <= magnitude) {
            const int tableSpan = g_angleLut[tableIndex + 1] - g_angleLut[tableIndex];
            angle = static_cast<int>((static_cast<long>(magnitude - g_angleLut[tableIndex]) *
                                      kWordDegreeStep) /
                                     static_cast<long>(tableSpan)) +
                    tableIndex * kWordDegreeStep;
            break;
        }
    }
    return value < 0 ? -angle : angle;
}

int expectedIsqrt(int value) {
    value = expectedAbs16(value);
    if (value < 4) return 1;
    int guess = value >> 2;
    int quotient = 0;
    do {
        quotient = value / guess;
        guess = (guess + quotient) >> 1;
    } while (expectedAbs16(guess - quotient) > 1);
    return guess;
}

} // namespace

void setDrawColor(int color) {
    g_testDrawColor = color;
    ++g_testSetDrawColorCalls;
}

void fillRectBoth(int x1, int y1, int x2, int y2) {
    require(g_testRectCalls < static_cast<int>(sizeof(g_testRects) / sizeof(g_testRects[0])),
            "test rectangle recorder capacity");
    g_testRects[g_testRectCalls++] = {x1, y1, x2, y2};
}

void FAR CDECL gfx_setColor(int color) {
    g_testGfxColor = color;
    ++g_testGfxSetColorCalls;
}

int FAR CDECL resetScanlineSpans(void) {
    ++g_testResetSpanCalls;
    return 0;
}

int FAR CDECL clipAndRasterizeEdge(void) {
    ++g_testClipEdgeCalls;
    return 0;
}

int FAR CDECL flushSpanDirtyRect(void) {
    ++g_testFlushSpanCalls;
    return 0;
}

void FAR CDECL gfx_waitRetrace(void) {}
void FAR CDECL gfx_nop23(void) {}
int FAR CDECL gfx_getModecode(void) { return g_testModeCode; }
int FAR CDECL gfx_curPage(void) { return 0; }
void FAR CDECL gfx_copyRect(int, uint16, uint16, int, uint16, uint16, int, int) {
    ++g_testCopyRectCalls;
}
void FAR CDECL gfx_drawLine(uint16, uint16, uint16, uint16) {}
void openBlitClosePic(const char *name, int) {
    std::strncpy(g_testLastPicName, name, sizeof(g_testLastPicName) - 1);
}
void drawWeaponAmmo(void) { ++g_testWeaponAmmoCalls; }
void drawWeaponSelectMarker(int weaponIdx) {
    ++g_testWeaponMarkerCalls;
    g_testLastWeaponMarker = weaponIdx;
}
void redrawTacMap(int, int) { ++g_testRedrawTacMapCalls; }
void fillPanelBox(int, int) { ++g_testPanelBoxCalls; }
void blitSprite(int, int, int, int, int, int, int) { ++g_testBlitCalls; }
void loadColorPalette(int) { ++g_testLoadPaletteCalls; }
void render3DView(int camX, int camY, int camZ, long worldX, long worldY, long worldZ,
                  int, int, int, int clipHeight) {
    ++g_testRender3DCalls;
    g_testRenderCamX = camX;
    g_testRenderCamY = camY;
    g_testRenderCamZ = camZ;
    g_testRenderWorldX = worldX;
    g_testRenderWorldY = worldY;
    g_testRenderWorldZ = worldZ;
    g_testRenderClipHeight = clipHeight;
}

int FAR buildRotationMatrixFar(int16 *matrix, int angleX, int angleY, int angleZ) {
    // This target isolates egflight.c; the full matrix builder is covered in
    // eg3drast tests. The recorder lets renderFrame camera tests verify which
    // original view angles are passed without duplicating matrix math here.
    ++g_testBuildRotationCalls;
    g_testBuildAngleX = angleX;
    g_testBuildAngleY = angleY;
    g_testBuildAngleZ = angleZ;
    std::memset(matrix, 0, sizeof(int16) * 9);
    matrix[0] = matrix[4] = matrix[8] = kQ15ZeroAngleProduct;
    return 0;
}

int FAR multiplyMatrix3x3Far(const int16 *matA, const int16 *matB, int16 *result) {
    // This target isolates egflight.c side effects; full matrix multiplication
    // behavior is covered by eg3drast_internal_behavior_tests.
    (void)matA;
    (void)matB;
    std::memset(result, 0, sizeof(int16) * 9);
    result[0] = result[4] = result[8] = kQ15ZeroAngleProduct;
    return 0;
}

int FAR CDECL audio_engineDroneOff(void) {
    ++g_testAudioEngineOffCalls;
    return 0;
}

int FAR CDECL audio_setEnginePitch(int knots, int thrust) {
    ++g_testEnginePitchCalls;
    g_testLastEngineKnots = knots;
    g_testLastEngineThrust = thrust;
    return 0;
}

int FAR initJoystickCalibration(void) {
    ++g_testJoystickCalibCalls;
    return 0;
}
int FAR readCalibratedJoystick(void) {
    ++g_testReadJoystickCalls;
    return 0;
}
void exitSlowMotion(void) { ++g_testExitSlowMotionCalls; }
void finalizeMission(int outcome) {
    ++g_testFinalizeMissionCalls;
    g_testFinalizeMissionOutcome = outcome;
}
void waitFrameSync(int) {}
void tempStrcpy(const char *message) {
    ++g_testTempMessageCalls;
    std::strncpy(g_testLastTempMessage, message, sizeof(g_testLastTempMessage) - 1);
    g_testLastTempMessage[sizeof(g_testLastTempMessage) - 1] = '\0';
}
void makeSound(int soundId, int priority) {
    ++g_testMakeSoundCalls;
    g_testLastSoundId = soundId;
    g_testLastSoundPriority = priority;
}

char far g_world3dData[0x100] = {};
uint8 joyAxes[8] = {};
int16 exitCode = 0;
struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;

void configureStepFlightBase(Game &stepGame, GameComm &stepComm) {
    resetStepRecorders();
    resetFlightInputRecorders();
    std::memset(&stepGame, 0, sizeof(stepGame));
    std::memset(&stepComm, 0, sizeof(stepComm));
    gameData = &stepGame;
    commData = &stepComm;
    stepGame.theater = kStepTheaterDesertStorm;
    stepGame.difficulty = kStepDifficultyNormal;
    stepGame.unk4 = 0;
    stepComm.setupUseJoy = 0;
    g_initPhase = 1;
    g_frameRateScaling = kStepFrameScale;
    frameTick = kStepSnapshotSlot;
    g_missionTick = 0;
    g_currentWeaponType = 0;
    g_inputDisabled = 0;
    g_kbdSensitivity = 0;
    g_joyRawX = kStepKeyboardCenter;
    g_joyRawY = kStepKeyboardCenter;
    joyAxes[0] = joyAxes[1] = 0;
    g_groundAltitude = 0;
    g_viewZ = 0;
    g_altitude = 0;
    g_viewX_ = 0;
    g_viewY_ = 0;
    g_ViewX = 0;
    g_ViewY = 0;
    g_fuelRemaining = kStepFuelStart;
    g_playerPlaneFlags = 0;
    g_setThrust = 0;
    g_thrust = 0;
    g_velocity = 0;
    g_knots = 0;
    g_stallSpeed = 0;
    g_rollPitchTrim = 0;
    g_inLandingCorridor = 0;
    g_autopilotEngaged = 0;
    g_autopilotAltitude = 0;
    g_directorMode = 0;
    g_airTargetLock = -1;
    g_groundTargetLock = -1;
    keyValue = 0;
    g_joyCalibTimer = 0;
    g_gearDownArmed = 0;
    g_autoCrashDive = 0;
    g_autoLandingActive = 0;
    g_ejectState = 0;
    g_crashCamZ = 0;
    g_smokeSourceIdx = -1;
    g_gunHits = 0;
    g_gunFiredFlag = 0;
    g_orientationDirty = 0;
    g_rotationCounter = 0;
    g_rollWasNonzero = 0;
    std::memset(g_viewSnapshotRing, 0, sizeof(struct ViewSnapshot) * kStepSnapshotRingCount);
    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[0] = g_orientMatrix[4] = g_orientMatrix[8] = kQ15Identity;
}

void updateEngineSound(void) {
    ++g_testUpdateEngineSoundCalls;
}

int kbhit(void) {
    if (g_testKbhitIndex < g_testKbhitCount) {
        return g_testKbhitResults[g_testKbhitIndex++];
    }
    return g_testKeyIndex < g_testKeyCount;
}

int egReadKey(void) {
    require(g_testKeyIndex < g_testKeyCount, "test keyboard queue underflow");
    return g_testKeys[g_testKeyIndex++];
}

int main() {
    const int sampleAngles[] = {
        0,
        1,
        kSineRound - 1,
        kSineRound,
        kAngleFractionMask,
        kWordDegreeStep,
        0x1234,
        kAngleQuarterTurn - 1,
        kAngleQuarterTurn,
        kAngleHalfTurn - 1,
        kAngleHalfTurn,
        kAngleThreeQuarterTurn - 1,
        kAngleThreeQuarterTurn,
        kAngleHalfTurn + kAngleFractionMask,
        0xFFFF,
    };
    for (int angle : sampleAngles) {
        require(sine(angle) == expectedSine(angle), "sine interpolation matches original lookup");
        require(cosine(angle) == expectedSine(angle + kAngleQuarterTurn), "cosine is sine plus quarter turn");
        require(hudSine(angle) == expectedSine(angle), "HUD sine matches original lookup");
    }

    const int wordSamples[] = {
        -32768,
        -32767,
        -20000,
        -16384,
        -1,
        0,
        1,
        12345,
        16384,
        20000,
        32766,
        32767,
    };
    for (int a : wordSamples) {
        for (int b : wordSamples) {
            require(fixedMulQ14(a, b) == expectedFixedMulQ14(a, b),
                    "fixedMulQ14 preserves original rounding");
        }
    }

    for (long value : {0L, 1L, -1L, 0x12345678L, static_cast<long>(static_cast<int32>(0x80000000u)), 0x7fffffffL}) {
        for (int count : {0, 1, 3, 8, 15}) {
            long left = value;
            long right = value;
            shiftLongLeftInPlace(count, &left);
            shiftLongRightInPlace(count, &right);
            require(static_cast<int32>(left) == static_cast<int32>(static_cast<uint32>(static_cast<int32>(value)) << count),
                    "shiftLongLeftInPlace behaves as DOS 32-bit long shift");
            require(static_cast<int32>(right) == static_cast<int32>(value) >> count,
                    "shiftLongRightInPlace behaves as DOS arithmetic long shift");
        }
    }

    const int clampSamples[] = {
        -32768,
        -20000,
        kClampWrapFloor,
        kClampWrapFloor + 1,
        -11,
        -10,
        -9,
        0,
        9,
        10,
        11,
        20000,
        32767,
    };
    for (int value : clampSamples) {
        require(clampRange(value, -10, 10) == expectedClampRange(value, -10, 10),
                "clampRange preserves wrap-floor high clamp");
        require(egClampValue(value, -10, 10) == (value > 10 ? 10 : (value < -10 ? -10 : value)),
                "egClampValue is plain clamp");
        require(signOf(value) == (value == 0 ? 0 : (value > 0 ? 1 : -1)), "signOf matches original");
    }

    for (auto [dx, dy] : {
             std::pair{0, 0},
             std::pair{10, 4},
             std::pair{-10, 4},
             std::pair{4, -10},
             std::pair{-4, -10},
             std::pair{40000, 40000},
             std::pair{-32768, 0},
             std::pair{0, -32768},
             std::pair{32767, -32768},
         }) {
        require(rangeApprox(dx, dy) == expectedRangeApprox(dx, dy), "rangeApprox matches original cap");
    }
    for (auto [dx, dy] : {
             std::pair{0, 1},
             std::pair{0, -1},
             std::pair{1, 0},
             std::pair{-1, 0},
             std::pair{100, 100},
             std::pair{100, -50},
             std::pair{-50, 100},
             std::pair{-100, -20},
             std::pair{7, -123},
             std::pair{123, 7},
             std::pair{-123, 7},
             std::pair{7, 123},
             std::pair{-7, -123},
             std::pair{32767, 1},
             std::pair{1, 32767},
         }) {
        require(static_cast<uint16>(computeBearing(dx, dy)) == static_cast<uint16>(expectedBearing(dx, dy)),
                "computeBearing matches original quadrant approximation");
    }

    for (auto [angle, scalar] : {
             std::pair<int, int>{0, 1000},
             std::pair<int, int>{0x1234, -2000},
             std::pair<int, int>{kAngleQuarterTurn, 32767},
             std::pair<int, int>{kAngleHalfTurn, -32768},
             std::pair<int, int>{0xFFFF, 1},
             std::pair<int, int>{0x80FF, 16384},
         }) {
        require(sinMul(angle, scalar) == expectedFixedMulQ14(expectedSine(angle), scalar),
                "sinMul matches original fixed multiply");
        require(cosMul(angle, scalar) == expectedFixedMulQ14(expectedSine(angle + kAngleQuarterTurn), scalar),
                "cosMul matches original fixed multiply");
    }

    for (auto [num, den] : {
             std::pair{0, 1},
             std::pair{1, 1},
             std::pair{-1, 1},
             std::pair{1, -1},
             std::pair{-1, -1},
             std::pair{16383, 32767},
             std::pair{-16383, 32767},
             std::pair{32767, 32767},
             std::pair{-32767, 32767},
             std::pair{-12000, -24000},
             std::pair{12345, 23456},
         }) {
        require(static_cast<uint16>(signedRatio16(num, den)) == expectedSignedRatio16(num, den),
                "signedRatio16 returns original 16-bit quotient bit pattern");
    }
    const int inverseTrigSamples[] = {
        -32768,
        -32767,
        -4096,
        -2048,
        -513,
        -512,
        -1,
        0,
        1,
        511,
        512,
        2048,
        4096,
        32766,
        32767,
    };
    for (int value : inverseTrigSamples) {
        require(static_cast<uint16>(valueToAngle(value)) == static_cast<uint16>(expectedValueToAngle(value)),
                "valueToAngle matches original table interpolation");
        require(static_cast<uint16>(complementAngle(value)) ==
                    static_cast<uint16>(kAngleQuarterTurn - expectedValueToAngle(value)),
                "complementAngle is quarter turn minus valueToAngle");
    }

    for (int value : {-32768, -32767, -10000, -1024, -17, -4, -3, -1, 0, 1,
                      3, 4, 15, 16, 17, 255, 1024, 4096, 16384, 32767}) {
        require(isqrt(value) == expectedIsqrt(value), "isqrt matches original Newton iteration");
    }
    const int pitchSamples[] = {0, 1, -1, kAngleQuarterTurn, kAngleHalfTurn, 0xFFFF};
    for (int pitch : pitchSamples) {
        require(hudPitchScale(pitch) == static_cast<int>((static_cast<unsigned long>(static_cast<uint16>(pitch)) * 360u) >> 8),
                "hudPitchScale uses original unsigned-word pitch scale");
    }

    for (int maxVal : {1, 4, 100, 2000}) {
        std::srand(1234);
        const int randValue = std::rand() & 0x7fff;
        std::srand(1234);
        require(randomRange(maxVal) == static_cast<int>((static_cast<long>(randValue) * maxVal) >> kRandomScaleShift),
                "randomRange scales DOS 15-bit rand output");
    }

    resetFlightDrawRecorders();
    g_hudVisible = 0;
    g_setThrust = kThrottleMilitary;
    UpdateThrottleState();
    require(g_testRectCalls == 0 && g_testSetDrawColorCalls == 0,
            "UpdateThrottleState preserves original hidden-HUD no-op");
    g_hudVisible = 1;
    UpdateThrottleState();
    require(g_testSetDrawColorCalls == 2 &&
                g_testRectCalls == 2 &&
                g_testRects[0].x1 == kHudThrottleX1 &&
                g_testRects[0].y1 == kHudThrottleTop &&
                g_testRects[0].x2 == kHudThrottleX2 &&
                g_testRects[0].y2 == kHudThrottleBottom &&
                g_testRects[1].y1 ==
                    -(kThrottleMilitary / kGaugeThrottleDivisor - kHudThrottleBottom) &&
                g_testDrawColor == kHudThrottleNormalColor,
            "UpdateThrottleState draws the original military-throttle HUD gauge");
    resetFlightDrawRecorders();
    g_hudVisible = 1;
    g_setThrust = kThrottleAfterburner;
    UpdateThrottleState();
    require(g_testSetDrawColorCalls == 3 &&
                g_testRectCalls == 3 &&
                g_testRects[2].y1 ==
                    -(kThrottleAfterburner / kGaugeThrottleDivisor - kHudThrottleBottom) &&
                g_testRects[2].y2 == 142 &&
                g_testDrawColor == kHudThrottleAfterburnerColor,
            "UpdateThrottleState overlays the original afterburner color above 100 percent");

    resetFlightDrawRecorders();
    g_hudVisible = 0;
    g_fuelRemaining = kFuelNormal;
    drawFuelGauge();
    require(g_testRectCalls == 0, "drawFuelGauge preserves original hidden-HUD no-op");
    g_hudVisible = 1;
    drawFuelGauge();
    require(g_testSetDrawColorCalls == 2 &&
                g_testRectCalls == 2 &&
                g_testRects[0].x1 == kHudFuelX1 &&
                g_testRects[0].y1 == kHudFuelTop &&
                g_testRects[0].x2 == kHudFuelX2 &&
                g_testRects[0].y2 == kHudFuelBottom &&
                g_testRects[1].y1 ==
                    -(kFuelNormal / kGaugeFuelDivisor - kHudFuelBottom) &&
                g_testDrawColor == kHudFuelNormalColor,
            "drawFuelGauge draws the original normal-fuel HUD gauge");
    resetFlightDrawRecorders();
    g_hudVisible = 1;
    g_fuelRemaining = kFuelLow;
    drawFuelGauge();
    require(g_fuelRemaining < kHudFuelLowThreshold &&
                g_testDrawColor == kHudFuelLowColor &&
                g_testRects[1].y1 == -(kFuelLow / kGaugeFuelDivisor - kHudFuelBottom),
            "drawFuelGauge uses the original low-fuel color and scale");

    resetFlightDrawRecorders();
    colorLut[kShapeColorIndex] = kShapeColorValue;
    const int16 shapeData[] = {
        kShapeColorIndex, kShapeStartX, kShapeStartY,
        kShapeEndX, kShapeEndY, kShapeTerminator, kShapeTerminator,
    };
    drawVectorShape(shapeData);
    require(g_testGfxSetColorCalls == 1 &&
                g_testGfxColor == kShapeColorValue &&
                g_testResetSpanCalls == 1 &&
                g_testClipEdgeCalls == 1 &&
                g_testFlushSpanCalls == 1 &&
                g_lineX1 == kShapeStartX &&
                g_lineY1 == kShapeStartY &&
                g_lineX2 == kShapeEndX &&
                g_lineY2 == kShapeEndY,
            "drawVectorShape walks the original color/polyline stream");

    g_ourHead = g_ourPitch = g_ourRoll = 0;
    g_orientationDirty = 1;
    g_rotationCounter = 99;
    rebuildOrientation();
    require(g_orientationDirty == 0 &&
                g_rotationCounter == 0 &&
                g_orientMatrix[0] == kQ15ZeroAngleProduct &&
                g_orientMatrix[4] == kQ15ZeroAngleProduct &&
                g_orientMatrix[8] == kQ15ZeroAngleProduct,
            "rebuildOrientation rebuilds the original zero-angle matrix and clears dirty state");
    computeAttitudeAngles();
    require(g_ourHead == 0 && g_ourPitch == 0 && g_ourRoll == 0,
            "computeAttitudeAngles recovers original zero attitude from identity orientation");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[2] = kAttitudeWideComponent;
    g_orientMatrix[3] = kAttitudeWideComponent;
    g_orientMatrix[4] = kAttitudeNarrowComponent;
    g_orientMatrix[5] = 0;
    g_orientMatrix[8] = kAttitudeNarrowComponent;
    computeAttitudeAngles();
    require(g_ourHead == complementAngle(std::abs(static_cast<int>(
                static_cast<int16>(expectedSignedRatio16(kAttitudeNarrowComponent, kQ15Identity))))) &&
                g_ourRoll == complementAngle(std::abs(static_cast<int>(
                static_cast<int16>(expectedSignedRatio16(kAttitudeNarrowComponent, kQ15Identity))))),
            "computeAttitudeAngles uses original complement-angle path for wide heading and roll components");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[2] = kAttitudeNarrowComponent;
    g_orientMatrix[3] = kAttitudeNarrowComponent;
    g_orientMatrix[4] = -kAttitudeNarrowComponent;
    g_orientMatrix[5] = 0;
    g_orientMatrix[8] = -kAttitudeNarrowComponent;
    computeAttitudeAngles();
    require(g_ourHead == 0x8000 - valueToAngle(std::abs(static_cast<int>(
                static_cast<int16>(expectedSignedRatio16(kAttitudeNarrowComponent, kQ15Identity))))) &&
                g_ourRoll == 0x8000 - valueToAngle(std::abs(static_cast<int>(
                static_cast<int16>(expectedSignedRatio16(kAttitudeNarrowComponent, kQ15Identity))))),
            "computeAttitudeAngles preserves original positive/negative quadrant folding");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[2] = -kAttitudeNarrowComponent;
    g_orientMatrix[3] = -kAttitudeNarrowComponent;
    g_orientMatrix[4] = kAttitudeNarrowComponent;
    g_orientMatrix[5] = 0;
    g_orientMatrix[8] = kAttitudeNarrowComponent;
    computeAttitudeAngles();
    require(static_cast<uint16>(g_ourHead) == static_cast<uint16>(-valueToAngle(std::abs(static_cast<int>(
                expectedSignedRatio16(-kAttitudeNarrowComponent, kQ15Identity))))) &&
                static_cast<uint16>(g_ourRoll) == static_cast<uint16>(0x10000 - valueToAngle(std::abs(static_cast<int>(
                expectedSignedRatio16(-kAttitudeNarrowComponent, kQ15Identity))))),
            "computeAttitudeAngles preserves original negative/positive quadrant folding");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[1] = kAttitudeNarrowComponent;
    g_orientMatrix[3] = kAttitudeNarrowComponent;
    g_orientMatrix[4] = -kAttitudeNarrowComponent;
    g_orientMatrix[5] = kAttitudePitchQuarterTurnInput;
    computeAttitudeAngles();
    require(g_ourRoll == 0 &&
                g_ourHead == 0x8000 - valueToAngle(kAttitudeNarrowComponent),
            "computeAttitudeAngles preserves original vertical-pitch heading quadrant fallback");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[2] = 0;
    g_orientMatrix[3] = 0;
    g_orientMatrix[4] = -kAttitudeNarrowComponent;
    g_orientMatrix[5] = 0;
    g_orientMatrix[8] = -kAttitudeNarrowComponent;
    computeAttitudeAngles();
    require(static_cast<uint16>(g_ourHead) == kAngleHalfTurn &&
                static_cast<uint16>(g_ourRoll) == kAngleHalfTurn,
            "computeAttitudeAngles preserves original high-byte quadrant add for zero/negative axes");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[1] = kAttitudeNarrowComponent;
    g_orientMatrix[3] = 0;
    g_orientMatrix[4] = -kAttitudeNarrowComponent;
    g_orientMatrix[5] = kAttitudePitchQuarterTurnInput;
    computeAttitudeAngles();
    require(static_cast<uint16>(g_ourHead) ==
                static_cast<uint16>(valueToAngle(kAttitudeNarrowComponent) + kAngleHalfTurn),
            "computeAttitudeAngles preserves original vertical-pitch high-byte quadrant add");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[1] = kAttitudeNarrowComponent;
    g_orientMatrix[3] = -kAttitudeNarrowComponent;
    g_orientMatrix[4] = kAttitudeNarrowComponent;
    g_orientMatrix[5] = kAttitudePitchQuarterTurnInput;
    computeAttitudeAngles();
    require(static_cast<uint16>(g_ourHead) ==
                static_cast<uint16>(-valueToAngle(kAttitudeNarrowComponent)),
            "computeAttitudeAngles preserves original vertical-pitch negative heading fallback");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[5] = kQ15Identity; /* +1.0 sine input maps pitch to the original -90 degree dirty band. */
    g_orientationDirty = 0;
    computeAttitudeAngles();
    require(g_orientationDirty == 1,
            "computeAttitudeAngles dirties orientation in the original negative near-vertical pitch band");

    std::memset(g_orientMatrix, 0, sizeof(int16) * 9);
    g_orientMatrix[0] = g_orientMatrix[4] = g_orientMatrix[8] = kQ15Identity;
    g_rollWasNonzero = 1;
    g_orientationDirty = 0;
    computeAttitudeAngles();
    require(g_ourRoll == 0 && g_orientationDirty == 1,
            "computeAttitudeAngles preserves original roll-was-nonzero dirty guard");
    g_rollWasNonzero = 0;

    int16 identityA[9] = {};
    int16 identityB[9] = {};
    identityA[0] = identityA[4] = identityA[8] = kQ15Identity;
    identityB[0] = identityB[4] = identityB[8] = kQ15Identity;
    g_rotationCounter = kRotationDirtyPeriod - 1;
    g_orientationDirty = 0;
    applyRotationDelta(identityA, identityB);
	    require(g_rotationCounter == kRotationDirtyPeriod &&
	                g_orientationDirty == 1 &&
	                g_orientMatrix[0] == kQ15ZeroAngleProduct &&
	                g_orientMatrix[4] == kQ15ZeroAngleProduct &&
	                g_orientMatrix[8] == kQ15ZeroAngleProduct,
	            "applyRotationDelta multiplies matrices and dirties orientation every original eighth rotation");

	    resetStepRecorders();
	    struct Game stepGame = {};
	    struct GameComm stepComm = {};
	    gameData = &stepGame;
	    commData = &stepComm;
	    stepGame.theater = kStepTheaterDesertStorm;
	    stepGame.difficulty = kStepDifficultyNormal;
	    stepGame.unk4 = 0;
	    stepComm.setupUseJoy = 0;
	    g_initPhase = 0;
	    g_frameRateScaling = kStepFrameScale;
	    frameTick = kStepSnapshotSlot;
	    g_missionTick = 0;
	    g_currentWeaponType = 0;
	    g_inputDisabled = 0;
	    g_kbdSensitivity = 0;
	    g_joyRawX = kStepKeyboardCenter;
	    g_joyRawY = kStepKeyboardCenter;
	    joyAxes[0] = joyAxes[1] = 0;
	    g_groundAltitude = 0;
	    g_fuelRemaining = kStepFuelStart;
	    g_playerPlaneFlags = 0;
	    g_orientationDirty = 1;
	    g_rotationCounter = 99;
	    g_targetSlots[0].viewIndex = 0;
	    g_planeTable.planes[0].flags = 0;
	    std::memset(g_viewSnapshotRing, 0, sizeof(struct ViewSnapshot) * kStepSnapshotRingCount);
	    stepFlightModel();
	    require(g_initPhase == 1 &&
	                g_ourHead == 0 &&
	                g_ourPitch == 0 &&
	                g_ourRoll == 0 &&
	                g_viewZ == 0 &&
	                g_altitude == 0 &&
	                g_velocity == 0 &&
	                g_setThrust == 0 &&
	                g_thrust == 0,
	            "stepFlightModel initializes the original Desert Storm no-input ground state");
	    require((g_playerPlaneFlags & kStepBrakeFlag) != 0 &&
	                g_testTempMessageCalls == 1 &&
	                g_testMakeSoundCalls == 0,
	            "stepFlightModel applies the original brakes-on ground behavior without throttle sound");
	    require(g_testEnginePitchCalls == 1 &&
	                g_testLastEngineKnots == g_knots &&
	                g_testLastEngineThrust == g_thrust,
	            "stepFlightModel updates the original engine pitch from computed knots and thrust");
	    require(g_viewSnapshotRing[kStepSnapshotSlot].heading == g_ourHead &&
	                g_viewSnapshotRing[kStepSnapshotSlot].pitch == g_ourPitch &&
	                g_viewSnapshotRing[kStepSnapshotSlot].roll == g_ourRoll &&
	                g_viewSnapshotRing[kStepSnapshotSlot].worldX == g_ViewX &&
	                g_viewSnapshotRing[kStepSnapshotSlot].worldY == g_ViewY &&
	                g_viewSnapshotRing[kStepSnapshotSlot].alt == g_viewZ,
	            "stepFlightModel records the original per-frame view snapshot");

	    configureStepFlightBase(stepGame, stepComm);
	    g_setThrust = 5;
	    g_testKeys[0] = kStepKeyThrottleUp;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_setThrust == 5 + kStepThrottleLowDelta &&
	                g_testLastSoundId == kStepThrottleSound,
	            "stepFlightModel applies original low-throttle keyboard increment and engine-start sound");

	    configureStepFlightBase(stepGame, stepComm);
	    g_setThrust = 20;
	    g_testKeys[0] = kStepKeyThrottleDown;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_setThrust == 20 - kStepThrottleDelta,
	            "stepFlightModel applies original throttle-down keyboard decrement");

	    configureStepFlightBase(stepGame, stepComm);
	    g_setThrust = 55;
	    g_testKeys[0] = kStepKeyShiftMinus;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_setThrust == 0 && g_testLastSoundId == kStepThrottleCutSound,
	            "stepFlightModel applies original shift-minus throttle cut sound");

	    configureStepFlightBase(stepGame, stepComm);
	    g_playerPlaneFlags = kStepBrakeFlag;
	    g_groundAltitude = 100;
	    g_viewZ = 100;
	    g_altitude = 100;
	    g_setThrust = 100;
	    g_testKeys[0] = kStepKeyBrakeToggle;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require((g_playerPlaneFlags & kStepBrakeFlag) == 0 &&
	                g_velocity > 0 &&
	                g_testMakeSoundCalls == kExpectedTwoCalls,
	            "stepFlightModel preserves original brake release launch assist");

	    configureStepFlightBase(stepGame, stepComm);
	    g_testKeys[0] = kStepKeyAltJoystick;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_testJoystickCalibCalls == 1 &&
	                g_joyCalibTimer == kStepJoystickCalibCooldown - 1,
	            "stepFlightModel starts original joystick calibration cooldown");

	    configureStepFlightBase(stepGame, stepComm);
	    exitCode = 99;
	    g_testKeys[0] = kStepKeyAltQuit;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_testFinalizeMissionCalls == 1 &&
	                g_testFinalizeMissionOutcome == 1 &&
	                exitCode == 0,
	            "stepFlightModel preserves original Alt-Q mission finalization side effects");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autopilotEngaged = 1;
	    g_directorMode = 2;
	    keyValue = 77;
	    g_testKeys[0] = kStepKeyThrottleUp;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_autopilotEngaged == 0 && g_directorMode == 0 && keyValue == 0,
	            "stepFlightModel cancels original autopilot/director state on keyboard input");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autopilotEngaged = 1;
	    g_autopilotAltitude = kStepAutopilotAltitude;
	    g_viewZ = kStepAutopilotViewZ;
	    g_altitude = kStepAutopilotViewZ;
	    g_viewX_ = kStepAutopilotViewX;
	    g_viewY_ = kStepAutopilotViewY;
	    g_ViewX = static_cast<long>(kStepAutopilotViewX) << kStepWorldCoordShift;
	    g_ViewY = static_cast<long>(kStepAutopilotViewY) << kStepWorldCoordShift;
	    g_missionTick = kStepAutopilotMissionTick;
	    g_waypointBearing = kStepAutopilotWaypointBearing;
	    waypointIndex = 3;
	    g_northSouthSign = 1;
	    g_targetSlots[1].viewIndex = 0;
	    g_planeTable.planes[0].mapX = kStepAutopilotTargetX;
	    g_planeTable.planes[0].mapY = kStepAutopilotTargetY;
	    g_planeTable.planes[0].flags = 0;
	    g_knots = kStepAutopilotKnots;
	    g_setThrust = 1;
	    stepFlightModel();
	    require(g_autopilotAltitude == kStepAutopilotAltitude &&
	                g_testExitSlowMotionCalls == 1 &&
	                g_setThrust >= kStepAutopilotThrottleMin &&
	                g_setThrust <= kStepAutopilotThrottleMax &&
	                (g_playerPlaneFlags & kStepGearFlag) == 0,
	            "stepFlightModel preserves original waypoint-3 autopilot target steering and throttle clamp");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autopilotEngaged = 1;
	    g_autopilotAltitude = kStepAutopilotAltitude;
	    g_viewZ = kStepLandingGroundAlt;
	    g_altitude = kStepLandingGroundAlt;
	    g_groundAltitude = kStepLandingGroundAlt;
	    g_viewX_ = kStepAutopilotViewX;
	    g_viewY_ = kStepAutopilotViewY;
	    g_missionTick = kStepAutopilotMissionTick;
	    waypointIndex = 3;
	    g_northSouthSign = 1;
	    g_inLandingCorridor = 1;
	    g_playerPlaneFlags = kStepGearFlag;
	    g_targetSlots[1].viewIndex = 0;
	    g_planeTable.planes[0].mapX = kStepAutopilotTargetX;
	    g_planeTable.planes[0].mapY = kStepAutopilotViewY;
	    g_planeTable.planes[0].flags = kStepCarrierFlag;
	    g_knots = 300; /* Below the original 350 knot landing gear clear threshold. */
	    g_setThrust = 1;
	    stepFlightModel();
	    require(g_setThrust == 0 &&
	                g_rollInput == 0 &&
	                g_pitchInput == 0 &&
	                (g_playerPlaneFlags & kStepBrakeFlag) != 0 &&
	                (g_playerPlaneFlags & kStepGearFlag) == 0,
	            "stepFlightModel preserves original carrier landing-corridor ground clamp");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autopilotEngaged = 1;
	    g_autopilotAltitude = kStepAutopilotAltitude;
	    g_viewZ = kStepAutopilotViewZ;
	    g_altitude = kStepAutopilotViewZ;
	    g_viewX_ = kStepAutopilotViewX;
	    g_viewY_ = kStepAutopilotViewY;
	    g_missionTick = kStepAutopilotMissionTick;
	    g_ourHead = kStepAutopilotFarHeading;
	    waypointIndex = 3;
	    g_northSouthSign = 1;
	    g_targetSlots[1].viewIndex = 0;
	    g_planeTable.planes[0].mapX = kStepAutopilotTargetX;
	    g_planeTable.planes[0].mapY = kStepAutopilotTargetY;
	    g_knots = kStepAutopilotKnots;
	    stepFlightModel();
	    require(g_setThrust == kStepAutopilotThrottleMax,
	            "stepFlightModel preserves original waypoint-3 far-heading full approach throttle");

	    configureStepFlightBase(stepGame, stepComm);
	    g_viewZ = 100;
	    g_altitude = 100;
	    g_knots = 351;
	    g_gearDownArmed = 1;
	    stepFlightModel();
	    require((g_playerPlaneFlags & kStepGearFlag) != 0 &&
	                g_gearDownArmed == 0 &&
	                std::strcmp(g_testLastTempMessage, "Landing gear raised") == 0 &&
	                g_testMakeSoundCalls == kExpectedTwoCalls,
	            "stepFlightModel auto-raises landing gear with the original warning text and sound");

	    configureStepFlightBase(stepGame, stepComm);
	    frameTick = 1;
	    g_viewX_ = 123;
	    g_viewY_ = 456;
	    g_viewZ = 500;
	    g_altitude = 500;
	    g_ejectState = 15;
	    stepFlightModel();
	    require(g_smokeParticleSlot == 0 &&
	                g_particles[0].posX == 123 &&
	                g_particles[0].posY == 456 &&
	                g_particles[0].alt == 500 &&
	                g_hitEffectTimer == static_cast<int16>(-8),
	            "stepFlightModel emits the original ejection smoke particle on the matching frame phase");

	    configureStepFlightBase(stepGame, stepComm);
	    g_viewZ = 500;
	    g_altitude = 500;
	    g_ejectState = 0x20; /* Next original ejection step applies a small negative crash-camera delta. */
	    g_crashCamZ = 0;
	    g_missionTick = 0; /* Original finalizes ejection only on eight-frame mission cadence. */
	    stepFlightModel();
	    require(g_crashCamZ == 0 &&
	                g_testFinalizeMissionCalls == 1 &&
	                g_testFinalizeMissionOutcome == 0,
	            "stepFlightModel preserves original ejection crash-camera floor and mission finalization cadence");

	    configureStepFlightBase(stepGame, stepComm);
	    g_currentWeaponType = 1;
	    g_airTargetLock = -1;
	    frameTick = 10;
	    g_frameRateScaling = kStepFrameScale;
	    g_viewSnapshotRing[1].heading = 400;
	    g_viewSnapshotRing[1].pitch = 200;
	    stepFlightModel();
	    require(g_aamSeekerX == 100 && g_aamSeekerY == -100,
	            "stepFlightModel computes original AAM seeker drift from delayed view snapshot");

	    configureStepFlightBase(stepGame, stepComm);
	    g_currentWeaponType = 1;
	    g_airTargetLock = 0;
	    frameTick = 10;
	    g_frameRateScaling = kStepFrameScale;
	    g_viewX_ = 0;
	    g_viewY_ = 0;
	    g_simObjects[0].posX = kStepAamLockedTargetX;
	    g_simObjects[0].posY = kStepAamLockedTargetY;
	    g_viewSnapshotRing[8].heading = 800; /* Locked-target range delay selects this original snapshot slot. */
	    g_viewSnapshotRing[8].pitch = 600;
	    stepFlightModel();
	    require(g_aamSeekerX == 200 && g_aamSeekerY == -300,
	            "stepFlightModel computes original AAM seeker drift from locked-target range delay");

	    configureStepFlightBase(stepGame, stepComm);
	    stepGame.unk4 = 1;
	    std::srand(1234);
	    g_viewZ = 500;
	    g_altitude = 500;
	    g_knots = 400;
	    stepFlightModel();
	    require(g_testEnginePitchCalls == 1,
	            "stepFlightModel runs the original weather turbulence path for nonzero game weather");

	    configureStepFlightBase(stepGame, stepComm);
	    g_playerPlaneFlags = kStepGearFlag;
	    g_viewZ = 100;
	    g_altitude = 100;
	    g_velocity = 1000;
	    g_stallSpeed = 100;
	    g_rollPitchTrim = 2000;
	    stepFlightModel();
	    require(g_pitchInput > 0,
	            "stepFlightModel preserves original gear-down positive pitch correction below trim");

	    configureStepFlightBase(stepGame, stepComm);
	    g_playerPlaneFlags = kStepBrakeFlag;
	    g_groundAltitude = kStepLandingGroundAlt;
	    g_viewZ = kStepLandingGroundAlt;
	    g_altitude = kStepLandingGroundAlt;
	    g_velocity = kStepGroundBrakeSlowVelocity;
	    stepFlightModel();
	    require(g_velocity == 0,
	            "stepFlightModel preserves original low-speed ground brake velocity floor");

	    configureStepFlightBase(stepGame, stepComm);
	    frameTick = kStepNoFuelBurnFrame;
	    g_setThrust = 100;
	    g_gunHits = kStepGunHitsThrottleFloor;
	    stepFlightModel();
	    require(g_setThrust == 0 &&
	                g_testRectCalls == kExpectedTwoCalls,
	            "stepFlightModel preserves original negative gun-damage throttle floor clamp");

	    configureStepFlightBase(stepGame, stepComm);
	    g_groundAltitude = 0;
	    g_viewZ = 100;
	    g_altitude = 100;
	    g_ourRoll = 0x3eba; /* Incoming roll selects an original g-load table entry above the 0x80 cap. */
	    g_orientMatrix[3] = 0x5a82; /* Just past the original roll complement-angle threshold. */
	    g_orientMatrix[4] = kAttitudeNarrowComponent;
	    stepFlightModel();
	    require(g_gees == 0x80,
	            "stepFlightModel preserves original high-gee cap and pitch-input clamp");

	    configureStepFlightBase(stepGame, stepComm);
	    g_groundAltitude = kStepLandingGroundAlt;
	    g_viewZ = kStepLandingGroundAlt;
	    g_altitude = kStepLandingGroundAlt;
	    g_orientMatrix[3] = kAttitudeNarrowComponent;
	    g_orientMatrix[4] = kQ15Identity;
	    stepFlightModel();
	    require(g_ourRoll == 0,
	            "stepFlightModel preserves original ground roll leveling");

	    configureStepFlightBase(stepGame, stepComm);
	    g_groundAltitude = kStepLandingGroundAlt;
	    g_viewZ = kStepLandingGroundAlt;
	    g_altitude = kStepLandingGroundAlt;
	    g_orientMatrix[5] = kAttitudeNarrowComponent; /* Positive matrix sine decodes to negative pitch. */
	    stepFlightModel();
	    require(g_ourPitch == 0,
	            "stepFlightModel preserves original ground pitch leveling");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autoLandingActive = 1;
	    g_altitude = kStepHighAltitudeMid;
	    stepFlightModel();
	    require(g_viewZ == (((kStepHighAltitudeMid - 0x2000) >> 1) + 0x2000),
	            "stepFlightModel preserves original mid-altitude compressed view-height mapping");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autoLandingActive = 1;
	    g_altitude = kStepHighAltitudeTop;
	    stepFlightModel();
	    require(g_viewZ == (((kStepHighAltitudeTop - 0x4000) >> 2) + 0x3000),
	            "stepFlightModel preserves original high-altitude compressed view-height mapping");

	    configureStepFlightBase(stepGame, stepComm);
	    g_groundAltitude = 100;
	    g_viewZ = 101;
	    g_altitude = 101;
	    g_velocity = 5000;
	    g_inLandingCorridor = 1;
	    g_missionStatus = 1;
	    g_closestThreatIndex = 0;
	    g_planeTable.planes[0].flags = 0;
	    g_orientMatrix[5] = kQ15Identity; /* Original attitude recovery maps this to a -90 degree touchdown descent. */
	    stepFlightModel();
	    require(g_testLastSoundId == kStepLandingCrashSound &&
	                g_testFinalizeMissionCalls == 1 &&
	                g_testFinalizeMissionOutcome == kStepLandingCrashOutcome,
	            "stepFlightModel preserves original hard landing crash finalization");

	    configureStepFlightBase(stepGame, stepComm);
	    stepGame.unk4 = 1;
	    g_groundAltitude = 100;
	    g_viewZ = 101;
	    g_altitude = 101;
	    g_velocity = 1000;
	    g_playerPlaneFlags = kStepGearFlag;
	    g_inLandingCorridor = 1;
	    g_missionStatus = 1;
	    g_orientMatrix[5] = kQ15Identity; /* Keeps sink rate below the hard-impact threshold; weather/gear triggers the crash. */
	    stepFlightModel();
	    require(g_testLastSoundId == kStepLandingCrashSound &&
	                g_testFinalizeMissionCalls == 1 &&
	                g_testFinalizeMissionOutcome == kStepLandingCrashOutcome,
	            "stepFlightModel preserves original weather gear-down landing crash condition");

	    configureStepFlightBase(stepGame, stepComm);
	    stepGame.unk4 = 1;
	    g_groundAltitude = 100;
	    g_viewZ = 101;
	    g_altitude = 101;
	    g_velocity = 1000;
	    g_inLandingCorridor = 1;
	    g_missionStatus = 1;
	    g_orientMatrix[3] = 0x5a82; /* Decodes to a roll beyond the original weather landing limit. */
	    g_orientMatrix[4] = kAttitudeNarrowComponent;
	    g_orientMatrix[5] = 30000; /* Steep enough to touch down, below the hard-impact sink threshold. */
	    stepFlightModel();
	    require(g_testLastSoundId == kStepLandingCrashSound &&
	                g_testFinalizeMissionCalls == 1 &&
	                g_testFinalizeMissionOutcome == kStepLandingCrashOutcome,
	            "stepFlightModel preserves original weather excessive-roll landing crash condition");

	    configureStepFlightBase(stepGame, stepComm);
	    g_initPhase = 0;
	    stepGame.difficulty = 0;
	    g_viewY_ = 100;
	    waypoints[1].mapY = 200;
	    stepFlightModel();
	    require(g_initPhase == 1 && g_ourHead == 0,
	            "stepFlightModel initializes original training heading from waypoint north/south comparison");

	    configureStepFlightBase(stepGame, stepComm);
	    g_initPhase = 0;
	    stepGame.theater = kStepTheaterOdd;
	    stepGame.difficulty = kStepDifficultyNormal;
	    stepFlightModel();
	    require(g_initPhase == 1 && g_ourHead == 0,
	            "stepFlightModel initializes original odd-theater combat heading");

	    configureStepFlightBase(stepGame, stepComm);
	    g_initPhase = 0;
	    stepGame.theater = kStepTheaterEven;
	    stepGame.difficulty = kStepDifficultyNormal;
	    g_targetSlots[0].viewIndex = 0;
	    g_planeTable.planes[0].flags = kStepCarrierFlag;
	    resetRenderRecorders();
	    stepFlightModel();
	    require(g_initPhase == 1 &&
	                g_testBuildAngleX == static_cast<int16>(kStepCarrierHeadingHighByte << 8),
	            "stepFlightModel preserves original carrier-heading high-byte bias during init");

	    configureStepFlightBase(stepGame, stepComm);
	    g_setThrust = 20;
	    g_testKeys[0] = kStepKeyAfterburner;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_setThrust == kStepAfterburnerThrottle &&
	                (g_playerPlaneFlags & kStepBrakeFlag) == 0,
	            "stepFlightModel applies original A-key afterburner throttle command");

	    configureStepFlightBase(stepGame, stepComm);
	    g_playerPlaneFlags = kStepBrakeFlag;
	    g_testKeys[0] = kStepKeyShiftThrottleUp;
	    g_testKeyCount = 1;
	    stepFlightModel();
	    require(g_setThrust == kStepFullThrottle &&
	                (g_playerPlaneFlags & kStepBrakeFlag) == 0,
	            "stepFlightModel applies original shift-equal full-throttle command and clears brakes");

	    configureStepFlightBase(stepGame, stepComm);
	    g_setThrust = 5;
	    g_testKeys[0] = kStepKeyThrottleUp;
	    g_testKeys[1] = kStepKeyThrottleDown;
	    g_testKeyCount = kExpectedTwoCalls;
	    stepFlightModel();
	    require(g_testKeyIndex == kExpectedTwoCalls &&
	                g_setThrust == 5 + kStepThrottleLowDelta,
	            "stepFlightModel flushes queued keyboard input after dispatching one original command");

	    configureStepFlightBase(stepGame, stepComm);
	    g_inputDisabled = 1;
	    joyAxes[0] = kStepAxisMax;
	    joyAxes[1] = kStepAxisMax;
	    stepFlightModel();
	    require(joyAxes[0] == 0 && joyAxes[1] == 0,
	            "stepFlightModel zeroes original joystick axes while input is disabled");

	    configureStepFlightBase(stepGame, stepComm);
	    stepComm.setupUseJoy = 1;
	    stepFlightModel();
	    require(g_testReadJoystickCalls == 1,
	            "stepFlightModel routes through original calibrated joystick reader when enabled");

	    configureStepFlightBase(stepGame, stepComm);
	    resetRenderRecorders();
	    g_hudVisible = 1;
	    g_testKeys[0] = kStepKeyAltBriefing;
	    g_testKeys[1] = kResumeKey;
	    g_testKeyCount = kExpectedTwoCalls;
	    g_testKbhitResults[0] = kKbhitHasKey;
	    g_testKbhitResults[1] = kKbhitNoKey;
	    g_testKbhitResults[2] = kKbhitHasKey;
	    g_testKbhitCount = 3; /* Dispatch key, empty flush, resume key. */
	    stepFlightModel();
	    require(g_testCopyRectCalls == 3 &&
	                g_testBlitCalls == 1 &&
	                g_testAudioEngineOffCalls == 1 &&
	                g_testUpdateEngineSoundCalls == 1,
	            "stepFlightModel preserves original Alt-B pause overlay and HUD restore path");

	    configureStepFlightBase(stepGame, stepComm);
	    g_testKeys[0] = kPauseKey;
	    g_testKeys[1] = kPauseKey;
	    g_testKeys[2] = kResumeKey;
	    g_testKeyCount = 3; /* Initial Alt-P dispatch plus one ignored pause key and one resume key. */
	    g_testKbhitResults[0] = kKbhitHasKey;
	    g_testKbhitResults[1] = kKbhitNoKey;
	    g_testKbhitResults[2] = kKbhitHasKey;
	    g_testKbhitResults[3] = kKbhitHasKey;
	    g_testKbhitCount = 4; /* Dispatch key, empty flush, pause key, resume key. */
	    stepFlightModel();
	    require(g_testKeyIndex == 3 &&
	                g_testAudioEngineOffCalls == 1 &&
	                g_testUpdateEngineSoundCalls == 1,
	            "stepFlightModel preserves original Alt-P pause loop key handling");

	    configureStepFlightBase(stepGame, stepComm);
	    frameTick = kStepNoFuelBurnFrame;
	    g_setThrust = 100;
	    g_gunHits = kStepGunHitsThrottleLimit;
	    stepFlightModel();
	    require(g_setThrust == kStepGunHitsClampedThrottle &&
	                g_testRectCalls == kExpectedTwoCalls,
	            "stepFlightModel clamps throttle by original gun-damage limit and redraws the throttle gauge");

	    configureStepFlightBase(stepGame, stepComm);
	    frameTick = kStepFuelBurnFrame;
	    g_setThrust = kStepFullThrottle;
	    g_thrust = kStepFullThrottle;
	    g_fuelRemaining = kStepFuelDepletionStart;
	    stepFlightModel();
	    require(g_fuelRemaining == 0 &&
	                g_thrust == 0 &&
	                g_testRectCalls == kExpectedTwoCalls,
	            "stepFlightModel preserves original fuel burn cadence and zero-thrust fuel exhaustion");

	    configureStepFlightBase(stepGame, stepComm);
	    g_viewX_ = 321;
	    g_viewY_ = 654;
	    g_viewZ = 0;
	    g_ejectState = kStepEjectGroundState;
	    g_smokeSourceIdx = -1;
	    stepFlightModel();
	    require(g_smokeSourceIdx == 0 &&
	                g_planeTable.planes[0].mapX == 321 &&
	                g_planeTable.planes[0].mapY == 654 &&
	                g_hitMapX == 321 &&
	                g_hitMapY == 654 &&
	                g_hitAlt == 0 &&
	                g_hitEffectTimer == static_cast<int16>(kStepSmokeHitTimer) &&
	                g_setThrust == 0 &&
	                g_velocity == 0 &&
	                g_testLastSoundId == kStepEjectCrashSound,
	            "stepFlightModel preserves original ejection ground-impact smoke and engine stop state");

	    configureStepFlightBase(stepGame, stepComm);
	    g_autoCrashDive = 1;
	    g_ourPitch = 0;
	    g_velocity = 1000;
	    g_setThrust = 50;
	    stepFlightModel();
	    require(g_velocity == 0 &&
	                g_setThrust == 0 &&
	                g_autoCrashDive == 0,
	            "stepFlightModel preserves original auto-crash-dive velocity cut");

		    resetRenderRecorders();
	    keyValue = 0;
	    g_lastViewKey = 0;
	    g_hudVisible = kRenderHudVisible;
	    g_detailLevel = 1;
	    g_mapMode = 0;
	    g_activePanelMode = 0;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = kRenderHead;
	    g_ourPitch = kRenderPitch;
	    g_ourRoll = kRenderRoll;
	    g_pageFront[8] = 96;
	    for (int i = 0; i < 9; ++i) {
	        g_orientMatrix[i] = static_cast<int16>(i + 1);
	        g_camRotMatrix[i] = 0;
	    }
	    renderFrame();
	    require(g_externalCamDist == kRenderExternalCamMin &&
	                g_camEyeX == kRenderViewX &&
	                g_camEyeY == kRenderViewY &&
	                g_camEyeZ == kRenderViewZ + 0x18 &&
	                g_viewTargetX == kRenderViewX &&
	                g_viewTargetY == kRenderWorldFlipBase - kRenderViewY &&
	                g_viewTargetAlt == kRenderViewZ,
	            "renderFrame initializes the original forward cockpit camera and clamps external distance");
	    require(g_viewHeading == kRenderHead &&
	                g_viewPitch == kRenderPitch &&
	                g_viewRoll == kRenderRoll &&
	                std::memcmp(g_camRotMatrix, g_orientMatrix, sizeof(int16) * 9) == 0,
	            "renderFrame keeps original orientation matrix for forward view");
	    require(g_testLoadPaletteCalls == 1 &&
	                g_testRender3DCalls == 1 &&
	                g_testRenderCamX == -kRenderHead &&
	                g_testRenderCamY == kRenderPitch &&
	                g_testRenderCamZ == kRenderRoll &&
	                g_testRenderWorldX == kRenderViewX &&
	                g_testRenderWorldY == kRenderViewY &&
	                g_testRenderWorldZ == kRenderViewZ + 0x18 &&
	                g_testRenderClipHeight == g_pageFront[8] + 1 &&
	                g_hudBottomY == kRenderHudBottom,
	            "renderFrame loads palette and renders the original forward 3D viewport");

	    resetRenderRecorders();
	    keyValue = kRenderTrailingViewKey;
	    g_lastViewKey = 0;
	    g_hudVisible = kRenderHudVisible;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_detailLevel = 1;
	    frameTick = kRenderTrailingFrameTick;
	    g_frameRateScaling = kRenderTrailingFrameScale;
	    g_renderAlphaQ12 = kRenderTrailingHalfAlphaQ12;
	    g_viewSnapshotRing[kRenderTrailingPreviousSlot].heading = 1000;
	    g_viewSnapshotRing[kRenderTrailingPreviousSlot].pitch = 2000;
	    g_viewSnapshotRing[kRenderTrailingPreviousSlot].roll = 3000;
	    g_viewSnapshotRing[kRenderTrailingPreviousSlot].worldX = 10000;
	    g_viewSnapshotRing[kRenderTrailingPreviousSlot].worldY = 20000;
	    g_viewSnapshotRing[kRenderTrailingPreviousSlot].alt = 300;
	    g_viewSnapshotRing[kRenderTrailingCurrentSlot].heading = 3000;
	    g_viewSnapshotRing[kRenderTrailingCurrentSlot].pitch = 6000;
	    g_viewSnapshotRing[kRenderTrailingCurrentSlot].roll = 7000;
	    g_viewSnapshotRing[kRenderTrailingCurrentSlot].worldX = 14000;
	    g_viewSnapshotRing[kRenderTrailingCurrentSlot].worldY = 28000;
	    g_viewSnapshotRing[kRenderTrailingCurrentSlot].alt = 500;
	    renderFrame();
	    require(g_viewHeading == 2000 &&
	                g_viewPitch == 4000 &&
	                g_viewRoll == 5000 &&
	                g_camEyeX == 12000 &&
	                g_camEyeY == 24000 &&
	                g_camEyeZ == 400,
	            "renderFrame uses the original Q12 interpolation for the trailing replay camera");
	    require(g_testBuildRotationCalls == 1 &&
	                g_testBuildAngleX == g_viewHeading &&
	                g_testBuildAngleY == g_viewPitch &&
	                g_testBuildAngleZ == g_viewRoll &&
	                g_testCopyRectCalls == 1 &&
	                g_pageFront[8] == 199 &&
	                g_hudBottomY == kRenderHiddenHudBottom,
	            "renderFrame hides the HUD and rebuilds camera rotation for the original trailing view");

	    resetRenderRecorders();
	    keyValue = kRenderRearViewKey;
	    g_lastViewKey = 0;
	    g_hudVisible = kRenderHudVisible;
	    g_detailLevel = 1;
	    g_mapMode = 0;
	    g_activePanelMode = 0;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = kRenderHead;
	    g_ourPitch = kRenderPitch;
	    g_ourRoll = kRenderRoll;
	    g_pageFront[8] = g_pageBack[8] = 199;
	    renderFrame();
	    require(static_cast<uint16>(g_viewHeading) == static_cast<uint16>(kRenderHead + kAngleHalfTurn) &&
	                g_viewPitch == -kRenderPitch &&
	                g_viewRoll == -kRenderRoll &&
	                std::strcmp(g_testLastPicName, "256Rear.Pic") == 0 &&
	                g_pageFront[8] == kRenderSidePanelBottom &&
	                g_pageBack[8] == kRenderSidePanelBottom,
	            "renderFrame switches to the original rear cockpit picture and side-panel viewport");
		    require(g_testBuildRotationCalls == 1 &&
		                g_testCopyRectCalls == 2 &&
		                g_testBlitCalls == 2 &&
		                g_testRenderCamX == -g_viewHeading &&
		                g_testRenderCamY == g_viewPitch &&
		                g_testRenderCamZ == g_viewRoll &&
		                g_testRenderClipHeight == kRenderSidePanelBottom + 1 &&
		                g_hudBottomY == kRenderHiddenHudBottom,
		            "renderFrame renders the original rear cockpit overlay with hidden HUD");

	    resetRenderRecorders();
	    keyValue = kRenderRightViewKey;
	    g_lastViewKey = 0;
	    g_hudVisible = kRenderHudVisible;
	    g_detailLevel = 1;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = kRenderHead;
	    g_ourPitch = kRenderPitch;
	    g_ourRoll = kRenderRoll;
	    g_pageFront[8] = g_pageBack[8] = 199;
	    renderFrame();
	    require(static_cast<uint16>(g_viewHeading) ==
	                static_cast<uint16>(kRenderHead + kAngleQuarterTurn) &&
	                g_viewPitch == -kRenderRoll &&
	                g_viewRoll == kRenderPitch &&
	                std::strcmp(g_testLastPicName, "256Right.Pic") == 0,
	            "renderFrame preserves original right cockpit camera transform and picture");

	    resetRenderRecorders();
	    keyValue = kRenderLeftViewKey;
	    g_lastViewKey = 0;
	    g_hudVisible = kRenderHudVisible;
	    g_detailLevel = 1;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = kRenderHead;
	    g_ourPitch = kRenderPitch;
	    g_ourRoll = kRenderRoll;
	    g_pageFront[8] = g_pageBack[8] = 199;
	    renderFrame();
	    require(static_cast<uint16>(g_viewHeading) ==
	                static_cast<uint16>(kRenderHead - kAngleQuarterTurn) &&
	                g_viewPitch == kRenderRoll &&
	                g_viewRoll == -kRenderPitch &&
	                std::strcmp(g_testLastPicName, "256Left.Pic") == 0,
	            "renderFrame preserves original left cockpit camera transform and picture");

	    resetRenderRecorders();
	    keyValue = kRenderRearViewKey;
	    g_lastViewKey = 0;
	    g_testModeCode = kRenderEgaMode;
	    g_hudVisible = kRenderHudVisible;
	    g_detailLevel = 1;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = kRenderHead;
	    g_ourPitch = kRenderPitch;
	    g_ourRoll = kRenderRoll;
	    renderFrame();
	    require(std::strcmp(g_testLastPicName, "Rear.Pic") == 0,
	            "renderFrame preserves original non-MCGA rear cockpit picture name");

	    resetRenderRecorders();
	    keyValue = kRenderLeftExternalKey;
	    g_lastViewKey = kRenderLeftExternalKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = 0;
	    renderFrame();
	    require(g_viewHeading == -kAngleQuarterTurn &&
	                g_viewPitch == 0 &&
	                g_viewRoll == 0 &&
	                g_camEyeX == kRenderViewX +
	                    (kRenderExternalOffsetBase << g_externalCamDist) &&
	                g_camEyeY == kRenderViewY,
	            "renderFrame preserves original left external camera offset");

	    resetRenderRecorders();
	    keyValue = kRenderNorthExternalKey;
	    g_lastViewKey = kRenderNorthExternalKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    renderFrame();
	    require(static_cast<uint16>(g_viewHeading) == kAngleHalfTurn &&
	                g_viewPitch == 0 &&
	                g_viewRoll == 0 &&
	                g_camEyeY == kRenderViewY +
	                    (kRenderExternalOffsetBase << g_externalCamDist),
	            "renderFrame preserves original north external camera offset");

	    resetRenderRecorders();
	    keyValue = kRenderChaseExternalKey;
	    g_lastViewKey = kRenderChaseExternalKey;
	    g_hudVisible = 0;
	    g_detailLevel = 0;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_ourHead = 0;
	    renderFrame();
	    require(g_viewHeading == 0 &&
	                g_camEyeX == kRenderViewX &&
	                g_camEyeY == kRenderViewY -
	                    (kRenderExternalOffsetBase << g_externalCamDist) &&
	                g_camEyeZ == kRenderViewZ + (kRenderChaseAltBase << g_externalCamDist) &&
	                g_horizonGroundColor == kRenderLowDetailGroundColor &&
	                static_cast<unsigned char>(g_skyColorIndex) == kRenderLowDetailSkyColor,
	            "renderFrame preserves original chase camera offset and low-detail horizon colors");

	    resetRenderRecorders();
	    keyValue = kRenderDirectorKey;
	    g_lastViewKey = kRenderDirectorKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_directorMode = 0;
	    g_lastMissileSlot = kRenderMissileSlot;
	    g_externalCamDist = kRenderInitialCamDist;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewX_ = kRenderViewX >> 5;
	    g_viewY_ = kRenderViewY >> 5;
	    g_viewZ = kRenderViewZ;
	    g_projectiles[kRenderMissileSlot].ttl = kRenderMissileTtl;
	    g_projectiles[kRenderMissileSlot].mapX = kRenderMissileMapX;
	    g_projectiles[kRenderMissileSlot].mapY = kRenderMissileMapY;
	    g_projectiles[kRenderMissileSlot].alt = kRenderMissileAlt;
	    g_projectiles[kRenderMissileSlot].worldX = kAngleQuarterTurn;
	    g_projectiles[kRenderMissileSlot].worldY = 0;
	    renderFrame();
	    require(g_viewTargetObj == kRenderMissileSlot &&
	                g_viewTargetX == static_cast<long>(g_projectiles[kRenderMissileSlot].mapX) << 5 &&
	                g_viewTargetY == static_cast<long>(g_projectiles[kRenderMissileSlot].mapY) << 5 &&
	                g_viewHeading == kAngleQuarterTurn,
	            "renderFrame preserves original director missile camera target lookup");

	    resetRenderRecorders();
	    keyValue = kRenderDirectorKey;
	    g_lastViewKey = kRenderDirectorKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_directorMode = 1;
	    g_lastMissileSlot = kRenderMissileSlot;
	    g_viewTargetObj = kRenderMissileSlot;
	    g_projectiles[kRenderMissileSlot].ttl = 0;
	    g_ourHead = kRenderHead;
	    g_ourPitch = kRenderPitch;
	    renderFrame();
	    require(keyValue == kRenderChaseExternalKey &&
	                g_projectiles[kRenderMissileSlot].worldX == kRenderHead &&
	                g_projectiles[kRenderMissileSlot].worldY == kRenderPitch,
	            "renderFrame preserves original expired director missile fallback");

	    resetRenderRecorders();
	    keyValue = kRenderTargetRearKey;
	    g_lastViewKey = kRenderTargetRearKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_currentWeaponType = 1;
	    g_airTargetLock = 0;
	    g_directorMode = 0;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewX_ = kRenderViewX >> 5;
	    g_viewY_ = kRenderViewY >> 5;
	    g_viewZ = kRenderViewZ;
	    g_simObjects[0].worldX = kRenderViewX + kRenderSimObjectDeltaX;
	    g_simObjects[0].worldY = kRenderViewY + kRenderSimObjectDeltaY;
	    g_simObjects[0].alt = kRenderViewZ + kRenderSimObjectDeltaAlt;
	    renderFrame();
	    require(g_viewTargetObj == kRenderSimObjectViewBase &&
	                g_viewTargetX == g_simObjects[0].worldX &&
	                g_viewTargetY == g_simObjects[0].worldY &&
	                g_viewTargetAlt == g_simObjects[0].alt,
	            "renderFrame preserves original rear target camera for sim objects");

	    resetRenderRecorders();
	    keyValue = kRenderTargetOrbitKey;
	    g_lastViewKey = kRenderTargetOrbitKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_currentWeaponType = kRenderGroundWeaponType;
	    g_groundTargetLock = 0;
	    g_directorMode = 0;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewX_ = kRenderViewX >> 5;
	    g_viewY_ = kRenderViewY >> 5;
	    g_viewZ = kRenderViewZ;
	    g_planeTable.planes[0].mapX = kRenderMissileMapX;
	    g_planeTable.planes[0].mapY = kRenderGroundTargetMapY;
	    g_planeTable.planes[0].flags = kStepCarrierFlag;
	    renderFrame();
	    require(g_viewTargetObj == kRenderGroundTargetViewBase &&
	                g_viewTargetX == static_cast<long>(g_planeTable.planes[0].mapX) << 5 &&
	                g_viewTargetY == static_cast<long>(g_planeTable.planes[0].mapY) << 5 &&
	                g_viewTargetAlt == kRenderCarrierTargetAlt,
	            "renderFrame preserves original ground-target orbit camera lookup");

	    resetRenderRecorders();
	    keyValue = kRenderDirectorKey;
	    g_lastViewKey = kRenderDirectorKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_directorMode = 0;
	    g_lastMissileSlot = kRenderMissileSlot;
	    g_ViewX = kRenderViewX;
	    g_ViewY = kRenderViewY;
	    g_viewZ = kRenderViewZ;
	    g_projectiles[kRenderMissileSlot].ttl = kRenderMissileTtl;
	    g_projectiles[kRenderMissileSlot].mapX = kRenderMissileMapX;
	    g_projectiles[kRenderMissileSlot].mapY = kRenderMissileMapY;
	    g_projectiles[kRenderMissileSlot].alt = kRenderMissileAlt;
	    g_projectiles[kRenderMissileSlot].worldX = 0x1111;
	    g_projectiles[kRenderMissileSlot].worldY = kRenderProjectileHighPitch;
	    renderFrame();
	    require(static_cast<uint16>(g_viewPitch) == kRenderProjectilePitchAfterFlip &&
	                static_cast<uint16>(g_viewHeading) == kRenderProjectileHeadingAfterFlip &&
	                static_cast<uint16>(g_viewRoll) == kAngleHalfTurn,
	            "renderFrame preserves original projectile-camera pitch flip when pitch exceeds the vertical limit");

	    resetRenderRecorders();
	    keyValue = kRenderCrashViewKey;
	    g_lastViewKey = kRenderCrashViewKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_crashCamX = kRenderCrashCamX;
	    g_crashCamY = kRenderCrashCamY;
	    g_crashCamZ = kRenderCrashCamZ;
	    renderFrame();
	    require(static_cast<uint16>(g_viewPitch) == kRenderCrashViewPitch &&
	                g_viewRoll == 0 &&
	                g_camEyeX == (static_cast<int32>(kRenderCrashCamX) << 5) &&
	                g_camEyeY == ((0x8000 - static_cast<int32>(kRenderCrashCamY)) << 5) &&
	                g_camEyeZ == kRenderCrashCamZ,
	            "renderFrame preserves original ejection/crash camera eye coordinates");

	    resetRenderRecorders();
	    keyValue = kRenderTargetOrbitKey;
	    g_lastViewKey = kRenderTargetOrbitKey;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_currentWeaponType = kRenderGroundWeaponType;
	    g_groundTargetLock = 0;
	    g_directorMode = 1;
	    g_ViewX = static_cast<long>(kRenderMissileMapX) << 5;
	    g_ViewY = static_cast<long>(kRenderGroundTargetMapY) << 5;
	    g_viewX_ = kRenderMissileMapX;
	    g_viewY_ = kRenderGroundTargetMapY;
	    g_viewZ = 1000;
	    g_planeTable.planes[0].mapX = kRenderMissileMapX;
	    g_planeTable.planes[0].mapY = kRenderGroundTargetMapY;
	    g_planeTable.planes[0].flags = kStepCarrierFlag;
	    renderFrame();
	    require(g_viewTargetAlt == kRenderCarrierTargetAlt &&
	                g_camEyeZ == kRenderCarrierDeckMinAlt,
	            "renderFrame preserves original carrier orbit camera deck-height clamp");

	    resetRenderRecorders();
	    keyValue = 0;
	    g_lastViewKey = 0;
	    g_hudVisible = 0;
	    g_detailLevel = 1;
	    g_mapMode = 0;
	    g_activePanelMode = 0;
	    g_setThrust = kThrottleMilitary;
	    missileSpecIndex = 1;
	    renderFrame();
	    require(g_hudVisible == 1 &&
	                g_testCopyRectCalls == kExpectedTwoCalls &&
	                g_testWeaponAmmoCalls == 1 &&
	                g_testWeaponMarkerCalls == 1 &&
	                g_testRedrawTacMapCalls == 1 &&
	                g_testPanelBoxCalls == 1 &&
	                g_groundTargetLock == static_cast<int16>(0xffff) &&
	                g_airTargetLock == static_cast<int16>(0xffff) &&
	                g_lockedTargetKilled == 0,
	            "renderFrame restores the original HUD and target panel state after returning from an external view");

		    resetFlightInputRecorders();
    g_frameTimingAccum = kSavedFrameTiming;
    g_testKeys[0] = kPauseKey;
    g_testKeys[1] = kResumeKey;
    g_testKeyCount = 2;
    waitForKeyPress();
    require(g_testAudioEngineOffCalls == 1 &&
                g_testUpdateEngineSoundCalls == 1 &&
                g_testKeyIndex == 2 &&
                g_frameTimingAccum == kSavedFrameTiming,
            "waitForKeyPress loops through pause keys, resumes on the next key, and restores frame timing");

    std::cout << "original_behavior_tests passed\n";
    return 0;
}
