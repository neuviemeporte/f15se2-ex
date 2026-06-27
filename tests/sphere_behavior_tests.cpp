#include "egdata.h"
#include "egtypes.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern void drawProjectionSphere(int skyColor);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SphereOriginalConstant : int {
    kLowDetailLevel = 2,
    kHighDetailLevel = 3,
    kSkyColor = 0x0C,
    kSphereColor = 0x05,
    kSphereRadius = 0x200,
    kSphereDistMin = 0x200,
    kSpherePitch = 0x1234,
    kSphereRoll = 0,
    kViewCenterX = 160,
    kViewCenterY = 100,
    kViewPosZ = 0x2000,
    kSphereRingCount = 16,
    kSpherePolygonCount = 32,
    kExtraScaleShiftOne = 1,
    kHalfScaleEnabled = 1,
    kUpperColorFirst = 0x60,
    kUpperColorLast = 0x6F,
    kLowerColorFirst = 0x70,
    kLowerColorLast = 0x7F,
    kPolygonPointCount = 4,
    kExpectedOneCall = 1,
    kExpectedNoCalls = 0,
    kTestFailureExitCode = 1,
};

struct PolygonCall {
    int fillColor;
    int pointCount;
    int edgeColor;
};

PolygonCall g_polygonCalls[kSpherePolygonCount] = {};
int g_polygonCallCount = 0;
int g_flatHorizonCalls = 0;
int g_lastFlatSkyColor = -1;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetSphereState() {
    std::memset(g_sphereRingRadii, 0, sizeof(int16) * kSphereRingCount);
    std::memset(g_polygonCalls, 0, sizeof(g_polygonCalls));
    g_polygonCallCount = 0;
    g_flatHorizonCalls = 0;
    g_lastFlatSkyColor = -1;
    g_detailLevel = kHighDetailLevel;
    g_sphereColor = kSphereColor;
    g_sphereRadius = kSphereRadius;
    g_sphereDistZ = kSphereDistMin;
    g_spherePitch = kSpherePitch;
    g_sphereRoll = kSphereRoll;
    g_extraScaleShift = 0;
    g_halfScaleRender = 0;
    g_viewCenterX = kViewCenterX;
    g_viewCenterY = kViewCenterY;
    g_viewPosZ = kViewPosZ;
}

} // namespace

int fixedMulQ14(int a, int b) {
    return static_cast<int>((static_cast<long>(a) * static_cast<long>(b)) >> 14);
}

int far drawFlatHorizon(int skyColor) {
    ++g_flatHorizonCalls;
    g_lastFlatSkyColor = skyColor;
    return 0;
}

int far drawPolygonOutline(int fillColor, int pointCount, int *, int edgeColor) {
    require(g_polygonCallCount < kSpherePolygonCount,
            "drawProjectionSphere emits no more than the original 32 ring polygons");
    g_polygonCalls[g_polygonCallCount++] = {fillColor, pointCount, edgeColor};
    return 0;
}

int main() {
    resetSphereState();
    g_detailLevel = kLowDetailLevel;
    drawProjectionSphere(kSkyColor);
    require(g_flatHorizonCalls == kExpectedOneCall &&
                g_lastFlatSkyColor == kSkyColor &&
                g_polygonCallCount == kExpectedNoCalls,
            "drawProjectionSphere falls back to the original flat horizon below detail level 3");

    resetSphereState();
    drawProjectionSphere(kSkyColor);
    require(g_flatHorizonCalls == kExpectedNoCalls &&
                g_polygonCallCount == kSpherePolygonCount,
            "drawProjectionSphere emits the original two 16-ring sphere bands at high detail");
    for (int idx = 0; idx < kSphereRingCount; ++idx) {
        require(g_polygonCalls[idx].fillColor == kSphereColor &&
                    g_polygonCalls[idx].pointCount == kPolygonPointCount &&
                    g_polygonCalls[idx].edgeColor == kUpperColorFirst + idx,
                "drawProjectionSphere emits the original upper-band polygon colors");
        require(g_polygonCalls[kSphereRingCount + idx].fillColor == kSphereColor &&
                    g_polygonCalls[kSphereRingCount + idx].pointCount == kPolygonPointCount &&
                    g_polygonCalls[kSphereRingCount + idx].edgeColor == kLowerColorFirst + idx,
                "drawProjectionSphere emits the original lower-band polygon colors");
    }
    require(g_polygonCalls[kSphereRingCount - 1].edgeColor == kUpperColorLast &&
                g_polygonCalls[kSpherePolygonCount - 1].edgeColor == kLowerColorLast,
            "drawProjectionSphere preserves the original final ring edge colors");

    resetSphereState();
    g_extraScaleShift = kExtraScaleShiftOne;
    g_halfScaleRender = kHalfScaleEnabled;
    drawProjectionSphere(kSkyColor);
    require(g_flatHorizonCalls == kExpectedNoCalls &&
                g_polygonCallCount == kSpherePolygonCount,
            "drawProjectionSphere preserves original high-detail output count when both scale flags are set");

    std::cout << "sphere_behavior_tests passed\n";
    return 0;
}
