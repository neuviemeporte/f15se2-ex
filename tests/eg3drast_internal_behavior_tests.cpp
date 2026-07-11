// Software-rasterizer internals (eg3drast.c) behavior tests.
//
// The tested primitives (udiv32by16/hsine/q15hi/projectVertexToScreen/clip*/
// rasterizeEdgeSpan/drawPolygonOutline/drawFlatHorizon/...) are `static` inside
// eg3drast.c, so this TU #includes the .c directly (SOURCES isolation) — LINK_CORE
// cannot reach file-local symbols. The inline gfx_*/drawClipLineGlobal stubs are
// legitimate output observers for the rasterizer, not LINK_CORE collisions. The
// rasterizer is a live backend; its span/edge/clip/projection math is unchanged.
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../src/eg3drast.c"

/* eg3drast.c now contains software replacement-GLB helpers that map material RGB
 * through gfx_impl.c in normal builds. This internal harness intentionally does
 * not link gfx_impl.c, so provide a deterministic palette stub; the tests here
 * exercise rasterizer math, not active DAC nearest-colour search. */
int gfx_nearestPaletteIndexRgb8(uint8 r, uint8 g, uint8 b) {
    (void)g;
    (void)b;
    return r & 0x0f;
}

/* Backs egdata.c's regnStr global (defined in stdata.c, not linked here). MSVC
 * links whole objects so this reference needs a definition; unused by the test. */
char aRegn_xxx[] = "regn.xxx";

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum Eg3dRastInternalOriginalConstant : int {
    kTestFailureExitCode = 1,
    kUnsignedDivideOverflowSentinel = 0x7f00,
    kClipRightBit = 0x01,
    kClipAboveBit = 0x02,
    kClipBelowBit = 0x04,
    kClipLeftBit = 0x08,
    kClipMaxX = 319,
    kClipMaxY = 111,
    kViewCenterX = 160,
    kViewCenterY = 100,
    kIdentityQ15 = 32767,
    kQ15ZeroAngleProduct = 32766,
    kHalfTurnAngle = 0x8000,
    kQuarterTurnAngle = 0x4000,
    kDepthHi = 2,
    kDepthLo = 0x3456,
    kDepthPacked = 0x23456,
    kHorizonSkyColor = 0x22,
    kHorizonGroundColor = 0x33,
    kPolygonEdgeColor = 0x44,
    kDirtyUnset = -1,
    kDirtyClearedMax = 0,
    kNearDepthThreshold = 1,
    kEdgeRunNearColorIndex = 0x0f,
    kEdgeRunMidColorIndex = 7,
    kEdgeRunFarColorIndex = 8,
    kVisibilityMaskLo = 0x0001,
    kVisibilityMaskHi = 0x0002,
    kPrimitiveCommandCount = 1,
    kPrimitiveLineOpcode = 0,
    kPrimitiveEdgeIndex = 0,
    kPrimitiveColorIndex = 1,
    kPrimitiveHiddenColor = 0xff,
    kPrimitiveHighWordFaceOpcode = 0x81,
    kPrimitiveX1 = 12,
    kPrimitiveY1 = 13,
    kPrimitiveX2 = 40,
    kPrimitiveY2 = 41,
    kPrimitiveSpanMinY = 4,
    kPrimitiveSpanMaxY = 6,
    kFilledFaceOpcode = 1,
    kFilledFaceVertexCount = 3,
    kObjectColorBaseSkip = 0x400,
    kRejectedEdgeFlag = 0x80,
    kClipInsideX = 20,
    kClipInsideY = 30,
    kClipOutsideLeftX = -10,
    kClipOutsideRightX = 400,
    kClipOutsideTopY = -10,
    kClipOutsideBottomY = 140,
    kClipAcceptedFlag = 0x40,
    kClipP1BoundaryFlag = 0x20,
    kClipP2BoundaryFlag = 0x10,
    kClipRejectedFlag = 0x80,
    kClipTopRejectY = -5,
    kClipLeftRejectX = -5,
    kClipCrossLeftX = -20,
    kClipCrossRightX = 400,
    kClipCrossY1 = 10,
    kClipCrossY2 = 90,
    kClipOutsideBottomY2 = 150,
    kSceneRelY = 100,
    kSceneDepthHi = 99,
    kSceneVertexCoord = 100,
    kInlineVertexCount = 1,
    kIndexedVertexCountFlag = 0x81,
    kNoFaceCount = 0,
    kFaceCountOne = 1,
    kFaceRecordBytes = 8,
    kFaceThresholdForFlip = 1,
    kPointOpcode = 0x3f,
    kEdgeRunOpcode = 0x3e,
    kEdgeRunCount = 1,
    kSortedOpcode = 0x40,
    kRenderMode = 0,
    kRenderModeFive = 5,
    kLodCountNoSkip = 0,
    kSortedCapacity = 35,
    kTransformOpcodeBase = 0x60,
    kTransformOpcodeIndex = 2,
    kSpinAngle = 0x1234,
    kDacDepthHi = 0x1000,
    kDacShade = 0xf0,
    kLodSkipOpcode = 0x80,
    kLodShortAdvance = 3,
    kLodLongAdvance = 6,
    kEdgeRunOffscreenRef = 0,
    kRleBranchRoot = 0,
    kRleBranchLeaf = 1,
    kRleSecondLeaf = 2,
    kHugeClipNegative = -70000,
    kHugeClipPositive = 70000,
    kDescendingLineTop = 40,
    kDescendingLineBottom = 10,
    kRleDisplayListBytes = 16,
    kRleDisplayListTwoEdgeBytes = 32,
    kClipMidpointPositive = 100,
    kClipMidpointNegative = -100,
    kClipSliverX1 = -500,
    kClipSliverY1 = -300,
    kClipSliverX2 = 0,
    kClipSliverY2 = 120,
    kClipInteriorA = 10,
    kClipInteriorB = 20,
    kSortTieDepthHi = 1,
    kSortTieDepthLoExisting = 0x2000,
    kSortTieDepthLoNew = 0x1000,
};

enum Eg3dRastInternalOriginalUnsignedConstant : unsigned {
    kProjectedInvalidCoord = 0x80008000u,
    kUnsignedFullDivideByZeroSentinel = 0xffffffffu,
};

int g_gfxSetColorCalls = 0;
int g_gfxDirtyRectCalls = 0;
int g_gfxDrawLineCalls = 0;
int g_gfxNop22Calls = 0;
int g_drawClipLineCalls = 0;
int g_lastGfxColor = 0;
int g_lastDirtyYMin = 0;
int g_lastDirtyYMax = 0;
uint16 g_lastLine[4] = {};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetDrawStubs() {
    g_gfxSetColorCalls = 0;
    g_gfxDirtyRectCalls = 0;
    g_gfxDrawLineCalls = 0;
    g_gfxNop22Calls = 0;
    g_drawClipLineCalls = 0;
    g_lastGfxColor = 0;
    g_lastDirtyYMin = 0;
    g_lastDirtyYMax = 0;
    std::memset(g_lastLine, 0, sizeof(g_lastLine));
}

void resetRasterState() {
    g_clipMaxX = kClipMaxX;
    g_clipMaxY = kClipMaxY;
    g_dirtyRectMinY = kDirtyUnset;
    g_dirtyRectMaxY = kDirtyClearedMax;
    for (int y = 0; y < 220; ++y) {
        g_spanBuf.minX[y] = -1;
        g_spanBuf.maxX[y] = 0;
    }
}

void setDepthWords(int vtx, int lo, int hi) {
    vtxScratch.vproj.in[vtx].num = static_cast<int16>(lo);
    vtxScratch.vproj.in[vtx].div = static_cast<int16>(hi);
}

void writeLe16(unsigned char *p, int value) {
    p[0] = static_cast<unsigned char>(value & 0xff);
    p[1] = static_cast<unsigned char>((value >> 8) & 0xff);
}

void setIdentityViewMatrix() {
    std::memset(g_viewRotMatrix, 0, sizeof(g_viewRotMatrix));
    g_viewRotMatrix[0] = kIdentityQ15;
    g_viewRotMatrix[4] = kIdentityQ15;
    g_viewRotMatrix[8] = kIdentityQ15;
}

void setIdentityObjectMatrix() {
    std::memset(g_objCombinedMatrix, 0, sizeof(g_objCombinedMatrix));
    g_objCombinedMatrix[0] = kIdentityQ15;
    g_objCombinedMatrix[4] = kIdentityQ15;
    g_objCombinedMatrix[8] = kIdentityQ15;
}

void resetSceneState() {
    resetDrawStubs();
    setIdentityViewMatrix();
    setIdentityObjectMatrix();
    g_overlayCenterX = g_overlayBaseX;
    g_overlayCenterY = g_overlayBaseY;
    g_halfScaleRender = 0;
    g_hudVisible = 0;
    g_objRenderMode = kRenderMode;
    g_objRelX = 0;
    g_objRelY = kSceneRelY;
    g_objTransform[0] = 0;
    g_objTransform[1] = 0;
    g_objTransform[2] = 0;
    g_objTransform[3] = 0;
    g_camBaseX = 0;
    g_camTransXLo = 0;
    g_camTransXHi = 0;
    g_camTransYLo = 0;
    g_camTransYHi = kSceneDepthHi;
    g_objShade = 0;
    g_objHasRotation = 0;
    g_posVisibleFlag = 0;
    g_sortedObjCount = 0;
    g_curLod = 0;
    g_detailLevel = 2;
    g_viewPosX = 0;
    g_viewPosY = 0;
    g_viewPosZ = 0;
    g_viewCenterX = kViewCenterX;
    g_viewCenterY = kViewCenterY;
    g_dacSupported = 0;
    g_modelWideVtxFlag = 0;
    g_vtxSignMask.Lo = kVisibilityMaskLo;
    g_vtxSignMask.Hi = 0;
}

} // namespace

void FAR CDECL gfx_setColor(int color) {
    ++g_gfxSetColorCalls;
    g_lastGfxColor = color;
}

void FAR CDECL gfx_dirtyRect(int16 *, int yMin, int yMax) {
    ++g_gfxDirtyRectCalls;
    g_lastDirtyYMin = yMin;
    g_lastDirtyYMax = yMax;
}

void FAR CDECL gfx_drawLine(uint16 x1, uint16 y1, uint16 x2, uint16 y2) {
    ++g_gfxDrawLineCalls;
    g_lastLine[0] = x1;
    g_lastLine[1] = y1;
    g_lastLine[2] = x2;
    g_lastLine[3] = y2;
}

void FAR CDECL gfx_nop22(void) {
    ++g_gfxNop22Calls;
}

int far drawClipLineGlobal(void) {
    ++g_drawClipLineCalls;
    return 0;
}

// Render-backend seam link-satisfiers (added since the base commit). Returning
// "vector inactive" keeps the integer span/edge rasterizer path under test; the
// native r2d poly / blit-offset calls sit behind r2d_vectorActive() and never run.
int r2d_vectorActive(void) { return 0; }
void r2d_submitPoly(const short *, int, int, int, int, int, int) {}
int FAR CDECL gfx_getBlitOffset() { return 0; }

int main() {
    int16 matrixA[9] = {};
    int16 matrixB[9] = {};
    int16 matrixR[9] = {};
    int polygonPoints[8] = {
        -10, 10,
        20, 10,
        20, 40,
        -10, 40,
    };
    unsigned char rleSrc[8] = {};
    unsigned char rleDst[8] = {};
    unsigned char rleBase[8] = {};

    require(udiv32by16(100, 5) == 20,
            "udiv32by16 preserves ordinary unsigned quotient");
    require(udiv32by16(0x80000000UL, 1) == kUnsignedDivideOverflowSentinel,
            "udiv32by16 saturates large quotients like the DOS divide trap");
    require(udiv32by16(100, 0) == kUnsignedDivideOverflowSentinel,
            "udiv32by16 uses the divide trap sentinel for zero denominator");
    require(udiv32by16_full(0x10000UL, 2) == 0x8000UL,
            "udiv32by16_full keeps the full quotient");
    require(udiv32by16_full(100, 0) ==
                static_cast<unsigned long>(kUnsignedFullDivideByZeroSentinel),
            "udiv32by16_full preserves the all-bits divide-by-zero value");
    require(sdivFull(-100, 5) == -20 &&
                sdivFull(100, -5) == -20 &&
                sdiv32by16(-100, -5) == 20,
            "signed 32/16 helpers apply signs after unsigned division");
    require(imul16(-7, 6) == -42 && lshr_s(-8, 1) == -4,
            "manual multiply and arithmetic shift preserve signed behavior");

    require(hsine(0) == 0 &&
                hcosine(0) == kIdentityQ15 &&
                hsine(kQuarterTurnAngle) == kIdentityQ15 &&
                hsine(kHalfTurnAngle) == 0,
            "local sine/cosine table helper matches cardinal angles");
    require(q15hi(kIdentityQ15, kIdentityQ15) == kQ15ZeroAngleProduct,
            "q15hi keeps the high word of the doubled Q15 product");
    require(q15sum(kIdentityQ15, kIdentityQ15, kIdentityQ15, kIdentityQ15, 0) == -4,
            "q15sum keeps DOS high-word wraparound for product sums");

    std::memset(matrixR, 0, sizeof(matrixR));
    require(buildRotationMatrixFar(matrixR, 0, 0, 0) == 0 &&
                matrixR[4] == kQ15ZeroAngleProduct &&
                matrixR[8] == kQ15ZeroAngleProduct,
            "buildRotationMatrixFar builds the original cardinal view matrix and sphere terms");

    {
        unsigned char lodStream[8] = {};
        writeLe16(colorLut + 0x10, 100);
        lodStream[0] = kLodSkipOpcode;
        writeLe16(lodStream + 1, kLodLongAdvance);
        lodStream[3] = 0;
        g_extraScaleShift = 0;
        g_objDistance = 50;
        g_modelStreamPtr = reinterpret_cast<char *>(lodStream);
        advanceModelPointerLod();
        require(g_modelStreamPtr == reinterpret_cast<char *>(lodStream + kLodShortAdvance),
                "advanceModelPointerLod preserves the original near-distance three-byte LOD advance");

        g_objDistance = 200;
        g_modelStreamPtr = reinterpret_cast<char *>(lodStream);
        advanceModelPointerLod();
        require(g_modelStreamPtr == reinterpret_cast<char *>(lodStream + kLodLongAdvance),
                "advanceModelPointerLod preserves the original far-distance table-skip advance");
    }

    {
        unsigned char opcodeStream[] = {static_cast<unsigned char>(kTransformOpcodeBase | kTransformOpcodeIndex)};
        g_modelStreamPtr = reinterpret_cast<char *>(opcodeStream);
        g_spinAngle = kSpinAngle;
        g_objTransform[kTransformOpcodeIndex] = 0;
        storeObjTransformByOpcode();
        require(g_objTransform[kTransformOpcodeIndex] == kSpinAngle,
                "storeObjTransformByOpcode writes spinAngle to the original opcode-selected transform slot");
    }

    setVtxDepth(3, kDepthPacked);
    require(vtxScratch.vproj.in[3].num == kDepthLo &&
                vtxScratch.vproj.in[3].div == kDepthHi &&
                getVtxDepth(3) == kDepthPacked,
            "vertex depth pack/unpack uses the original low/high word layout");

    g_viewCenterX = kViewCenterX;
    g_viewCenterY = kViewCenterY;
    g_halfScaleRender = 0;
    g_extraScaleShift = 0;
    VCAMX(0) = 0x10000L;
    VCAMY(0) = -0x10000L;
    setDepthWords(0, 0, 1);
    projectVertexToScreen(0);
    require(vtxScratch.vproj.x.v[0] == kViewCenterX + 256 &&
                vtxScratch.vproj.y.v[0] == kViewCenterY + 192,
            "projectVertexToScreen divides camera numerators by positive depth");
    setDepthWords(1, 0, 0);
    projectVertexToScreen(1);
    require(vtxScratch.vproj.x.v[1] == kProjectedInvalidCoord &&
                vtxScratch.vproj.y.v[1] == kProjectedInvalidCoord,
            "projectVertexToScreen marks non-positive depth with the DOS invalid coordinate");

    g_clipMaxX = kClipMaxX;
    g_clipMaxY = kClipMaxY;
    require(clipComputeOutcode(10, 10) == 0 &&
                (clipComputeOutcode(-1, 10) & kClipLeftBit) &&
                (clipComputeOutcode(kClipMaxX + 1, 10) & kClipRightBit) &&
                (clipComputeOutcode(10, -1) & kClipBelowBit) &&
                (clipComputeOutcode(10, kClipMaxY + 1) & kClipAboveBit),
            "clipComputeOutcode reports the original left/right/top/bottom bits");
    require(computeClipOutcode(10, 10) == 0 &&
                (computeClipOutcode(-1, kClipMaxY + 1) & (kClipLeftBit | kClipAboveBit)),
            "computeClipOutcode mirrors the 16-bit clipping helper");
    require(clipPointInside(0, 0, 20, 0) == 1 &&
                clipPointInside(20, 0, kClipMaxY, 0) == 1 &&
                clipPointInside(20, 0, 20, 0) == 0,
            "clipPointInside only accepts boundary points");
    require(pointOnClipEdge(0, 20) == 1 &&
                pointOnClipEdge(20, kClipMaxY) == 1 &&
                pointOnClipEdge(20, 20) == 0,
            "pointOnClipEdge recognizes exact clip rectangle edges");
    {
        int outY = 0;
        g_clipNeedsSubdiv = 0;
        require(clampToClipEdge(kClipLeftBit | kClipBelowBit, -100, -50, &outY) == 0 &&
                    outY == 0 &&
                    g_clipNeedsSubdiv == 0,
                "clampToClipEdge snaps small left/top coordinates without subdivision");
    }

    resetRasterState();
    g_lineX1 = 2;
    g_lineY1 = 2;
    g_lineX2 = 8;
    g_lineY2 = 5;
    rasterizeEdgeSpan();
    require(g_dirtyRectMinY == 2 &&
                g_dirtyRectMaxY == 5 &&
                g_spanBuf.minX[2] <= 2 &&
                g_spanBuf.maxX[5] >= 8,
            "rasterizeEdgeSpan records shallow edge extents and dirty rows");

    resetRasterState();
    g_lineX1 = 10;
    g_lineY1 = kDescendingLineTop;
    g_lineX2 = 30;
    g_lineY2 = kDescendingLineBottom;
    rasterizeEdgeSpan();
    require(g_dirtyRectMinY == kDescendingLineBottom &&
                g_dirtyRectMaxY == kDescendingLineTop &&
                g_spanBuf.minX[kDescendingLineBottom] == 30 &&
                g_spanBuf.minX[kDescendingLineTop] == 10,
            "rasterizeEdgeSpan preserves original descending-Y edge swap path");

    resetRasterState();
    g_lineX1 = -10;
    g_lineY1 = 10;
    g_lineX2 = 20;
    g_lineY2 = 30;
    require(clipAndRasterizeEdge() == 0 &&
                g_dirtyRectMinY >= 0 &&
                g_dirtyRectMaxY <= kClipMaxY &&
                g_spanBuf.minX[10] == 0,
            "clipAndRasterizeEdge fills the left boundary for an offscreen endpoint");
    require(crOutcode(-1, -1) == (kClipLeftBit | kClipBelowBit) &&
                clipMulDiv(10, -20, 5) == -40,
            "clip raster helpers keep compact outcode and signed ratio behavior");

    resetRasterState();
    resetDrawStubs();
    require(drawPolygonOutline(0, 4, polygonPoints, kPolygonEdgeColor) == 0 &&
                g_gfxSetColorCalls == 1 &&
                g_lastGfxColor == kPolygonEdgeColor &&
                g_gfxDirtyRectCalls == 1 &&
                g_gfxNop22Calls == 1,
            "drawPolygonOutline sets edge color, flushes spans, and calls the slot-22 no-op");

    resetRasterState();
    resetDrawStubs();
    g_spherePitch = 0;
    g_sphereRoll = kIdentityQ15;
    g_sphereRadius = 0;
    g_sphereDistZ = kIdentityQ15;
    g_viewCenterX = kViewCenterX;
    g_viewCenterY = kViewCenterY;
    g_horizonGroundColor = kHorizonGroundColor;
    g_detailLevel = 2;
    require(drawFlatHorizon(kHorizonSkyColor) == 0 &&
                g_gfxSetColorCalls >= 1 &&
                g_gfxDirtyRectCalls >= 1 &&
                g_gfxNop22Calls == 1,
            "drawFlatHorizon renders the low-detail flat horizon through span fills");

    resetRasterState();
    resetDrawStubs();
    g_spherePitch = 0;
    g_sphereRoll = kIdentityQ15;
    g_sphereRadius = 0;
    g_sphereDistZ = kIdentityQ15;
    g_viewCenterX = kViewCenterX;
    g_viewCenterY = kViewCenterY;
    g_horizonGroundColor = kHorizonGroundColor;
    g_detailLevel = 1;
    require(drawFlatHorizon(kHorizonSkyColor) == 0 &&
                g_gfxSetColorCalls >= 2 &&
                g_gfxDirtyRectCalls >= 2,
            "drawFlatHorizon preserves original two-pass sky and ground fill at higher detail");

    resetRasterState();
    resetDrawStubs();
    g_spherePitch = 0;
    g_sphereRoll = kIdentityQ15;
    g_sphereRadius = 0;
    g_sphereDistZ = 0; /* Original near-distance fallback scale. */
    g_viewCenterX = kViewCenterX;
    g_viewCenterY = kViewCenterY;
    g_detailLevel = 2;
    require(drawFlatHorizon(kHorizonSkyColor) == 0 &&
                g_gfxNop22Calls == 1,
            "drawFlatHorizon preserves original near-distance horizon scale fallback");

    resetRasterState();
    resetDrawStubs();
    g_spherePitch = 0;
    g_sphereRoll = kIdentityQ15;
    g_sphereRadius = -1;
    g_sphereDistZ = kIdentityQ15;
    g_viewCenterX = kViewCenterX;
    g_viewCenterY = 0; /* Degenerate horizon on the top boundary selects the full-viewport fill path. */
    g_detailLevel = 1;
    require(drawFlatHorizon(kHorizonSkyColor) == 0 &&
                g_gfxDirtyRectCalls == 1,
            "drawFlatHorizon preserves original degenerate-boundary full viewport fill");

    matrixA[0] = kIdentityQ15;
    matrixA[4] = kIdentityQ15;
    matrixA[8] = kIdentityQ15;
    matrixB[0] = kIdentityQ15;
    matrixB[4] = kIdentityQ15;
    matrixB[8] = kIdentityQ15;
    multiplyMatrix3x3(matrixA, matrixB, matrixR);
    require(matrixR[0] == kQ15ZeroAngleProduct &&
                matrixR[4] == kQ15ZeroAngleProduct &&
                matrixR[8] == kQ15ZeroAngleProduct,
            "multiplyMatrix3x3 keeps the doubled Q15 high-word identity product");
    require(multiplyMatrix3x3Far(matrixA, matrixB, matrixR) == 0,
            "multiplyMatrix3x3Far preserves the original zero return");

    buildInverseRotationMatrix(matrixA, 0, 0, 0);
    require(matrixA[0] == kQ15ZeroAngleProduct &&
                matrixA[4] == kQ15ZeroAngleProduct &&
                matrixA[8] == kQ15ZeroAngleProduct,
            "buildInverseRotationMatrix builds the original zero-angle inverse matrix");
    matrixA[1] = 12;
    matrixA[3] = 34;
    matrixA[2] = 56;
    matrixA[6] = 78;
    matrixA[5] = 90;
    matrixA[7] = 123;
    std::memcpy(g_objOrientMatrix, matrixA, sizeof(matrixA));
    transposeOrientationMatrix();
    require(g_objOrientMatrix[1] == 34 &&
                g_objOrientMatrix[3] == 12 &&
                g_objOrientMatrix[2] == 78 &&
                g_objOrientMatrix[6] == 56 &&
                g_objOrientMatrix[5] == 123 &&
                g_objOrientMatrix[7] == 90,
            "transposeOrientationMatrix swaps only the off-diagonal terms");

    require(edgeRunColor(1) == colorLut[kEdgeRunNearColorIndex] &&
                edgeRunColor(3000) == colorLut[kEdgeRunMidColorIndex] &&
                edgeRunColor(6000) == colorLut[kEdgeRunFarColorIndex],
            "edgeRunColor preserves near/mid/far color bands");

    require(erec(-1) == &g_erecTrash &&
                erec(EREC_COUNT) == &g_erecTrash &&
                erec(0) == reinterpret_cast<struct EdgeRec *>(flt15_buf2),
            "erec routes out-of-range edge indices to the trash record");

    rleSrc[0] = 0;
    rleSrc[1] = 0xff;
    rleSrc[2] = 0xff;
    rleBase[0] = 0xff;
    decodeRleEdgeRow(rleSrc, rleDst, rleBase);
    require(rleDst[0] == 0 && rleDst[1] == 0xff,
            "decodeRleEdgeRow emits a leaf edge and terminates with 0xff");

    {
        unsigned char branchSrc[8] = {};
        unsigned char branchDst[8] = {};
        unsigned char branchBase[8] = {};
        branchSrc[0] = kRleBranchRoot;
        branchSrc[1] = 0xff;
        branchSrc[2] = kRleBranchLeaf;
        branchSrc[3] = 0xff;
        branchSrc[4] = kRleSecondLeaf;
        branchSrc[5] = 0xff;
        branchSrc[6] = 0xff;
        branchBase[kRleBranchRoot] = 0;
        branchBase[kRleBranchLeaf] = 0xff;
        branchBase[kRleSecondLeaf] = 0xff;
        decodeRleEdgeRow(branchSrc, branchDst, branchBase);
        require(branchDst[0] == kRleBranchLeaf &&
                    branchDst[1] == kRleSecondLeaf &&
                    branchDst[2] == kRleBranchRoot &&
                    branchDst[3] == 0xff,
                "decodeRleEdgeRow preserves original branch push/pop traversal order");
    }

    {
        unsigned char read0Src[8] = {};
        unsigned char read0Dst[8] = {};
        unsigned char read0Base[8] = {};
        read0Src[0] = kRleBranchRoot;
        read0Src[1] = kRleBranchLeaf;
        read0Src[2] = 0xff;
        read0Src[3] = 0xff;
        read0Src[4] = 0xff;
        read0Base[kRleBranchRoot] = 0xff;
        read0Base[kRleBranchLeaf] = 0xff;
        decodeRleEdgeRow(read0Src, read0Dst, read0Base);
        require(read0Dst[0] == kRleBranchLeaf &&
                    read0Dst[1] == kRleBranchRoot &&
                    read0Dst[2] == 0xff,
                "decodeRleEdgeRow preserves original read0 push from a 0xff-base root");
    }

    {
        unsigned char overflowSrc[8] = {};
        unsigned char overflowDst[8] = {};
        unsigned char overflowBase[8] = {};
        std::memset(overflowBase, 0xff, sizeof(overflowBase));
        overflowSrc[0] = 0;
        // The DOS tree walker has a 256-entry parent/state stack. A malformed
        // two-node cycle exercises the C guard without depending on 0xff, which
        // is also the original child sentinel and cannot name node 255.
        overflowSrc[1] = 1;
        overflowSrc[2] = 0xff;
        overflowSrc[3] = 0;
        overflowSrc[4] = 0xff;
        decodeRleEdgeRow(overflowSrc, overflowDst, overflowBase);
        require(overflowDst[0] == 0xff,
                "decodeRleEdgeRow stops original over-deep RLE trees at the stack sentinel");
    }

    {
        unsigned char maskStream[4] = {};
        unsigned char *maskPtr = maskStream;
        writeLe16(maskStream, kVisibilityMaskLo);
        g_modelWideVtxFlag = 0;
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        require(testVisibilityMask(&maskPtr) == kVisibilityMaskLo &&
                    maskPtr == maskStream + 2,
                "testVisibilityMask reads the original narrow low-word visibility mask");

        maskPtr = maskStream;
        writeLe16(maskStream, 0);
        writeLe16(maskStream + 2, kVisibilityMaskHi);
        g_modelWideVtxFlag = 1;
        g_vtxSignMask.Lo = 0;
        g_vtxSignMask.Hi = kVisibilityMaskHi;
        require(testVisibilityMask(&maskPtr) == kVisibilityMaskHi &&
                    maskPtr == maskStream + 4,
                "testVisibilityMask reads the original wide low/high visibility masks");
    }

    {
        struct EdgeRec *rec = EREC(kPrimitiveEdgeIndex);
        unsigned char primitiveStream[6] = {};
        resetDrawStubs();
        std::memset(rec, 0, sizeof(*rec));
        rec->x1 = kPrimitiveX1;
        rec->y1 = kPrimitiveY1;
        rec->x2 = kPrimitiveX2;
        rec->y2 = kPrimitiveY2;
        primitiveStream[0] = kPrimitiveCommandCount;
        primitiveStream[1] = kPrimitiveLineOpcode;
        writeLe16(primitiveStream + 2, kVisibilityMaskLo);
        primitiveStream[4] = kPrimitiveEdgeIndex;
        primitiveStream[5] = kPrimitiveColorIndex;
        g_modelWideVtxFlag = 0;
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        g_objShade = 0;
        renderPrimitiveList(primitiveStream);
        require(g_gfxSetColorCalls == 1 &&
                    g_lastGfxColor == colorLut[kPrimitiveColorIndex] &&
                    g_drawClipLineCalls == 1 &&
                    g_lineX1 == kPrimitiveX1 &&
                    g_lineY1 == kPrimitiveY1 &&
                    g_lineX2 == kPrimitiveX2 &&
                    g_lineY2 == kPrimitiveY2,
                "renderPrimitiveList decodes the original visible line command and dispatches clipped drawing");
    }

    {
        unsigned char hiddenFaceStream[] = {
            static_cast<unsigned char>(kFilledFaceOpcode),
            static_cast<unsigned char>(kFilledFaceVertexCount),
            0, 1, 2,
            static_cast<unsigned char>(kPrimitiveColorIndex),
        };
        unsigned char *facePtr = hiddenFaceStream;
        resetDrawStubs();
        g_vtxSignMask.Lo = 0;
        renderPrimitiveCommand(&facePtr);
        require(facePtr == hiddenFaceStream + sizeof(hiddenFaceStream) &&
                    g_gfxSetColorCalls == 0,
                "renderPrimitiveCommand skips an original filled face whose visibility bit is clear");

        facePtr = hiddenFaceStream;
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        hiddenFaceStream[sizeof(hiddenFaceStream) - 1] = static_cast<unsigned char>(kPrimitiveHiddenColor);
        renderPrimitiveCommand(&facePtr);
        require(facePtr == hiddenFaceStream + sizeof(hiddenFaceStream) &&
                    g_gfxSetColorCalls == 0,
                "renderPrimitiveCommand skips original hidden-color filled faces");

        facePtr = hiddenFaceStream;
        hiddenFaceStream[sizeof(hiddenFaceStream) - 1] = static_cast<unsigned char>(kPrimitiveColorIndex + 1);
        g_objColorBase = kObjectColorBaseSkip;
        renderPrimitiveCommand(&facePtr);
        require(facePtr == hiddenFaceStream + sizeof(hiddenFaceStream) &&
                    g_gfxSetColorCalls == 0,
                "renderPrimitiveCommand preserves the original object-color-base face skip");
        g_objColorBase = 0;
    }

    {
        unsigned char highMaskFaceStream[] = {
            static_cast<unsigned char>(kPrimitiveHighWordFaceOpcode),
            1,
            0,
            static_cast<unsigned char>(kPrimitiveHiddenColor),
        };
        unsigned char *facePtr = highMaskFaceStream;
        resetDrawStubs();
        g_vtxSignMask.Lo = 0;
        g_vtxSignMask.Hi = kVisibilityMaskLo;
        renderPrimitiveCommand(&facePtr);
        require(facePtr == highMaskFaceStream + sizeof(highMaskFaceStream) &&
                    g_gfxSetColorCalls == 0,
                "renderPrimitiveCommand preserves original high-word face visibility mask");
    }

    {
        unsigned char faceStream[] = {
            static_cast<unsigned char>(kFilledFaceOpcode),
            static_cast<unsigned char>(kFilledFaceVertexCount),
            0, 1, 2,
            static_cast<unsigned char>(kPrimitiveColorIndex),
        };
        unsigned char *facePtr = faceStream;
        resetRasterState();
        resetDrawStubs();
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        for (int idx = 0; idx < kFilledFaceVertexCount; ++idx) {
            struct EdgeRec *rec = EREC(idx);
            std::memset(rec, 0, sizeof(*rec));
            rec->x1 = 10 + idx * 5;
            rec->y1 = 10;
            rec->x2 = 15 + idx * 5;
            rec->y2 = 20;
            rec->flags = kClipAcceptedFlag;
            rec->cv[0] = rec->x1;
            rec->cv[2] = rec->y1;
        }
        renderPrimitiveCommand(&facePtr);
        require(facePtr == faceStream + sizeof(faceStream) &&
                    g_gfxSetColorCalls == 1 &&
                    g_gfxDirtyRectCalls >= 1,
                "renderPrimitiveCommand fills an original visible clipped face and closes the extra clipped edge");
    }

    {
        struct EdgeRec *rec = EREC(kPrimitiveEdgeIndex);
        unsigned char invisibleLineStream[6] = {};
        unsigned char *linePtr = invisibleLineStream;
        resetDrawStubs();
        invisibleLineStream[0] = kPrimitiveLineOpcode;
        writeLe16(invisibleLineStream + 1, kVisibilityMaskLo);
        invisibleLineStream[3] = kPrimitiveEdgeIndex;
        invisibleLineStream[4] = kPrimitiveColorIndex;
        g_vtxSignMask.Lo = 0;
        renderPrimitiveCommand(&linePtr);
        require(linePtr == invisibleLineStream + 5 &&
                    g_drawClipLineCalls == 0,
                "renderPrimitiveCommand skips an original line command whose visibility mask is clear");

        linePtr = invisibleLineStream;
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        std::memset(rec, 0, sizeof(*rec));
        rec->flags = kRejectedEdgeFlag;
        renderPrimitiveCommand(&linePtr);
        require(linePtr == invisibleLineStream + 5 &&
                    g_drawClipLineCalls == 0,
                "renderPrimitiveCommand skips original rejected edge records");
    }

    {
        struct EdgeRec *rec = EREC(kPrimitiveEdgeIndex);
        unsigned char rleDisplayList[kRleDisplayListBytes] = {};
        unsigned char *tree = rleDisplayList + 1;
        unsigned char *coord = tree + 3; /* one edge uses two coord bytes plus the root/tree bytes. */
        unsigned char *cnts = coord + 2;
        unsigned char *dataBase = cnts + 1;
        rleDisplayList[0] = 0xff;
        tree[0] = kPrimitiveEdgeIndex;
        tree[1] = 0xff;
        tree[2] = 0xff;
        writeLe16(coord, 0);
        cnts[0] = 1;
        dataBase[0] = kPrimitiveLineOpcode;
        writeLe16(dataBase + 1, kVisibilityMaskLo);
        dataBase[3] = kPrimitiveEdgeIndex;
        dataBase[4] = kPrimitiveColorIndex;
        resetDrawStubs();
        std::memset(rec, 0, sizeof(*rec));
        rec->x1 = kPrimitiveX1;
        rec->y1 = kPrimitiveY1;
        rec->x2 = kPrimitiveX2;
        rec->y2 = kPrimitiveY2;
        g_modelEdgeCount = 1;
        g_modelWideVtxFlag = 0;
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        renderPrimitiveList(rleDisplayList);
        require(g_drawClipLineCalls == 1 &&
                    g_lastGfxColor == colorLut[kPrimitiveColorIndex],
                "renderPrimitiveList preserves original 0xff RLE display-list command ordering");
    }

    {
        struct EdgeRec *rec0 = EREC(0);
        struct EdgeRec *rec1 = EREC(1);
        unsigned char rleDisplayList[kRleDisplayListTwoEdgeBytes] = {};
        unsigned char *tree = rleDisplayList + 1;
        unsigned char *coord = tree + 5; /* two shared edges: four coord bytes plus root/tree bytes. */
        unsigned char *cnts = coord + 4;
        unsigned char *dataBase = cnts + 2;
        rleDisplayList[0] = 0xff;
        tree[0] = kRleBranchRoot;
        tree[1] = 0xff;
        tree[2] = kRleBranchLeaf;
        tree[3] = 0xff;
        tree[4] = 0xff;
        writeLe16(coord, 0);
        writeLe16(coord + 2, 5);
        cnts[0] = 1;
        cnts[1] = 1;
        dataBase[0] = kPrimitiveLineOpcode;
        writeLe16(dataBase + 1, kVisibilityMaskLo);
        dataBase[3] = 0;
        dataBase[4] = kPrimitiveColorIndex;
        dataBase[5] = kPrimitiveLineOpcode;
        writeLe16(dataBase + 6, kVisibilityMaskLo << 1);
        dataBase[8] = 1;
        dataBase[9] = kPrimitiveColorIndex + 1;
        resetDrawStubs();
        std::memset(rec0, 0, sizeof(*rec0));
        std::memset(rec1, 0, sizeof(*rec1));
        rec0->x1 = kPrimitiveX1;
        rec0->y1 = kPrimitiveY1;
        rec0->x2 = kPrimitiveX2;
        rec0->y2 = kPrimitiveY2;
        rec1->x1 = kPrimitiveX2;
        rec1->y1 = kPrimitiveY2;
        rec1->x2 = kPrimitiveX2 + 1;
        rec1->y2 = kPrimitiveY2 + 1;
        g_modelEdgeCount = 2;
        g_modelWideVtxFlag = 0;
        g_vtxSignMask.Lo = kVisibilityMaskLo | (kVisibilityMaskLo << 1);
        renderPrimitiveList(rleDisplayList);
        require(g_drawClipLineCalls == 2 &&
                    g_lastGfxColor == colorLut[kPrimitiveColorIndex],
                "renderPrimitiveList advances through multiple original RLE order entries");
    }

    {
        struct EdgeRec rec = {};
        resetRasterState();
        rec.x1 = 2;
        rec.y1 = kPrimitiveSpanMinY;
        rec.x2 = 7;
        rec.y2 = kPrimitiveSpanMaxY;
        drawPrimitiveEdges(&rec);
        require(g_dirtyRectMinY == kPrimitiveSpanMinY &&
                    g_dirtyRectMaxY == kPrimitiveSpanMaxY,
                "drawPrimitiveEdges rasterizes visible primitive edges into the original span buffers");
    }

    resetRasterState();
    resetDrawStubs();
    g_spanBuf.minX[kPrimitiveSpanMinY] = -5;
    g_spanBuf.maxX[kPrimitiveSpanMinY] = kClipMaxX + 5;
    g_dirtyRectMinY = kPrimitiveSpanMinY;
    g_dirtyRectMaxY = kPrimitiveSpanMinY;
    require(flushSpanDirtyRect() == 0 &&
                g_gfxDirtyRectCalls == 1 &&
                g_lastDirtyYMin == kPrimitiveSpanMinY &&
                g_lastDirtyYMax == kPrimitiveSpanMinY &&
                g_spanBuf.minX[kPrimitiveSpanMinY] == 0 &&
                g_spanBuf.maxX[kPrimitiveSpanMinY] == kClipMaxX,
            "flushSpanDirtyRect clamps original span bounds and emits the dirty rectangle");

    resetRasterState();
    resetDrawStubs();
    g_spanBuf.minX[kPrimitiveSpanMinY] = kClipMaxX + 5;
    g_spanBuf.maxX[kPrimitiveSpanMinY] = -5;
    g_dirtyRectMinY = kPrimitiveSpanMinY;
    g_dirtyRectMaxY = kPrimitiveSpanMinY;
    require(flushSpanDirtyRect() == 0 &&
                g_spanBuf.minX[kPrimitiveSpanMinY] == kClipMaxX &&
                g_spanBuf.maxX[kPrimitiveSpanMinY] == 0,
            "flushSpanDirtyRect preserves original high-min/negative-max clamp order");

    {
        struct EdgeRec rec = {};
        resetRasterState();
        rec.x1 = kClipOutsideLeftX;
        rec.x1h = -1;
        rec.y1 = kClipInsideY;
        rec.x2 = kClipInsideX;
        rec.y2 = kClipInsideY;
        clipLineSegment(&rec);
        require(!(rec.flags & kClipRejectedFlag) &&
                    rec.x1 >= 0 &&
                    rec.x1 <= kClipMaxX,
                "clipLineSegment clips an original P1-left crossing onto the viewport");

        rec = {};
        rec.x1 = kClipInsideX;
        rec.y1 = kClipInsideY;
        rec.x2 = kClipOutsideRightX;
        rec.y2 = kClipInsideY;
        clipLineSegment(&rec);
        require(!(rec.flags & kClipRejectedFlag) &&
                    rec.x2 >= 0 &&
                    rec.x2 <= kClipMaxX,
                "clipLineSegment clips an original P2-right crossing onto the viewport");

        rec = {};
        rec.x1 = kClipInsideX;
        rec.y1 = kClipOutsideTopY;
        rec.y1h = -1;
        rec.x2 = kClipOutsideRightX;
        rec.y2 = kClipOutsideBottomY;
        clipLineSegment(&rec);
        require(!(rec.flags & kClipRejectedFlag) &&
                    rec.x1 >= 0 &&
                    rec.x1 <= kClipMaxX &&
                    rec.x2 >= 0 &&
                    rec.x2 <= kClipMaxX &&
                    (rec.flags & kClipP2BoundaryFlag),
                "clipLineSegment drives the original both-outside subdivision path");

        rec = {};
        rec.x1 = kClipOutsideLeftX;
        rec.x1h = -1;
        rec.y1 = kClipOutsideTopY;
        rec.y1h = -1;
        rec.x2 = kClipOutsideRightX;
        rec.y2 = kClipOutsideTopY;
        rec.y2h = -1;
        clipLineSegment(&rec);
        require((rec.flags & kClipRejectedFlag) != 0,
                "clipLineSegment rejects original same-side invisible edges");
    }

    {
        int bx = kClipCrossLeftX;
        int cx = -1;
        int si = kClipCrossY1;
        int dx = 0;
        g_clipNeedsSubdiv = 0;
        g_clipMidxLo = kClipCrossRightX;
        g_clipMidxHi = 0;
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        require(clipMidpointSubdivide(&bx, &cx, &si, &dx) == 0 &&
                    bx == 0 &&
                    si == kClipCrossY1,
                "clipMidpointSubdivide preserves original 16-bit edge hit during midpoint search");

        bx = 10;
        cx = 0;
        si = 10;
        dx = 0;
        g_clipNeedsSubdiv = 0;
        g_clipMidxLo = 20;
        g_clipMidxHi = 0;
        g_clipMidyLo = 20;
        g_clipMidyHi = 0;
        require(clipMidpointSubdivide(&bx, &cx, &si, &dx) == 0 &&
                    bx == 10 &&
                    si == 10,
                "clipMidpointSubdivide preserves original 16-step interior convergence fallback");

        long midpointNegative = kClipMidpointNegative;
        long midpointPositive = kClipMidpointPositive;
        bx = static_cast<int16>(midpointNegative);
        cx = HI16(midpointNegative);
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 1;
        g_clipMidxLo = static_cast<int16>(midpointPositive);
        g_clipMidxHi = HI16(midpointPositive);
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        require(clipMidpointSubdivide(&bx, &cx, &si, &dx) != 0 &&
                    bx == 0 &&
                    cx == 0 &&
                    si == kClipCrossY1,
                "clipMidpointSubdivide preserves original 32-bit boundary acceptance");

        bx = kClipInteriorA;
        cx = 0;
        si = kClipInteriorA;
        dx = 0;
        g_clipNeedsSubdiv = 1;
        g_clipMidxLo = kClipInteriorB;
        g_clipMidxHi = 0;
        g_clipMidyLo = kClipInteriorB;
        g_clipMidyHi = 0;
        require(clipMidpointSubdivide(&bx, &cx, &si, &dx) == 0 &&
                    bx == kClipInteriorA &&
                    si == kClipInteriorA,
                "clipMidpointSubdivide preserves original 32-step interior convergence fallback");

        bx = kClipCrossRightX;
        cx = 0;
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 0;
        g_clipMidxLo = kClipCrossLeftX;
        g_clipMidxHi = -1;
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        g_clipOutcode1 = kClipRightBit;
        g_clipOutcode2 = kClipLeftBit;
        require(clipLineMidpoint(&bx, &cx, &si, &dx) == 0 &&
                    bx >= 0 &&
                    bx <= kClipMaxX,
                "clipLineMidpoint preserves original 16-bit crossing subdivision acceptance");

        bx = kClipCrossLeftX;
        cx = -1;
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 0;
        g_clipMidxLo = kClipCrossLeftX - 20;
        g_clipMidxHi = -1;
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        g_clipOutcode1 = kClipLeftBit;
        g_clipOutcode2 = 0;
        require((clipLineMidpoint(&bx, &cx, &si, &dx) & kClipLeftBit) != 0,
                "clipLineMidpoint preserves original 16-bit same-side exhaustion return");

        bx = kClipCrossLeftX;
        cx = -1;
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 0;
        g_clipMidxLo = kClipCrossLeftX - 20;
        g_clipMidxHi = -1;
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        g_clipOutcode1 = kClipLeftBit;
        g_clipOutcode2 = kClipLeftBit;
        require((clipLineMidpoint(&bx, &cx, &si, &dx) & kClipLeftBit) != 0,
                "clipLineMidpoint preserves original 16-bit shared-outcode rejection");

        bx = static_cast<int16>(midpointNegative);
        cx = HI16(midpointNegative);
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 1;
        g_clipMidxLo = static_cast<int16>(midpointPositive);
        g_clipMidxHi = HI16(midpointPositive);
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        g_clipOutcode1 = kClipLeftBit;
        g_clipOutcode2 = kClipRightBit;
        require(clipLineMidpoint(&bx, &cx, &si, &dx) == 0 &&
                    bx == 0 &&
                    si == kClipCrossY1,
                "clipLineMidpoint preserves original 32-bit crossing acceptance");

        bx = static_cast<int16>(midpointNegative);
        cx = HI16(midpointNegative);
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 1;
        g_clipMidxLo = static_cast<int16>(midpointNegative);
        g_clipMidxHi = HI16(midpointNegative);
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        g_clipOutcode1 = kClipLeftBit;
        g_clipOutcode2 = kClipLeftBit;
        require((clipLineMidpoint(&bx, &cx, &si, &dx) & kClipLeftBit) != 0,
                "clipLineMidpoint preserves original 32-bit shared-outcode rejection");

        bx = static_cast<int16>(midpointNegative);
        cx = HI16(midpointNegative);
        si = kClipCrossY1;
        dx = 0;
        g_clipNeedsSubdiv = 1;
        g_clipMidxLo = static_cast<int16>(midpointNegative);
        g_clipMidxHi = HI16(midpointNegative);
        g_clipMidyLo = kClipCrossY1;
        g_clipMidyHi = 0;
        g_clipOutcode1 = kClipLeftBit;
        g_clipOutcode2 = 0;
        require((clipLineMidpoint(&bx, &cx, &si, &dx) & kClipLeftBit) != 0,
                "clipLineMidpoint preserves original 32-bit asymmetric-outcode exhaustion");

        long hugePositive = kHugeClipPositive;
        bx = static_cast<int16>(hugePositive);
        cx = HI16(hugePositive);
        si = static_cast<int16>(hugePositive);
        dx = HI16(hugePositive);
        g_clipNeedsSubdiv = 1;
        g_clipMidxLo = static_cast<int16>(hugePositive);
        g_clipMidxHi = HI16(hugePositive);
        g_clipMidyLo = static_cast<int16>(hugePositive);
        g_clipMidyHi = HI16(hugePositive);
        g_clipOutcode1 = kClipRightBit | kClipAboveBit;
        g_clipOutcode2 = kClipRightBit | kClipAboveBit;
        require(clipLineMidpoint(&bx, &cx, &si, &dx) != 0,
                "clipLineMidpoint preserves original 32-bit subdivision reject for far outside points");

        struct EdgeRec subdivReject = {};
        g_clipP1xLo = static_cast<int16>(midpointNegative);
        g_clipP1xHi = HI16(midpointNegative);
        g_clipP1yLo = kClipCrossY1;
        g_clipP1yHi = 0;
        g_clipP2xLo = static_cast<int16>(midpointNegative);
        g_clipP2xHi = HI16(midpointNegative);
        g_clipP2yLo = kClipCrossY2;
        g_clipP2yHi = 0;
        g_clipNeedsSubdiv = 1;
        g_clipOutcode1 = kClipLeftBit;
        g_clipOutcode2 = kClipLeftBit;
        subdivReject.y1h = kClipCrossY1;
        subdivReject.y2h = kClipCrossY2;
        clipLineSubdivP1Outside(&subdivReject);
        require((subdivReject.flags & kClipRejectedFlag) != 0 &&
                    subdivReject.x1 == 0,
                "clipLineSubdivP1Outside preserves original shared-outcode rejection");
    }

    {
        resetRasterState();
        resetDrawStubs();
        g_lineX1 = kClipInsideX;
        g_lineY1 = kClipTopRejectY;
        g_lineX2 = kPrimitiveX2;
        g_lineY2 = kClipTopRejectY;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY == kDirtyUnset,
                "clipAndRasterizeEdge rejects original same-top invisible polygon edges without spans");

        resetRasterState();
        g_lineX1 = kClipLeftRejectX;
        g_lineY1 = kClipInsideY;
        g_lineX2 = kClipLeftRejectX - 10;
        g_lineY2 = kClipInsideY + 10;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY == kClipInsideY &&
                    g_spanBuf.minX[kClipInsideY] == 0,
                "clipAndRasterizeEdge fills the original left boundary column for same-left outside edges");

        resetRasterState();
        g_lineX1 = kClipOutsideRightX;
        g_lineY1 = kClipInsideY;
        g_lineX2 = kClipOutsideRightX + 10;
        g_lineY2 = kClipInsideY + 10;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY == kClipInsideY &&
                    g_spanBuf.maxX[kClipInsideY] == kClipMaxX,
                "clipAndRasterizeEdge fills the original right boundary column for same-right outside edges");

        resetRasterState();
        g_lineX1 = kClipCrossLeftX;
        g_lineY1 = kClipCrossY1;
        g_lineX2 = kPrimitiveX2;
        g_lineY2 = kClipCrossY2;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY >= 0 &&
                    g_dirtyRectMaxY <= kClipMaxY &&
                    g_lineX2 >= 0,
                "clipAndRasterizeEdge clips original P1-outside/P2-inside crossings before rasterizing");

        resetRasterState();
        g_lineX1 = kClipCrossLeftX;
        g_lineY1 = kClipCrossY1;
        g_lineX2 = kClipCrossRightX;
        g_lineY2 = kClipCrossY2;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY >= 0 &&
                    g_dirtyRectMaxY <= kClipMaxY &&
                    g_lineX1 >= 0 &&
                    g_lineX2 <= kClipMaxX,
                "clipAndRasterizeEdge clips both original outside endpoints in two passes");

        resetRasterState();
        g_lineX1 = kClipSliverX1;
        g_lineY1 = kClipSliverY1;
        g_lineX2 = kClipSliverX2;
        g_lineY2 = kClipSliverY2;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY == 0 &&
                    g_dirtyRectMaxY == kClipMaxY &&
                    g_spanBuf.minX[0] == 0 &&
                    g_spanBuf.minX[kClipMaxY] == 0,
                "clipAndRasterizeEdge preserves original sliver fallback boundary fill");

        resetRasterState();
        g_lineX1 = kPrimitiveX1;
        g_lineY1 = kClipOutsideTopY;
        g_lineX2 = kPrimitiveX2;
        g_lineY2 = kClipInsideY;
        clipAndRasterizeEdge();
        require(g_dirtyRectMinY >= 0 &&
                    g_lineY2 >= 0,
                "clipAndRasterizeEdge preserves the original top-edge horizontal clipping fallback");
    }

    {
        resetRasterState();
        resetDrawStubs();
        g_lineX1 = kPrimitiveX1;
        g_lineY1 = kClipInsideY;
        g_lineX2 = kClipOutsideRightX;
        g_lineY2 = kClipInsideY + 1;
        require(clipHorizonLineDraw() == 0 &&
                    g_gfxDrawLineCalls == 1 &&
                    g_lineX2 == kClipMaxX,
                "clipHorizonLineDraw clips original right-edge crossings");

        resetDrawStubs();
        g_lineX1 = kPrimitiveX1;
        g_lineY1 = kClipOutsideTopY;
        g_lineX2 = kPrimitiveX2;
        g_lineY2 = kClipInsideY;
        require(clipHorizonLineDraw() == 0 &&
                    g_gfxDrawLineCalls == 1 &&
                    g_lineY1 == 0,
                "clipHorizonLineDraw clips original top-edge crossings");

        resetDrawStubs();
        g_lineX1 = kPrimitiveX1;
        g_lineY1 = kClipInsideY;
        g_lineX2 = kPrimitiveX2;
        g_lineY2 = kClipOutsideBottomY2;
        require(clipHorizonLineDraw() == 0 &&
                    g_gfxDrawLineCalls == 1 &&
                    g_lineY2 == kClipMaxY,
                "clipHorizonLineDraw clips original bottom-edge crossings");
    }

    require(dirRound(0) == 0 &&
                dirRound(0x4000) == 1,
            "dirRound preserves the original doubled-high-word carry behavior");

    resetSceneState();
    require(transformAndCullObject(kSceneRelY, 0, 0) == 0 &&
                g_camTransYHi == kSceneDepthHi &&
                g_objDistance > 0,
            "transformAndCullObject accepts an in-frustum object and stores camera depth");
    require(transformAndCullObjectFar(0, kSceneRelY, 0) == 0,
            "transformAndCullObjectFar preserves the original relX,relY,relZ argument order");

    {
        unsigned char faceStream[1 + kFaceRecordBytes] = {};
        unsigned char *facePtr = faceStream;
        faceStream[0] = kFaceCountOne;
        writeLe16(faceStream + 7, kFaceThresholdForFlip);
        resetSceneState();
        rotatePoint3d(3, 2, 1, &facePtr);
        require(facePtr == faceStream + 1 + kFaceRecordBytes &&
                    g_modelEdgeCount == kFaceCountOne &&
                    g_vtxSignMask.Lo == static_cast<int16>(~kVisibilityMaskLo),
                "rotatePoint3d reads the original face table and clears flipped visibility bits");

        facePtr = faceStream;
        g_modelStreamPtr = reinterpret_cast<char *>(faceStream);
        rotatePoint3dFar();
        require(g_modelStreamPtr == reinterpret_cast<char *>(faceStream + 1 + kFaceRecordBytes),
                "rotatePoint3dFar advances the original model stream pointer");
    }

    resetSceneState();
    g_camTransYHi = kSceneDepthHi;
    emitModelVertex(0, 0, kSceneVertexCoord, 0);
    require(vtxScratch.vproj.in[0].div > 0 &&
                vtxScratch.vproj.x.v[0] == kViewCenterX &&
                vtxScratch.vproj.y.v[0] == kViewCenterY,
            "emitModelVertex transforms and projects the original inline vertex coordinates");

    {
        unsigned char vertexStream[1 + 2 + 6] = {};
        unsigned char *vertexPtr = vertexStream;
        resetSceneState();
        vertexStream[0] = kInlineVertexCount;
        writeLe16(vertexStream + 1, kVisibilityMaskLo);
        writeLe16(vertexStream + 3, 0);
        writeLe16(vertexStream + 5, kSceneVertexCoord);
        writeLe16(vertexStream + 7, 0);
        transformVertexList(&vertexPtr);
        require(vertexPtr == vertexStream + sizeof(vertexStream) &&
                    vtxScratch.vproj.in[0].div > 0,
                "transformVertexList decodes the original explicit inline vertex stream");
    }

    {
        unsigned char indexedStream[1 + 2 + 1] = {
            static_cast<unsigned char>(kIndexedVertexCountFlag),
            0, 0,
            0,
        };
        unsigned char *vertexPtr = indexedStream;
        resetSceneState();
        writeLe16(indexedStream + 1, kVisibilityMaskLo);
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        DW(0x004) = kSceneVertexCoord;
        DW(0x25c) = 0;
        DW(0x4b4) = static_cast<long>(kSceneDepthHi) << 16;
        transformVertexList(&vertexPtr);
        require(vertexPtr == indexedStream + sizeof(indexedStream) &&
                    vtxScratch.vproj.in[0].div > 0,
                "transformVertexList decodes original precomputed indexed vertices");

        vertexPtr = indexedStream;
        resetSceneState();
        setIdentityObjectMatrix();
        writeLe16(indexedStream + 1, kVisibilityMaskLo);
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        g_offscreenRender = 1;
        g_replayLog.vertexX[0] = kSceneVertexCoord;
        reinterpret_cast<int16 *>(g_modelVertY)[0] = kSceneVertexCoord;
        reinterpret_cast<int16 *>(g_modelVertZ)[0] = kSceneVertexCoord;
        buf3d3_1[0] = 0;
        buf3d3_2[0] = 0;
        buf3d3_3[0] = 0;
        transformVertexList(&vertexPtr);
        require(vertexPtr == indexedStream + sizeof(indexedStream) &&
                    vtxScratch.vproj.in[0].div > 0,
                "transformVertexList decodes original offscreen indexed vertices");
        g_offscreenRender = 0;
    }

    resetSceneState();
    size3d3_3 = 1;
    size3d3_4 = 1;
    size3d3_5 = 1;
    size3d3_6 = 1;
    g_replayLog.vertexX[0] = kSceneVertexCoord;
    reinterpret_cast<int16 *>(g_modelVertY)[0] = kSceneVertexCoord;
    reinterpret_cast<int16 *>(g_modelVertZ)[0] = kSceneVertexCoord;
    buf3d3_1[0] = 0;
    buf3d3_2[0] = 0;
    buf3d3_3[0] = 0;
    require(transformModelVerticesFar() == 0 &&
                DW(0x004) != 0 &&
                DW(0x25c) != 0 &&
                DW(0x4b4) != 0,
            "transformModelVerticesFar precomputes the original shared vertex coordinate sums");

    {
        unsigned char edgeStream[1 + 2 + 2] = {};
        unsigned char *edgePtr = edgeStream;
        struct EdgeRec *rec = EREC(0);
        resetSceneState();
        edgeStream[0] = 1;
        writeLe16(edgeStream + 1, kVisibilityMaskLo);
        edgeStream[3] = 0;
        edgeStream[4] = 1;
        vtxScratch.vproj.in[0].div = 0;
        vtxScratch.vproj.in[1].div = 1;
        vtxScratch.vproj.x.v[0] = -10;
        vtxScratch.vproj.y.v[0] = 10;
        vtxScratch.vproj.x.v[1] = 20;
        vtxScratch.vproj.y.v[1] = 20;
        projectModelEdges(&edgePtr);
        require(edgePtr == edgeStream + sizeof(edgeStream) &&
                    !(rec->flags & kClipRejectedFlag),
                "projectModelEdges clips the original behind-near-plane edge against the front vertex");

        edgePtr = edgeStream;
        g_modelStreamPtr = reinterpret_cast<char *>(edgeStream);
        projectModelEdgesFar();
        require(g_modelStreamPtr == reinterpret_cast<char *>(edgeStream + sizeof(edgeStream)),
                "projectModelEdgesFar advances the original edge stream pointer");
    }

    {
        unsigned char edgeStream[1 + 2 + 2] = {};
        unsigned char *edgePtr = edgeStream;
        struct EdgeRec *rec = EREC(0);
        resetSceneState();
        edgeStream[0] = 1;
        writeLe16(edgeStream + 1, kVisibilityMaskLo);
        edgeStream[3] = 0;
        edgeStream[4] = 1;
        g_vtxSignMask.Lo = 0;
        std::memset(rec, 0, sizeof(*rec));
        projectModelEdges(&edgePtr);
        require(edgePtr == edgeStream + sizeof(edgeStream) &&
                    rec->flags == 0,
                "projectModelEdges skips original invisible edge records without touching the destination");

        edgePtr = edgeStream;
        resetSceneState();
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        vtxScratch.vproj.in[0].div = 0;
        vtxScratch.vproj.in[1].div = 0;
        projectModelEdges(&edgePtr);
        require((rec->flags & kClipRejectedFlag) != 0,
                "projectModelEdges rejects original edges with both vertices behind the near plane");

        edgePtr = edgeStream;
        resetSceneState();
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        vtxScratch.vproj.in[0].div = 1;
        vtxScratch.vproj.in[1].div = 0;
        vtxScratch.vproj.x.v[0] = 20;
        vtxScratch.vproj.y.v[0] = 20;
        vtxScratch.vproj.x.v[1] = -10;
        vtxScratch.vproj.y.v[1] = 10;
        projectModelEdges(&edgePtr);
        require(!(rec->flags & kClipRejectedFlag),
                "projectModelEdges clips original edges whose second vertex is behind the near plane");

        edgePtr = edgeStream;
        resetSceneState();
        g_vtxSignMask.Lo = kVisibilityMaskLo;
        vtxScratch.vproj.in[0].div = 1;
        vtxScratch.vproj.in[1].div = 1;
        vtxScratch.vproj.x.v[0] = kPrimitiveX1;
        vtxScratch.vproj.y.v[0] = kPrimitiveY1;
        vtxScratch.vproj.x.v[1] = kPrimitiveX2;
        vtxScratch.vproj.y.v[1] = kPrimitiveY2;
        projectModelEdges(&edgePtr);
        require(rec->x1 == kPrimitiveX1 &&
                    rec->y1 == kPrimitiveY1 &&
                    rec->x2 == kPrimitiveX2 &&
                    rec->y2 == kPrimitiveY2,
                "projectModelEdges copies original already-front projected vertices directly");
    }

    {
        unsigned char pointStream[2] = {
            static_cast<unsigned char>(kPointOpcode),
            static_cast<unsigned char>(kPrimitiveColorIndex),
        };
        resetSceneState();
        g_modelStreamPtr = reinterpret_cast<char *>(pointStream);
        processSceneObject();
        require(g_drawClipLineCalls == 1 &&
                    g_lastGfxColor == colorLut[kPrimitiveColorIndex],
                "processSceneObject dispatches the original point opcode through sceneObjPoint");
    }

    {
        unsigned char edgeRunStream[4] = {
            static_cast<unsigned char>(kEdgeRunOpcode),
            0,
            static_cast<unsigned char>(kEdgeRunCount),
            0,
        };
        resetSceneState();
        DW(0x004) = 0;
        DW(0x25c) = 0;
        DW(0x4b4) = static_cast<long>(kSceneDepthHi) << 16;
        g_modelStreamPtr = reinterpret_cast<char *>(edgeRunStream);
        processSceneObject();
        require(g_drawClipLineCalls == 1,
                "processSceneObject dispatches the original edge-run opcode through sceneObjEdgeRun");
    }

    {
        unsigned char edgeRunStream[4] = {
            static_cast<unsigned char>(kEdgeRunOpcode),
            0,
            static_cast<unsigned char>(kEdgeRunCount),
            static_cast<unsigned char>(kEdgeRunOffscreenRef),
        };
        resetSceneState();
        setIdentityObjectMatrix();
        g_offscreenRender = 1;
        g_replayLog.vertexX[0] = kSceneVertexCoord;
        reinterpret_cast<int16 *>(g_modelVertY)[0] = kSceneVertexCoord;
        reinterpret_cast<int16 *>(g_modelVertZ)[0] = kSceneVertexCoord;
        buf3d3_1[0] = 0;
        buf3d3_2[0] = 0;
        buf3d3_3[0] = 0;
        g_modelStreamPtr = reinterpret_cast<char *>(edgeRunStream);
        processSceneObject();
        require(g_drawClipLineCalls == 1,
                "processSceneObject dispatches the original offscreen edge-run vertex transform path");
        g_offscreenRender = 0;
    }

    {
        unsigned char emptyObjectStream[] = {
            0, /* rotatePoint3d face count */
            0, /* transformVertexList empty vertex list */
            0, /* projectModelEdges empty edge list */
            0, /* renderPrimitiveList empty display list */
        };
        resetSceneState();
        g_dacSupported = 1;
        g_camTransYHi = kDacDepthHi;
        g_objTransform[1] = kSpinAngle;
        g_modelStreamPtr = reinterpret_cast<char *>(emptyObjectStream);
        processSceneObject();
        require(g_objShade == kDacShade &&
                    g_objHasRotation != 0,
                "processSceneObject preserves original DAC shade calculation and rotated-object matrix path");
        g_dacSupported = 0;
    }

    {
        unsigned char displayList[] = {
            static_cast<unsigned char>(kPrimitiveCommandCount),
            static_cast<unsigned char>(kPrimitiveLineOpcode),
            0, 0,
            static_cast<unsigned char>(kPrimitiveEdgeIndex),
            static_cast<unsigned char>(kPrimitiveColorIndex),
        };
        resetSceneState();
        writeLe16(displayList + 2, kVisibilityMaskLo);
        std::memset(EREC(kPrimitiveEdgeIndex), 0, sizeof(struct EdgeRec));
        EREC(kPrimitiveEdgeIndex)->x1 = kPrimitiveX1;
        EREC(kPrimitiveEdgeIndex)->y1 = kPrimitiveY1;
        EREC(kPrimitiveEdgeIndex)->x2 = kPrimitiveX2;
        EREC(kPrimitiveEdgeIndex)->y2 = kPrimitiveY2;
        g_modelStreamPtr = reinterpret_cast<char *>(displayList);
        require(drawModelDisplayList() == 0 &&
                    g_drawClipLineCalls == 1,
                "drawModelDisplayList dispatches the original display-list stream");
    }

    {
        unsigned char modelStream[] = {
            static_cast<unsigned char>(kRenderMode),
            static_cast<unsigned char>(kSortedOpcode),
        };
        unsigned char sortedPointStream[2] = {
            static_cast<unsigned char>(kPointOpcode),
            static_cast<unsigned char>(kPrimitiveColorIndex),
        };
        resetSceneState();
        projectSceneObject(reinterpret_cast<char *>(modelStream), 0, 0, 0, 0, kSceneRelY, 0);
        require(g_sortedObjCount == 1,
                "projectSceneObject inserts original deferred objects into the sorted list");
        g_sortRecs[g_sortList[0]].model = reinterpret_cast<char *>(sortedPointStream);
        renderSortedListFar();
        require(g_sortedObjCount == 0,
                "renderSortedListFar drains the original sorted-object queue");
    }

    {
        unsigned char modelA[] = {0};
        unsigned char modelB[] = {1};
        resetSceneState();
        g_sortedObjCount = 0;
        g_curLod = 2;
        g_objRenderMode = kRenderModeFive;
        g_camTransYLo = 0;
        g_camTransYHi = 1;
        insertSortedObject(modelA);
        g_camTransYHi = 2;
        insertSortedObject(modelB);
        require(g_sortedObjCount == 2 &&
                    g_sortRecs[g_sortList[0]].model == reinterpret_cast<char *>(modelB) &&
                    g_sortRecs[g_sortList[0]].depthHi == 0x20,
                "insertSortedObject preserves original farthest-first ordering and LOD/render-mode depth bias");

        resetSceneState();
        g_sortedObjCount = 1;
        g_sortList[0] = 0;
        g_sortRecs[0].depthHi = kSortTieDepthHi;
        g_sortRecs[0].depthLo = 0;
        g_curLod = 0;
        g_objRenderMode = kRenderMode;
        // insertSortedObject shifts camera depth right by eight at LOD 0.
        g_camTransYLo = 0;
        g_camTransYHi = (kSortTieDepthHi + 1) << 8;
        insertSortedObject(modelA);
        require(g_sortedObjCount == 2 &&
                    g_sortList[0] == 1,
                "insertSortedObject preserves original high-word farthest-first insertion");

        resetSceneState();
        g_sortedObjCount = 1;
        g_sortList[0] = 0;
        g_sortRecs[0].depthHi = kSortTieDepthHi;
        g_sortRecs[0].depthLo = kSortTieDepthLoExisting;
        g_curLod = 0;
        g_objRenderMode = kRenderMode;
        // Encode the target post-shift depth `(hi:lo)` in the original camera
        // words; LOD 0 applies an unsigned right shift by eight before sorting.
        g_camTransYHi = ((kSortTieDepthHi << 16) | kSortTieDepthLoNew) >> 8;
        g_camTransYLo = (((kSortTieDepthHi << 16) | kSortTieDepthLoNew) & 0xff) << 8;
        insertSortedObject(modelA);
        require(g_sortedObjCount == 2 &&
                    g_sortList[1] == 1 &&
                    g_sortRecs[g_sortList[1]].model == reinterpret_cast<char *>(modelA),
                "insertSortedObject preserves original equal-depth low-word tie placement");

        g_sortedObjCount = kSortedCapacity;
        for (int idx = 0; idx < kSortedCapacity; ++idx) {
            g_sortList[idx] = idx;
            g_sortRecs[idx].depthHi = idx;
            g_sortRecs[idx].depthLo = 0;
        }
        g_camTransYHi = kSortedCapacity + 1;
        insertSortedObject(modelA);
        require(g_sortedObjCount == kSortedCapacity &&
                    g_sortRecs[g_sortList[0]].model == reinterpret_cast<char *>(modelA),
                "insertSortedObject drops the oldest original sorted slot when the 35-entry queue is full");
    }

    {
        unsigned char immediateModel[] = {
            static_cast<unsigned char>(kRenderMode),
            0, 0, 0, 0,
        };
        resetSceneState();
        projectSceneObject(reinterpret_cast<char *>(immediateModel), 0, 0, 0, 0, kSceneRelY, 0);
        require(g_sortedObjCount == 0 &&
                    g_modelStreamPtr == reinterpret_cast<char *>(immediateModel + 1),
                "projectSceneObject preserves the original immediate-render path for coplanar non-sorted objects");

        unsigned char deferredModel[] = {
            static_cast<unsigned char>(kRenderMode),
            0,
        };
        resetSceneState();
        projectSceneObject(reinterpret_cast<char *>(deferredModel), 0, 0, 0, 0, kSceneRelY, 1);
        require(g_sortedObjCount == 1 &&
                    g_sortRecs[g_sortList[0]].model == reinterpret_cast<char *>(deferredModel + 1),
                "projectSceneObject preserves original non-coplanar deferred insertion");

        unsigned char transformModel[] = {
            static_cast<unsigned char>(kRenderMode),
            static_cast<unsigned char>(0x40 | kTransformOpcodeBase | kTransformOpcodeIndex),
            0,
        };
        resetSceneState();
        g_spinAngle = kSpinAngle;
        projectSceneObject(reinterpret_cast<char *>(transformModel), 0, 0, 0, 0, kSceneRelY, 1);
        require(g_objTransform[kTransformOpcodeIndex] == kSpinAngle &&
                    g_sortedObjCount == 1,
                "projectSceneObject preserves the original store-transform opcode before sorted insertion");
    }

    std::cout << "eg3drast_internal_behavior_tests passed\n";
    return 0;
}
