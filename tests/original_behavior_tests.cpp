// EGAME flight-math + attitude/orientation behavior tests (LINK_CORE).
//
// Exercises the real deterministic math against the linked core library: the
// trig/fixed-point primitives (sine/cosine/fixedMulQ14/shiftLong*/clamp/bearing/
// signedRatio16/valueToAngle/complementAngle/isqrt/hudPitchScale/randomRange) and
// the orientation pipeline (rebuildOrientation/computeAttitudeAngles/
// applyRotationDelta), all pure functions of game state. Golden values are
// cross-checked against independent reimplementations that deliberately hit the
// DOS edge cases (0x8000 abs-overflow, 16-bit wraparound, Q15 rounding).
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
extern void applyRotationDelta(const int16 *matA, const int16 *matB);
extern void computeAttitudeAngles(void);
extern void rebuildOrientation(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
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
    kQ15Identity = 32767,
    kQ15ZeroAngleProduct = 32766,
    kRotationDirtyPeriod = 8,
    kAttitudeWideComponent = 0x6000,
    kAttitudeNarrowComponent = 0x0400,
    kAttitudePitchQuarterTurnInput = -32767,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
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

int main() {
    // --- trig lookup: sine / cosine / hudSine -------------------------------
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

    // --- fixed-point multiply + 32-bit shift helpers ------------------------
    const int wordSamples[] = {
        -32768, -32767, -20000, -16384, -1, 0, 1, 12345, 16384, 20000, 32766, 32767,
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

    // --- clamp / sign helpers -----------------------------------------------
    const int clampSamples[] = {
        -32768, -20000, kClampWrapFloor, kClampWrapFloor + 1, -11, -10, -9, 0, 9, 10, 11, 20000, 32767,
    };
    for (int value : clampSamples) {
        require(clampRange(value, -10, 10) == expectedClampRange(value, -10, 10),
                "clampRange preserves wrap-floor high clamp");
        require(egClampValue(value, -10, 10) == (value > 10 ? 10 : (value < -10 ? -10 : value)),
                "egClampValue is plain clamp");
        require(signOf(value) == (value == 0 ? 0 : (value > 0 ? 1 : -1)), "signOf matches original");
    }

    // --- distance approximation + bearing quadrant curve --------------------
    for (auto [dx, dy] : {
             std::pair{0, 0}, std::pair{10, 4}, std::pair{-10, 4}, std::pair{4, -10},
             std::pair{-4, -10}, std::pair{40000, 40000}, std::pair{-32768, 0},
             std::pair{0, -32768}, std::pair{32767, -32768},
         }) {
        require(rangeApprox(dx, dy) == expectedRangeApprox(dx, dy), "rangeApprox matches original cap");
    }
    for (auto [dx, dy] : {
             std::pair{0, 1}, std::pair{0, -1}, std::pair{1, 0}, std::pair{-1, 0},
             std::pair{100, 100}, std::pair{100, -50}, std::pair{-50, 100}, std::pair{-100, -20},
             std::pair{7, -123}, std::pair{123, 7}, std::pair{-123, 7}, std::pair{7, 123},
             std::pair{-7, -123}, std::pair{32767, 1}, std::pair{1, 32767},
         }) {
        require(static_cast<uint16>(computeBearing(dx, dy)) == static_cast<uint16>(expectedBearing(dx, dy)),
                "computeBearing matches original quadrant approximation");
    }

    // --- sinMul / cosMul ----------------------------------------------------
    for (auto [angle, scalar] : {
             std::pair<int, int>{0, 1000}, std::pair<int, int>{0x1234, -2000},
             std::pair<int, int>{kAngleQuarterTurn, 32767}, std::pair<int, int>{kAngleHalfTurn, -32768},
             std::pair<int, int>{0xFFFF, 1}, std::pair<int, int>{0x80FF, 16384},
         }) {
        require(sinMul(angle, scalar) == expectedFixedMulQ14(expectedSine(angle), scalar),
                "sinMul matches original fixed multiply");
        require(cosMul(angle, scalar) == expectedFixedMulQ14(expectedSine(angle + kAngleQuarterTurn), scalar),
                "cosMul matches original fixed multiply");
    }

    // --- signed ratio + inverse trig (valueToAngle / complementAngle) -------
    for (auto [num, den] : {
             std::pair{0, 1}, std::pair{1, 1}, std::pair{-1, 1}, std::pair{1, -1}, std::pair{-1, -1},
             std::pair{16383, 32767}, std::pair{-16383, 32767}, std::pair{32767, 32767},
             std::pair{-32767, 32767}, std::pair{-12000, -24000}, std::pair{12345, 23456},
         }) {
        require(static_cast<uint16>(signedRatio16(num, den)) == expectedSignedRatio16(num, den),
                "signedRatio16 returns original 16-bit quotient bit pattern");
    }
    const int inverseTrigSamples[] = {
        -32768, -32767, -4096, -2048, -513, -512, -1, 0, 1, 511, 512, 2048, 4096, 32766, 32767,
    };
    for (int value : inverseTrigSamples) {
        require(static_cast<uint16>(valueToAngle(value)) == static_cast<uint16>(expectedValueToAngle(value)),
                "valueToAngle matches original table interpolation");
        require(static_cast<uint16>(complementAngle(value)) ==
                    static_cast<uint16>(kAngleQuarterTurn - expectedValueToAngle(value)),
                "complementAngle is quarter turn minus valueToAngle");
    }

    // --- isqrt / hudPitchScale / randomRange --------------------------------
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

    // --- rebuildOrientation + computeAttitudeAngles round trip --------------
    // Orientation math is pure: rebuildOrientation builds g_orientMatrix from the
    // Euler attitude via the real buildRotationMatrixFar; computeAttitudeAngles
    // recovers the attitude (valueToAngle/complementAngle/signedRatio16 path).
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
    g_orientMatrix[5] = kQ15Identity; /* +1.0 sine input maps pitch to the -90 degree dirty band. */
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

    // --- applyRotationDelta matrix product + periodic dirty flag ------------
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

    std::cout << "original_behavior_tests passed\n";
    return 0;
}
