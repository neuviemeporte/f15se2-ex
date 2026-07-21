// 3D projection / tile-grid / map-geometry behavior tests (LINK_CORE + headless).
//
// Exercises the real deterministic geometry math against the linked core library:
// LOD coordinate scaling, shape-offset resolution, the 3DG tile-grid traversal,
// dynamic tile overrides, world<->tile index math, nearest-tile / waypoint
// selection, model vertex sign-mask + screen projection, aspect scaling, tac-map
// screen mapping, radar-scope + world->HUD projection, target range/bearing/loft,
// target-symbol category remap, and viewport setup. Golden values were
// hand-checked against the current source.
#include "egdata.h"
#include "egtypes.h"
#include "inttype.h"
#include "struct.h"
#include "comm.h"
#include "headless.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <tuple>

extern uint32 scaleCoordToLod(int16 level, uint32 coord);
extern int mapXToScreen(int mapX);
extern int mapYToScreen(int mapY);
extern int objectToScreen(int mapX, int mapY, int16 *outScreenX, int16 *outScreenY);
extern void projectWorldToHud(int16 worldX, int16 worldY, int16 worldZ);
extern int32 rotateVectorComponent(int16 axis, int16 vecX, int16 vecY, int16 vecZ);
extern int16 computeMapTargetRange(int16 targetIdx);
extern int16 computeSimObjectRange(int16 objIdx);
extern int16 computeTargetBearing(int16 targetX, int16 targetY, int16 wantBearing);
extern int16 findWaypointEntry(int16 mapX, int16 mapY);
extern int16 computeLoftAngle();
extern int16 getTargetSymbol(int16 wpIdx);
extern int16 isTargetOverWater(int16 wpIdx);
extern void setupViewport(const int16 *rect);
extern int16 process3dg(int16 lod, int16 col, int16 row);
extern void addTileEntry(struct TileObject *rec, int16 value, char tag);
extern int16 lookupTileEntry(int16 lod, int16 subIndex, int16 tileX, int16 tileY);
extern void worldToTileIndex(int16 worldX, int16 worldY, int16 *outCol, int16 *outRow);
extern void computeTileBounds(int16 *minTileX, int16 *maxTileX, int16 *minTileY, int16 *maxTileY);
extern void setViewPosition(int16 viewX, int16 viewY, int16 viewZ);
extern struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY);
extern int missileTargetCompat(int weaponType, int objIdx);
extern void projectMapPoint(int mapX, int mapY);
extern void hudMessage(const char *src);
extern void setTimedMessage(char *message);
extern void buildVertexSignMask(int16 screenX, int16 screenY);
extern void projectModelVertices(int16 screenX, int16 screenY);
extern int16 aspectScaleY(int16 screenY);
extern int16 readAxisInput(int16 axisIdx);
extern int shapeDataOffset(int shapeId);
extern int16 sine(int16 angle);
extern int16 cosine(int16 angle);

extern vtxSignMask_t g_vtxSignMask;
namespace {

// The scope's 5/6 vertical aspect (see projectMapPoint / scopeAspectY in egui.c):
// the projection pre-divides the vertical offset so the GL x1.2 present restores a
// square top-down scope. Rounds to whole 320-space pixels the same way scopeRound
// does (half away from zero).
constexpr float kScopeAspectY = 5.0f / 6.0f;
inline int scopeRoundExpect(float v) {
    return static_cast<int>(v < 0.0f ? v - 0.5f : v + 0.5f);
}

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum GeometryOriginalConstant : int {
    kAngleQuarterTurn = 0x4000,
    kAngleHalfTurn = 0x8000,
    kAngleThreeQuarterTurn = 0xC000,
    kBearingCurveBase = 0x2800,
    kBearingCurveCenter = 0x1333,
    kBearingCurveScale = 0x0B00,
    kRangeMax = 0x7FFF,
    kQ15ProductShift = 15,
    kQ15RoundBitShift = 14,
    kProcess3dgParentCell = 3,
    kProcess3dgLeafCell = 9,
    kTileOverrideValueOld = 0x1234,
    kTileOverrideValueNew = 0x4567,
    kTileOverrideShapeOld = 0x22,
    kTileOverrideShapeNew = 0x33,
    kCompatWeaponType = 4,
    kCompatTargetCategory = 7,
    kDynamicShapeFlag = 0x100,
    kShapeOffsetTableSlot = 50,
    kAircraftModelSlotOffset = 0x92,
    kDynamicShapeSlotOffset = 0xC3,
    kShapeOffsetTableEntries = 100,
    kWorld3dClearBytes = 0x100, /* model-header region the tile tests touch */
    kRadarProjectionCenterX = 160,
    kRadarProjectionCenterY = 152,
    kRadarClipRight = 195,
    kRadarClipTop = 107,
    kRadarBaseShift = 7,
    kReadAxisDisabledAccum = 77,
    kAspectInputY = 40,
    kAspectScaledY = 30,
    kVertexSignMaskCount = 2,
    kVertexNegativeNormal = -1,
    kVertexPositiveNormal = 1,
    kVertexSignMaskAfterNegativeFirstEdge = -2,
    kProjectVertexCount = 2,
    kProjectVertexX0 = 32,
    kProjectVertexY0 = 16,
    kProjectVertexX1 = -8,
    kProjectVertexY1 = -4,
    kPackedVertexCount = 1,
    kPackedVertexOpcode = 0x80 | kPackedVertexCount,
    kPackedVertexRef = 2,
    kPackedVertexXIndex = 3,
    kPackedVertexYIndex = 4,
    kPackedVertexX = 64,
    kPackedVertexY = -32,
    kProjectScreenX = 10,
    kProjectScreenY = 20,
    kProjectTileZoomShift = 1,
    kMapModelCenterX = 100,
    kMapModelCenterY = 80,
    kHudProjCenterX = 160,
    kHudProjFullHeightCenterY = 100,
    kHudProjWorldX = 1000,
    kHudProjWorldY = 2000,
    kHudProjViewZ = 320,
    kHudProjStoredDepth = -2,
    kHudProjOffscreenWorldX = 900,
    kHudCrashCamFlag = 0x80,
    kHudCamEyeDelta = 32,
    kHudProjNearPositiveZ = 640,
    kHudProjPostClipXWorld = 1009,
    kHudProjPostClipYWorld = 2010,
    kHudProjNarrowPageBottom = 56,
    kMapRangeTargetIdx = 6,
    kSimRangeObjIdx = 3,
    kTargetIdentifiedFlag = 0x80,
    kWaterTargetCategory = 12,
    kLandTargetCategory = 7,
    kWaterTargetSymbol = 0x22,
    kLandTargetSymbol = 0x33,
    kExplicitTargetSymbol = 0x155,
    kWaypointLookupMapX = 128,
    kWaypointLookupMapY = 32640,
    kWaypointLookupShape = 2,
    kWaypointDynamicNameBase = 0x100,
    kWaypointExistingIndex = 1,
    kWaypointSmokeSourceReset = -1,
    kTimedMessageFrames = 12,
    kNearestTileShape = 5,
    kNearestTileDynamicShape = 7,
    kNearestTileDynamicValue = 0x2345,
    kNearestTileTargetCategory = 1,
    kNearestTileModelOffset = 0,
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

int expectedFixedMulQ14(int a, int b) {
    const int32 p = static_cast<int32>(a) * static_cast<int32>(b);
    return sar32(p, kQ15ProductShift) + (sar32(p, kQ15RoundBitShift) & 1);
}

int expectedRangeApprox(int dx, int dy) {
    dx = std::abs(dx);
    dy = std::abs(dy);
    const long dist = dx > dy ? static_cast<long>(dy >> 1) + dx
                              : static_cast<long>(dx >> 1) + dy;
    return dist > kRangeMax ? kRangeMax : static_cast<int>(dist);
}

int expectedBearing(int deltaX, int deltaY) {
    if (deltaX == 0) return deltaY > 0 ? 0 : kAngleHalfTurn;
    if (deltaY == 0) return deltaX > 0 ? kAngleQuarterTurn : kAngleThreeQuarterTurn;

    const int absX = std::abs(deltaX);
    const int absY = std::abs(deltaY);
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

void setLe16(uint8 *p, int value) {
    p[0] = static_cast<uint8>(value & 0xff);
    p[1] = static_cast<uint8>((value >> 8) & 0xff);
}

void zeroTileGridBuffers() {
    for (int i = 0; i < 66; ++i) g_topLodGrid[i] = 0;
    for (int i = 0; i < 0x100; ++i) buf1_3dg[i] = 0;
    for (int i = 0; i < 0x200; ++i) {
        buf2_3dg[i] = 0;
        buf3_3dg[i] = 0;
        buf4_3dg[i] = 0;
    }
}

} // namespace

int main() {
    test_headless_init();

    // --- scaleCoordToLod rounded LOD shifts (eg3dproj) ----------------------
    for (auto [level, coord, expected] : {std::tuple{4, 0x10000u, (0x10000u + 0x20u) >> 6},
                                          std::tuple{3, 0x10000u, (0x10000u + 8u) >> 4},
                                          std::tuple{2, 0x10000u, (0x10000u + 2u) >> 2},
                                          std::tuple{1, 0x10000u, 0x10000u},
                                          std::tuple{0, 0x10000u, 0x20000u},
                                          std::tuple{-1, 0x1234u, 0x2468u}}) {
        require(scaleCoordToLod(level, coord) == expected,
                "scaleCoordToLod matches original rounded LOD shifts");
    }

    // --- shapeDataOffset aircraft vs dynamic model resolution (egmath) ------
    buf3d3[0] = 0;
    buf3d3[kShapeOffsetTableSlot] = kDynamicShapeSlotOffset;
    reinterpret_cast<int16 *>(flt15_buf1)[0] = 0;
    reinterpret_cast<int16 *>(flt15_buf1)[1] = kAircraftModelSlotOffset;
    require(shapeDataOffset(0) == AIRCRAFT_MODELS_OFFSET,
            "shapeDataOffset maps zero aircraft model slot to aircraft base");
    require(shapeDataOffset(1) == AIRCRAFT_MODELS_OFFSET + kAircraftModelSlotOffset,
            "shapeDataOffset adds aircraft model table offsets");
    require(shapeDataOffset(kDynamicShapeFlag) == 0,
            "shapeDataOffset maps dynamic shape zero through buf3d3");
    require(shapeDataOffset(kDynamicShapeFlag | kShapeOffsetTableSlot) == kDynamicShapeSlotOffset,
            "shapeDataOffset maps dynamic shape ids through buf3d3");

    // --- process3dg recursive tile-grid traversal (eg3dgrid) ----------------
    zeroTileGridBuffers();
    g_topLodGrid[(1 + 2) + ((-1 + 2) << 3)] = kProcess3dgParentCell;
    buf1_3dg[6 + (7 << 4)] = kProcess3dgParentCell;
    buf2_3dg[1 + ((2 << 2) + (kProcess3dgParentCell << 4))] = kProcess3dgParentCell;
    buf3_3dg[3 + ((1 << 2) + (kProcess3dgParentCell << 4))] = kProcess3dgParentCell;
    buf4_3dg[2 + ((3 << 2) + (kProcess3dgParentCell << 4))] = kProcess3dgLeafCell;
    require(process3dg(4, 1, -1) == kProcess3dgParentCell,
            "process3dg LOD 4 applies original +2 top-grid offset");
    require(process3dg(3, 6, 7) == kProcess3dgParentCell,
            "process3dg LOD 3 reads first 3DG child grid");
    require(process3dg(2, 25, 30) == kProcess3dgParentCell,
            "process3dg LOD 2 recurses through LOD 3 parent cell");
    require(process3dg(1, 103, 121) == kProcess3dgParentCell,
            "process3dg LOD 1 recurses through LOD 2 parent cell");
    require(process3dg(0, 414, 487) == kProcess3dgLeafCell,
            "process3dg LOD 0 recurses through LOD 1 parent cell");
    require(process3dg(4, -3, 0) == 0 && process3dg(0, 0x400, 0) == 0,
            "process3dg rejects original out-of-bounds coordinates");

    // --- addTileEntry / lookupTileEntry dynamic overrides (eg3dmap) ---------
    struct TileSceneObject sceneObject = {};
    struct TileObject tileObject = {};
    g_tileEntryCount = 0;
    g_tileEntryIdx = -1;
    tileObject.entry = &sceneObject;
    tileObject.lod = 2;
    tileObject.subIndex = 4;
    tileObject.tileX = 6;
    tileObject.tileY = 8;
    addTileEntry(&tileObject, kTileOverrideValueOld, static_cast<char>(kTileOverrideShapeOld));
    require(g_tileEntryCount == 1, "addTileEntry appends one dynamic tile override");
    require((sceneObject.shape & 0x80) != 0, "addTileEntry marks the source scene object as overridden");
    require(g_dynTileEntries[0].lod == tileObject.lod &&
                g_dynTileEntries[0].subIndex == tileObject.subIndex &&
                g_dynTileEntries[0].tileX == tileObject.tileX &&
                g_dynTileEntries[0].tileY == tileObject.tileY &&
                g_dynTileEntries[0].value == kTileOverrideValueOld &&
                g_dynTileEntries[0].shape == kTileOverrideShapeOld,
            "addTileEntry preserves original packed lod/sub/tile/value/shape bytes");
    tileObject.shapeOff = 0;
    tileObject.flag = 0;
    addTileEntry(&tileObject, kTileOverrideValueNew, static_cast<char>(kTileOverrideShapeNew));
    require(lookupTileEntry(2, 4, 6, 8) == kTileOverrideValueNew,
            "lookupTileEntry returns the newest matching override");
    require(g_tileEntryIdx == 1, "lookupTileEntry leaves index at the matching override");
    require(lookupTileEntry(2, 4, 6, 7) == 0,
            "lookupTileEntry returns zero for missing tile keys");

    // --- worldToTileIndex centered 4:3 conversion (eg3dmap) -----------------
    g_viewCenterX = 100;
    g_viewCenterY = 50;
    g_mapOriginX = 120;
    g_mapOriginY = 90;
    g_tileWorldSize = 16;
    int16 tileCol = 0;
    int16 tileRow = 0;
    worldToTileIndex(140, 80, &tileCol, &tileRow);
    require(tileCol == ((140 - 100 + 120) / 16) &&
                tileRow == (((80 - 50) * 4 / 3 + 90) / 16),
            "worldToTileIndex preserves original centered 4:3 tile conversion");

    // --- computeTileBounds clamps (eg3dmap) ---------------------------------
    g_clipMaxX = 319;
    g_clipMaxY = 199;
    g_tileGridDim = 20;
    g_viewCenterX = 200;
    g_viewCenterY = 160;
    g_mapOriginX = 0;
    g_mapOriginY = 0;
    g_tileWorldSize = 16;
    int16 minTileX = -1;
    int16 maxTileX = -1;
    int16 minTileY = -1;
    int16 maxTileY = -1;
    computeTileBounds(&minTileX, &maxTileX, &minTileY, &maxTileY);
    require(minTileX == 0 && minTileY == 0,
            "computeTileBounds clamps negative minimum tile bounds to zero");
    require(maxTileX == ((g_clipMaxX - g_viewCenterX + g_mapOriginX) / g_tileWorldSize) &&
                maxTileY == (((g_clipMaxY - g_viewCenterY) * 4 / 3 + g_mapOriginY) / g_tileWorldSize),
            "computeTileBounds computes unclamped maximum tile bounds with original formula");
    g_viewCenterX = -100;
    g_viewCenterY = -100;
    computeTileBounds(&minTileX, &maxTileX, &minTileY, &maxTileY);
    require(maxTileX == g_tileGridDim - 1 && maxTileY == g_tileGridDim - 1,
            "computeTileBounds clamps maximum tile bounds to grid dimension");

    // --- setViewPosition camera-position store (eg3dcam) --------------------
    setViewPosition(123, -456, 789);
    require(g_viewPosX == 123 && g_viewPosY == -456 && g_viewPosZ == 789,
            "setViewPosition stores original camera position globals");

    // --- readAxisInput disabled-input path (egmath) -------------------------
    struct GameComm axisComm = {};
    commData = &axisComm;
    g_inputDisabled = 1;
    g_axisInputAccum[0] = kReadAxisDisabledAccum;
    require(readAxisInput(0) == 0,
            "readAxisInput preserves original disabled-input zero return");
    g_inputDisabled = 0;

    // --- findNearestTileObject target-model selection (eg3dmap) -------------
    {
        struct TileSceneObject targetScene = {};
        zeroTileGridBuffers();
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
        std::memset(buf3d3, 0, sizeof(uint16) * kShapeOffsetTableEntries);
        std::memset(g_shapeTargetCategory, 0, sizeof(g_shapeTargetCategory));
        std::memset(g_world3dData, 0, kWorld3dClearBytes);
        g_tileEntryCount = 0;
        g_tileEntryIdx = -1;
        targetScene.shape = kNearestTileShape;
        matrix3dt[1][0] = 1;
        matrix3dt[2][0] = 1;
        matrix3dt_2[1][0] = &targetScene;
        matrix3dt_2[2][0] = &targetScene;
        g_shapeTargetCategory[kNearestTileShape] = kNearestTileTargetCategory;
        g_render3DTiles = 0;
        require(findNearestTileObject(0, 0) == nullptr,
                "findNearestTileObject skips original empty target models when tile rendering is disabled");

        g_world3dData[kNearestTileModelOffset + 2] = 1;
        require(findNearestTileObject(0, 0) == &nearestTile &&
                    nearestTile.id == kNearestTileShape,
                "findNearestTileObject accepts original target models with a nonzero third header byte");

        g_world3dData[kNearestTileModelOffset + 2] = 0;
        g_render3DTiles = 1;
        require(findNearestTileObject(0, 0) == &nearestTile &&
                    nearestTile.id == kNearestTileShape,
                "findNearestTileObject keeps empty target models when original 3D tile rendering is enabled");

        g_tileEntryCount = 0;
        g_tileEntryIdx = -1;
        targetScene.shape = 0x80 | kNearestTileShape;
        matrix3dt[2][0] = 0;
        for (int sample = 0; sample < 9; ++sample) {
            g_dynTileEntries[g_tileEntryCount].lod = 1;
            g_dynTileEntries[g_tileEntryCount].subIndex = 0;
            g_dynTileEntries[g_tileEntryCount].tileX =
                static_cast<uint8>(1 + g_neighborSampling.gridX[sample]);
            g_dynTileEntries[g_tileEntryCount].tileY =
                static_cast<uint8>(1 + g_neighborSampling.gridY[sample]);
            g_dynTileEntries[g_tileEntryCount].value = kNearestTileDynamicValue;
            g_dynTileEntries[g_tileEntryCount].shape = kNearestTileDynamicShape;
            ++g_tileEntryCount;
        }
        buf3d3[kNearestTileDynamicShape] = kNearestTileModelOffset;
        require(findNearestTileObject(0x1000, 0x1000) == &nearestTile &&
                    nearestTile.id == kNearestTileDynamicShape &&
                    g_tileEntryIdx >= 0,
                "findNearestTileObject applies the newest original dynamic tile override before choosing model data");
    }

    // --- aspectScaleY 3/4 vertical scale (eg3dmap) --------------------------
    require(aspectScaleY(kAspectInputY) == kAspectScaledY,
            "aspectScaleY preserves the original 3/4 vertical scale");

    // --- buildVertexSignMask edge-normal sign bits (eg3dmap) ----------------
    {
        uint8 edgeStream[20] = {};
        edgeStream[0] = kVertexSignMaskCount;
        setLe16(edgeStream + 5, kVertexNegativeNormal);
        setLe16(edgeStream + 13, kVertexPositiveNormal);
        g_modelStreamPtr = reinterpret_cast<char *>(edgeStream);
        buildVertexSignMask(0, 0);
        require(g_modelEdgeCount == kVertexSignMaskCount &&
                    g_modelWideVtxFlag == 0 &&
                    g_vtxSignMask.Lo == kVertexSignMaskAfterNegativeFirstEdge &&
                    g_vtxSignMask.Hi == -1,
                "buildVertexSignMask flips the original low sign-mask bit for negative edge normals");
    }

    // --- projectModelVertices inline + packed vertex expansion (eg3dmap) ----
    {
        uint8 vertexStream[24] = {};
        vertexStream[0] = kProjectVertexCount;
        setLe16(vertexStream + 3, kProjectVertexX0);
        setLe16(vertexStream + 5, kProjectVertexY0);
        setLe16(vertexStream + 11, kProjectVertexX1);
        setLe16(vertexStream + 13, kProjectVertexY1);
        g_modelStreamPtr = reinterpret_cast<char *>(vertexStream);
        g_modelWideVtxFlag = 0;
        g_tileZoomShift = kProjectTileZoomShift;
        g_viewCenterX = kMapModelCenterX;
        g_viewCenterY = kMapModelCenterY;
        projectModelVertices(kProjectScreenX, kProjectScreenY);
        require(g_modelVtxCount == kProjectVertexCount &&
                    vtxScratch.vproj.in[0].num == 1 &&
                    vtxScratch.vproj.in[0].div == 1 &&
                    vtxScratch.vproj.x.v[0] == ((kProjectVertexX0 >> kProjectTileZoomShift) + kProjectScreenX + kMapModelCenterX) &&
                    vtxScratch.vproj.y.v[0] == (-aspectScaleY((kProjectVertexY0 >> kProjectTileZoomShift) + kProjectScreenY) + kMapModelCenterY) &&
                    vtxScratch.vproj.x.v[1] == ((kProjectVertexX1 >> kProjectTileZoomShift) + kProjectScreenX + kMapModelCenterX),
                "projectModelVertices expands inline vertex coordinates into original screen-space vproj slots");

        uint8 packedVertexStream[8] = {};
        packedVertexStream[0] = kPackedVertexOpcode;
        packedVertexStream[3] = kPackedVertexRef;
        buf3d3_1[kPackedVertexRef] = kPackedVertexXIndex;
        buf3d3_2[kPackedVertexRef] = kPackedVertexYIndex;
        g_replayLog.vertexX[kPackedVertexXIndex] = kPackedVertexX;
        reinterpret_cast<int16 *>(g_modelVertY)[kPackedVertexYIndex] = kPackedVertexY;
        g_modelStreamPtr = reinterpret_cast<char *>(packedVertexStream);
        g_modelWideVtxFlag = 0;
        projectModelVertices(kProjectScreenX, kProjectScreenY);
        require(g_modelVtxCount == kPackedVertexCount &&
                    vtxScratch.vproj.x.v[0] == ((kPackedVertexX >> kProjectTileZoomShift) + kProjectScreenX + kMapModelCenterX) &&
                    vtxScratch.vproj.y.v[0] == (-aspectScaleY((kPackedVertexY >> kProjectTileZoomShift) + kProjectScreenY) + kMapModelCenterY),
                "projectModelVertices expands original packed vertex references through the optional coordinate tables");
    }

    // --- missileTargetCompat weapon/category table lookup (egtacmap) --------
    g_planeTable.planes[5].nameIndex = 0x80 | 2;
    g_shapeTargetCategory[2] = kCompatTargetCategory;
    require(missileTargetCompat(kCompatWeaponType, 5) ==
                static_cast<int>(static_cast<int8>(
                    g_targetCompatTable[kCompatWeaponType * 13 + kCompatTargetCategory])),
            "missileTargetCompat masks target name and indexes original weapon/category table");

    // --- mapXToScreen / mapYToScreen zoom mapping (egtacmap) ----------------
    g_mapCenterX = 0x2000;
    g_mapCenterY = 0x3000;
    g_mapZoomLevel = 8;
    require(mapXToScreen(0x2400) == (((0x2400 - 0x2000) >> 2) + 60),
            "mapXToScreen uses original center and zoom formula");
    require(mapYToScreen(0x3400) == ((((0x3400 - 0x3000) >> 2) * 3 >> 1 >> 1) + 140),
            "mapYToScreen uses original 3/4 vertical aspect formula");

    // --- projectMapPoint radar-scope projection + clip (egui) ---------------
    g_viewX_ = 0x2000;
    g_viewY_ = 0x3000;
    g_ourHead = 0;
    g_radarScopeRange = 1;
    projectMapPoint(0x2100, 0x2F00);
    {
        // projectMapPoint carries the fraction the original's >>shift dropped: it
        // scales the world delta in float, rotates by g_ourHead with the shared
        // sine table, applies the 5/6 vertical aspect, then rounds. Reproduce that
        // to track the intended projection rather than the old pure-integer form.
        const int shift = kRadarBaseShift - static_cast<char>(g_radarScopeRange);
        const float inv = 1.0f / static_cast<float>(1 << shift);
        const float c = static_cast<float>(cosine(g_ourHead)) / 32768.0f;
        const float s = static_cast<float>(sine(g_ourHead)) / 32768.0f;
        const float fsx = static_cast<float>(0x2100 - 0x2000) * inv;
        const float fsy = static_cast<float>(0x3000 - 0x2F00) * inv;
        const int expX = scopeRoundExpect(static_cast<float>(kRadarProjectionCenterX) + (c * fsx - s * fsy));
        const int expY = scopeRoundExpect(static_cast<float>(kRadarProjectionCenterY) - (c * fsy + s * fsx) * kScopeAspectY);
        require(vtxScratch.vproj.x.lo == expX &&
                    vtxScratch.vproj.y.lo == expY &&
                    g_projDepth == 0,
                "projectMapPoint projects to the float radar-center mapping (5/6 aspect) for visible points");
    }
    projectMapPoint(0x4000, 0x3000);
    require(g_projDepth == -1 && vtxScratch.vproj.x.lo > kRadarClipRight,
            "projectMapPoint marks points outside the original radar clip as hidden");
    projectMapPoint(0x2000, 0x1000);
    require(g_projDepth == -1 && vtxScratch.vproj.y.lo < kRadarClipTop,
            "projectMapPoint applies the original vertical radar clip bounds");

    // --- objectToScreen scope-clip acceptance (egtacmap) --------------------
    g_hudVisible = 1;
    g_scopeClipLeft = 10;
    g_scopeClipRight = 200;
    g_scopeClipTop = 20;
    g_scopeClipBottom = 180;
    int16 sx = 0;
    int16 sy = 0;
    require(objectToScreen(0x2000, 0x3000, &sx, &sy) == 1 && sx == 60 && sy == 140,
            "objectToScreen accepts points inside scope clip");
    require(objectToScreen(0, 0, &sx, &sy) == 0,
            "objectToScreen rejects points outside scope clip");
    g_hudVisible = 0;
    require(objectToScreen(0x2000, 0x3000, &sx, &sy) == 0,
            "objectToScreen rejects when HUD is hidden");

    // --- hudMessage / setTimedMessage HUD text + timers (egtacmap) ----------
    g_frameRateScaling = 4; // timer = frameRateScaling * 3
    hudMessage("status");
    require(std::strcmp(tempString, "status") == 0 &&
                g_hudMsgTimer == kTimedMessageFrames,
            "hudMessage copies HUD text and sets the original three-second timer");
    char timedMessage[] = "director";
    setTimedMessage(timedMessage);
    require(std::strcmp(string_3C04A, "director") == 0 &&
                g_dirMsgTimer == kTimedMessageFrames,
            "setTimedMessage copies director text and sets the original three-second timer");

    // --- rotateVectorComponent X,Z,Y column access (egtgt2) -----------------
    for (int i = 0; i < 9; ++i) {
        g_camRotMatrix[i] = 0;
    }
    g_camRotMatrix[0] = 0x4000;
    g_camRotMatrix[3] = -0x2000;
    g_camRotMatrix[6] = 0x3000;
    const long expectedRot =
        expectedFixedMulQ14(g_camRotMatrix[0], 3000) +
        expectedFixedMulQ14(g_camRotMatrix[3], 1000) +
        expectedFixedMulQ14(g_camRotMatrix[6], -2000);
    require(rotateVectorComponent(0, 3000, -2000, 1000) == expectedRot,
            "rotateVectorComponent preserves X,Z,Y column access");

    // --- projectWorldToHud perspective projection + clipping (egtgt2) -------
    // Permutation view matrix maps relX->camX, relY->camY, relZ->camDepth.
    for (int i = 0; i < 9; ++i) {
        g_camRotMatrix[i] = 0;
    }
    g_camRotMatrix[0] = kRangeMax;
    g_camRotMatrix[5] = kRangeMax;
    g_camRotMatrix[7] = kRangeMax;
    g_viewX_ = kHudProjWorldX;
    g_viewY_ = kHudProjWorldY;
    g_viewZ = kHudProjViewZ;
    g_viewMode = VIEW_COCKPIT;
    g_halfScaleRender = 0;
    g_pageFront[8] = 199;
    projectWorldToHud(kHudProjWorldX, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo == kHudProjCenterX &&
                vtxScratch.vproj.y.lo == kHudProjFullHeightCenterY &&
                g_projDepth == kHudProjStoredDepth,
            "projectWorldToHud projects centered points through the original full-height HUD center");
    g_viewMode = (ViewMode)kHudCrashCamFlag;
    g_ViewX = g_camEyeX + kHudCamEyeDelta;
    g_ViewY = g_camEyeY;
    g_camEyeZ = g_viewZ;
    projectWorldToHud(kHudProjWorldX, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo != -1,
            "projectWorldToHud preserves original crash-camera relative-eye adjustment path");
    g_viewMode = VIEW_COCKPIT;
    projectWorldToHud(kHudProjWorldX, kHudProjWorldY, kHudProjNearPositiveZ);
    require(vtxScratch.vproj.x.lo == -1,
            "projectWorldToHud rejects points behind the original camera depth plane");
    g_halfScaleRender = 1;
    projectWorldToHud(kHudProjPostClipXWorld, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo != -1,
            "projectWorldToHud preserves original half-scale projection path");
    g_halfScaleRender = 0;
    projectWorldToHud(kHudProjOffscreenWorldX, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo == -1,
            "projectWorldToHud rejects points outside the original depth-vs-X frustum");
    projectWorldToHud(kHudProjPostClipXWorld, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo == -1 &&
                g_offscreenProjX != -1,
            "projectWorldToHud preserves original post-projection X clipping");
    projectWorldToHud(kHudProjWorldX, kHudProjPostClipYWorld, 0);
    require(vtxScratch.vproj.x.lo == -1 &&
                g_offscreenProjX == kHudProjCenterX,
            "projectWorldToHud preserves original post-projection Y clipping");
    g_pageFront[8] = kHudProjNarrowPageBottom;
    projectWorldToHud(kHudProjWorldX, kHudProjWorldY, 0);
    require(vtxScratch.vproj.y.lo == kHudProjNarrowPageBottom,
            "projectWorldToHud uses the original narrow-HUD vertical center");
    g_pageFront[8] = 199;

    // --- computeTargetBearing / map / sim range (egtgt2) --------------------
    g_viewX_ = 5000;
    g_viewY_ = 7000;
    const int range = computeTargetBearing(4700, 7600, 1);
    require(range == expectedRangeApprox(300, -600),
            "computeTargetBearing stores original range approximation");
    require(g_targetRange == range, "computeTargetBearing writes g_targetRange");
    require(static_cast<uint16>(g_targetBearing) == static_cast<uint16>(expectedBearing(-300, -600)),
            "computeTargetBearing writes original bearing when requested");
    g_targetBearing = 1234;
    computeTargetBearing(4700, 7600, 0);
    require(g_targetBearing == 1234, "computeTargetBearing leaves bearing unchanged when not requested");

    g_planeTable.planes[kMapRangeTargetIdx].mapX = 4700;
    g_planeTable.planes[kMapRangeTargetIdx].mapY = 7600;
    require(computeMapTargetRange(kMapRangeTargetIdx) == expectedRangeApprox(300, -600) &&
                static_cast<uint16>(g_targetBearing) == static_cast<uint16>(expectedBearing(-300, -600)),
            "computeMapTargetRange reads map target coordinates and requests original bearing update");
    g_targetBearing = 4321;
    g_simObjects[kSimRangeObjIdx].posX = 4700;
    g_simObjects[kSimRangeObjIdx].posY = 7600;
    require(computeSimObjectRange(kSimRangeObjIdx) == expectedRangeApprox(300, -600) &&
                g_targetBearing == 4321,
            "computeSimObjectRange reads sim-object coordinates without updating bearing");

    // --- computeLoftAngle unsigned divide (egtgt2) --------------------------
    g_ourPitch = 0x1000;
    g_viewZ = 0x2000;
    require(computeLoftAngle() ==
                static_cast<int>((static_cast<unsigned long>((0x4000 - std::abs(g_ourPitch)) << 12) /
                                  static_cast<unsigned int>(g_viewZ + 0x1000)) -
                                 0x4000),
            "computeLoftAngle matches original unsigned divide formula");

    // --- isTargetOverWater / getTargetSymbol category remap (egtgt2) --------
    g_waterTargetId[0] = kWaterTargetSymbol;
    g_landTargetId[0] = kLandTargetSymbol;
    g_planeTable.planes[kMapRangeTargetIdx].flags = kTargetIdentifiedFlag;
    g_planeTable.planes[kMapRangeTargetIdx].nameIndex = 2;
    g_shapeTargetCategory[2] = kWaterTargetCategory;
    require(isTargetOverWater(kMapRangeTargetIdx) == 1 &&
                getTargetSymbol(kMapRangeTargetIdx) == kWaterTargetSymbol + 0x100,
            "isTargetOverWater and getTargetSymbol preserve original water-category symbol remap");
    g_shapeTargetCategory[2] = kLandTargetCategory;
    require(isTargetOverWater(kMapRangeTargetIdx) == 0 &&
                getTargetSymbol(kMapRangeTargetIdx) == kLandTargetSymbol + 0x100,
            "getTargetSymbol uses the original land symbol for identified non-water targets");
    g_planeTable.planes[kMapRangeTargetIdx].flags = 0;
    g_planeTable.planes[kMapRangeTargetIdx].nameIndex = kExplicitTargetSymbol;
    require(getTargetSymbol(kMapRangeTargetIdx) == kExplicitTargetSymbol,
            "getTargetSymbol returns the explicit target name index when the identified flag is clear");

    // --- findWaypointEntry dynamic-waypoint creation + reuse (egtgt2) -------
    {
        struct TileSceneObject waypointScene = {};
        zeroTileGridBuffers();
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
        std::memset(g_shapeTargetCategory, 0, sizeof(g_shapeTargetCategory));
        std::memset(g_planeTable.planes, 0, sizeof(g_planeTable.planes));
        std::memset(g_world3dData, 0, kWorld3dClearBytes);
        std::memset(buf3d3, 0, sizeof(uint16) * kShapeOffsetTableEntries);
        g_tileEntryCount = 0;
        g_tileEntryIdx = -1;
        g_world3dData[0] = 1;
        waypointScene.shape = kWaypointLookupShape;
        matrix3dt[1][0] = 1;
        matrix3dt[2][0] = 1;
        matrix3dt_2[1][0] = &waypointScene;
        matrix3dt_2[2][0] = &waypointScene;
        g_shapeTargetCategory[kWaypointLookupShape] = kWaterTargetCategory;
        g_render3DTiles = 1;
        g_planeCount = 2;
        g_smokeSourceIdx = 0;
        const int createdIdx = findWaypointEntry(kWaypointLookupMapX, kWaypointLookupMapY);
        require(createdIdx == 0 &&
                    g_planeTable.planes[0].nameIndex ==
                        nearestTile.id + kWaypointDynamicNameBase &&
                    g_smokeSourceIdx == kWaypointSmokeSourceReset,
                "findWaypointEntry creates the original dynamic waypoint in slot zero when no plane matches");
        g_planeTable.planes[kWaypointExistingIndex].mapX = g_planeTable.planes[0].mapX;
        g_planeTable.planes[kWaypointExistingIndex].mapY = g_planeTable.planes[0].mapY;
        require(findWaypointEntry(kWaypointLookupMapX, kWaypointLookupMapY) == kWaypointExistingIndex,
                "findWaypointEntry returns the original existing waypoint index when map coordinates match");
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(g_shapeTargetCategory, 0, sizeof(g_shapeTargetCategory));
        require(findWaypointEntry(kWaypointLookupMapX, kWaypointLookupMapY) == -1,
                "findWaypointEntry returns the original no-waypoint sentinel when no nearest tile object exists");
    }

    // --- setupViewport clip/center/overlay math (eg3dvp) --------------------
    // gfx_setBlitOffset/gfx_setOvlVal2 store into private gfx state (not readable
    // here), so only the clip/center/overlay globals are asserted.
    int16 rect[] = {0, 0, 0, 0, 0, 0, 0, 10, 109, 20, 219};
    g_halfScaleRender = 0;
    g_hudVisible = 0;
    setupViewport(rect);
    require(g_viewCenterX == 99 && g_viewCenterY == 49,
            "setupViewport centers non-top viewport from rectangle size");
    require(g_clipMaxX == 199 && g_clipMaxY == 99,
            "setupViewport updates clip bounds");
    require(g_overlayCenterX == g_overlayBaseX && g_overlayCenterY == g_overlayBaseY,
            "setupViewport starts overlay center pointers at base tables");
    rect[7] = 0;
    rect[8] = 199;
    g_halfScaleRender = 1;
    g_hudVisible = 1;
    setupViewport(rect);
    require(g_viewCenterY == 100, "setupViewport uses full-screen top viewport center");
    require(g_overlayCenterX == g_overlayBaseX + 8 && g_overlayCenterY == g_overlayBaseY + 24,
            "setupViewport applies half-scale and HUD overlay pointer offsets");

    std::cout << "geometry_behavior_tests passed\n";
    return 0;
}
