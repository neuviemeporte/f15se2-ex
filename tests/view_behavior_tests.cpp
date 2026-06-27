#include "egdata.h"
#include "egtypes.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern void loadRegion3D(void);
extern void render3DView(int camX, int camY, int camZ, long worldX, long worldY, long worldZ,
                         int clipLeft, int clipTop, int clipWidth, int clipHeight);
extern void waitFrameSync(int frames);
extern void loadColorPalette(int idx);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum ViewOriginalConstant : int {
    kPaletteIndex = 2,
    kPaletteRecordBytes = 16,
    kWaitFrames = 3,
    kInitialTick = 10,
    kExpectedYieldCalls = 3,
    kDisplayPage = 1,
    kViewParamWords = 11,
    kClipLeft = 7,
    kClipTop = 11,
    kClipWidth = 30,
    kClipHeight = 20,
    kClipRight = kClipLeft + kClipWidth - 1,
    kClipBottom = kClipTop + kClipHeight - 1,
    kSkyColorIndex = 4,
    kSkyColor = 0x2A,
    kCamX = 100,
    kCamY = 200,
    kCamZ = 300,
    kWorldX = 0x11112222,
    kWorldY = 0x33334444,
    kWorldZ = 0x55556666,
    kTransformArgZero = 0,
    kTransformArgOne = 1,
    kExpectedOneCall = 1,
    kExpectedNoCalls = 0,
    kTestFailureExitCode = 1,
};

int g_load3DCalls = 0;
int g_yieldCalls = 0;
int g_setPageCalls = 0;
int g_lastSetPage = -1;
int g_transformCalls = 0;
int g_projectCalls = 0;
int g_updateTargetCalls = 0;
int g_rasterizeCalls = 0;
int g_overlayCalls = 0;

struct TransformSnapshot {
    const int16 *params;
    int camX;
    int camY;
    int camZ;
    int arg4;
    int arg5;
    int worldZ;
    int arg7;
} g_lastTransform = {};

struct ProjectSnapshot {
    int camX;
    int camY;
    long worldX;
    long worldY;
    long worldZ;
} g_lastProject = {};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetViewState() {
    std::memset(colorLut, 0, sizeof(uint8) * 0x100);
    std::memset(g_viewParams, 0, sizeof(int16) * kViewParamWords);
    g_timerTickByte[0] = 0;
    g_renderPageToggle = 0;
    g_skyColorIndex = 0;
    g_load3DCalls = 0;
    g_yieldCalls = 0;
    g_setPageCalls = 0;
    g_lastSetPage = -1;
    g_transformCalls = 0;
    g_projectCalls = 0;
    g_updateTargetCalls = 0;
    g_rasterizeCalls = 0;
    g_overlayCalls = 0;
    g_lastTransform = {};
    g_lastProject = {};
}

} // namespace

void load3DAll(void) {
    ++g_load3DCalls;
}

int FAR CDECL gfx_getDisplayPage(void) {
    return kDisplayPage;
}

void FAR CDECL gfx_setPageN(uint16 pageNum) {
    ++g_setPageCalls;
    g_lastSetPage = pageNum;
}

void timerYield(void) {
    ++g_yieldCalls;
    ++g_timerTickByte[0];
}

void setup3DTransform(const int16 *params, int camX, int camY, int camZ, int arg4, int arg5, int worldZ, int arg7) {
    ++g_transformCalls;
    g_lastTransform = {params, camX, camY, camZ, arg4, arg5, worldZ, arg7};
}

void projectObjects(int camX, int camY, long worldX, long worldY, long worldZ) {
    ++g_projectCalls;
    g_lastProject = {camX, camY, worldX, worldY, worldZ};
}

void updateTargetLock(void) {
    ++g_updateTargetCalls;
}

void rasterize3DWorld(void) {
    ++g_rasterizeCalls;
}

void drawHudWorldOverlay(void) {
    ++g_overlayCalls;
}

int main() {
    resetViewState();
    loadRegion3D();
    require(g_load3DCalls == kExpectedOneCall,
            "loadRegion3D delegates to the original all-region 3D loader");

    resetViewState();
    loadColorPalette(kPaletteIndex);
    require(std::memcmp(colorLut,
                        g_colorPalettes + kPaletteIndex * kPaletteRecordBytes,
                        kPaletteRecordBytes) == 0,
            "loadColorPalette copies the original 16-byte palette record into colorLut");

    resetViewState();
    g_timerTickByte[0] = kInitialTick;
    waitFrameSync(kWaitFrames);
    require(g_yieldCalls == kExpectedYieldCalls &&
                g_timerTickByte[0] == static_cast<uint8>(kInitialTick + kWaitFrames),
            "waitFrameSync yields until the original target tick byte is reached");
    waitFrameSync(0);
    require(g_yieldCalls == kExpectedYieldCalls,
            "waitFrameSync ignores non-positive frame counts");

    resetViewState();
    colorLut[kSkyColorIndex] = kSkyColor;
    g_skyColorIndex = kSkyColorIndex;
    render3DView(kCamX, kCamY, kCamZ, kWorldX, kWorldY, kWorldZ,
                 kClipLeft, kClipTop, kClipWidth, kClipHeight);
    require(g_viewParams[7] == kClipTop && g_viewParams[8] == kClipBottom &&
                g_viewParams[9] == kClipLeft && g_viewParams[10] == kClipRight,
            "render3DView stores the original inclusive clip rectangle in view params");
    require(g_viewParams[0] == kDisplayPage && g_viewParams[2] == kSkyColor,
            "render3DView stores original display page and sky color index in view params");
    require(g_setPageCalls == kExpectedOneCall && g_lastSetPage == kDisplayPage,
            "render3DView selects the display page in the native renderer path");
    require(g_transformCalls == kExpectedOneCall &&
                g_lastTransform.params == g_viewParams &&
                g_lastTransform.camX == kCamX &&
                g_lastTransform.camY == kCamY &&
                g_lastTransform.camZ == kCamZ &&
                g_lastTransform.arg4 == kTransformArgZero &&
                g_lastTransform.arg5 == kTransformArgZero &&
                g_lastTransform.worldZ == static_cast<int>(kWorldZ) &&
                g_lastTransform.arg7 == kTransformArgOne,
            "render3DView forwards the original transform arguments");
    require(g_projectCalls == kExpectedOneCall &&
                g_lastProject.camX == kCamX &&
                g_lastProject.camY == kCamY &&
                g_lastProject.worldX == kWorldX &&
                g_lastProject.worldY == kWorldY &&
                g_lastProject.worldZ == kWorldZ,
            "render3DView forwards the original object projection arguments");
    require(g_updateTargetCalls == kExpectedOneCall &&
                g_rasterizeCalls == kExpectedOneCall &&
                g_overlayCalls == kExpectedOneCall &&
                g_renderPageToggle == kExpectedOneCall,
            "render3DView runs the original target, raster, overlay, and page-toggle steps");
    require(g_yieldCalls == kExpectedNoCalls,
            "render3DView leaves frame pacing to the intentional fixed-timestep loop");

    std::cout << "view_behavior_tests passed\n";
    return 0;
}
