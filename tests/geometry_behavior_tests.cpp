#include "dos.h"
#include "egdata.h"
#include "egtypes.h"
#include "inttype.h"
#include "struct.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>

#include "posix_test_compat.h"

extern uint32 scaleCoordToLod(int level, uint32 coord);
extern void projectObjects(int heading, int rangeGate, long worldX, long worldY, long worldZ);
extern int mapXToScreen(int mapX);
extern int mapYToScreen(int mapY);
extern int objectToScreen(int mapX, int mapY, int16 *outScreenX, int16 *outScreenY);
extern void projectWorldToHud(int worldX, int worldY, int worldZ);
extern long rotateVectorComponent(int axis, int vecX, int vecY, int vecZ);
extern int computeMapTargetRange(int targetIdx);
extern int computeSimObjectRange(int objIdx);
extern int computeTargetBearing(int targetX, int targetY, int wantBearing);
extern int findWaypointEntry(int mapX, int mapY);
extern int computeLoftAngle();
extern int getTargetSymbol(int wpIdx);
extern int isTargetOverWater(int wpIdx);
extern void setupViewport(const int16 *rect);
extern int process3dg(int lod, int col, int row);
extern void addTileEntry(struct TileObject *rec, int value, char tag);
extern int lookupTileEntry(int lod, int subIndex, int tileX, int tileY);
extern void worldToTileIndex(int worldX, int worldY, int *outCol, int *outRow);
extern void computeTileBounds(int *minTileX, int *maxTileX, int *minTileY, int *maxTileY);
extern void setViewRotation(int rotX, int rotY, int rotZ);
extern void setViewPosition(int viewX, int viewY, int viewZ);
extern struct TileObject *findNearestTileObject(uint32 worldX, uint32 worldY);
extern void drawNearestTileObject(unsigned int coord1, unsigned int coord2, unsigned int coord3);
extern int missileTargetCompat(int weaponType, int objIdx);
extern void projectMapPoint(int mapX, int mapY);
extern void drawMapMarkerBox(int centerX, int centerY, int color);
extern void blitGaugeSprite(int srcCol, int srcRow, int destX, int destY);
extern void blitSprite(int destX, int destY, int srcX, int srcY, int spriteWidth, int spriteHeight, int transparent);
extern void cacheScopePanel(void);
extern void restoreScopePanel(void);
extern void captureScopePanel(void);
extern void clearStatusPanel(void);
extern void initTacMapView(void);
extern void redrawTacMap(int centerX, int centerY);
extern void zoomIn(void);
extern void zoomOut(void);
extern void setActivePanel(int panelId);
extern void refreshActivePanel(int panelId);
extern int plotMapObject(int mapX, int mapY, int color, int big);
extern int readMapPixelColor(int mapX, int mapY);
extern void drawMapRangeArc(int centerX, int centerY, int radius, int color,
                            int connectLines, int startAngle, int endAngle);
extern void drawFullscreenLine(int x1, int y1, int x2, int y2);
extern void drawViewportLine(int x1, int y1, int x2, int y2);
extern void drawClippedLineRegion(int x1, int y1, int x2, int y2,
                                  int clipLeft, int clipRight, int clipTop, int clipBottom,
                                  int drawBothPages);
extern void drawHudViewLine(int x1, int y1, int x2, int y2);
extern void setDrawColor(int color);
extern void fillRectBoth(int x1, int y1, int x2, int y2);
extern void drawColorPoint(int screenX, int screenY, int color);
extern void drawMapPoint(int x, int y, int color);
extern void switchIndicatorColor(int indicatorIdx, int color);
extern void drawPanelText(int panel, const char *text, int color);
extern void fillPanelBox(int panelId, int color);
extern void drawStringBothPages(const char *text, int screenX, int screenY, int color);
extern void drawStringActivePage(const char *text, int screenX, int screenY, int color);
extern void egDrawStringCentered(int16 *strStruct, const char *text, int screenX, int screenY, int color);
extern void drawNumber(int value, int x, int y, int color);
extern void tempStrcpy(const char *src);
extern void setTimedMessage(char *message);
extern void drawMapTileObject(char *modelData, int screenX, int screenY);
extern void drawMapTiles(int originX, int originY, int zoomShift);
extern void drawModelPoint(int x, int y);
extern void buildVertexSignMask(int screenX, int screenY);
extern void projectModelVertices(int screenX, int screenY);
extern int aspectScaleY(int screenY);
extern void renderMapTerrain(const int16 *transform, int mapX, int mapY, int zoomShift);
extern void setup3DTransform(const int16 *model, int angleX, int angleY, int angleZ,
                             int posX, int posY, int posZ, int renderScene);
extern void rasterize3DWorld(void);
extern void renderHudFrame(int unused);
extern void drawTacticalMap(char page);
extern void drawWorldObject(int shapeId, long worldX, long worldY, int altitude,
                            int objYaw, int objPitch, int objRoll, int scaleShift);
extern void drawTargetView(int shapeId, int worldX, int worldY, int altitude,
                           int objYaw, int objPitch, int objRoll, int mode, int shift);
extern int readAxisInput(int axisIdx);
extern int shapeDataOffset(int shapeId);
extern int16 *g_pageBack;
extern int16 *g_pageOffscreen;
extern int16 *g_pageFront;

int g_drawPage = 0;
int g_biosPixelX = 0;
int g_biosPixelY = 0;
int g_biosPixelPage = 0;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum GeometryOriginalConstant : int {
    kAngleQuarterTurn = 0x4000,
    kAngleHalfTurn = 0x8000,
    kAngleThreeQuarterTurn = 0xC000,
    kQ15ProductShift = 15,
    kQ15RoundBitShift = 14,
    kRangeMax = 0x7FFF,
    kBearingCurveBase = 0x2800,
    kBearingCurveCenter = 0x1333,
    kBearingCurveScale = 0x0B00,
    kProcess3dgParentCell = 3,
    kProcess3dgLeafCell = 9,
    kProcess3dgLod3BufferBytes = 0x100,
    kProcess3dgChildBufferBytes = 0x200,
    kTileOverrideValueOld = 0x1234,
    kTileOverrideValueNew = 0x4567,
    kTileOverrideShapeOld = 0x22,
    kTileOverrideShapeNew = 0x33,
    kCompatWeaponType = 4,
    kCompatTargetCategory = 7,
    kDynamicShapeFlag = 0x100,
    kShapeOffsetTableSlot = 0x7F,
    kAircraftModelSlotOffset = 0x92,
    kDynamicShapeSlotOffset = 0xC3,
    kRadarProjectionCenterX = 160,
    kRadarProjectionCenterY = 152,
    kRadarClipLeft = 124,
    kRadarClipRight = 195,
    kRadarClipTop = 107,
    kRadarClipBottom = 172,
    kRadarBaseShift = 7,
    kGaugeSpriteTileSize = 8,
    kGaugeSpriteSrcBiasX = 1,
    kGaugeSpriteSrcBiasY = 31,
    kGaugeSpriteCenterBias = 3,
    kGaugeSpriteSize = 7,
    kSpriteTransparentFlag = 1,
    kSpriteOpaqueFlag = 0x10,
    kScopePanelX = 24,
    kScopePanelY = 112,
    kScopePanelRight = 96,
    kScopePanelBottom = 168,
    kScopePanelWidth = 73,
    kScopePanelHeight = 57,
    kTacMapPanelLeft = 24,
    kTacMapPanelTop = 112,
    kTacMapPanelRight = 96,
    kTacMapPanelBottom = 168,
    kStatusPanelLeft = 120,
    kStatusPanelTop = 104,
    kStatusPanelRight = 199,
    kStatusPanelBottom = 175,
    kRightPanelLeft = 232,
    kRightPanelTop = 128,
    kRightPanelRight = 304,
    kRightPanelBottom = 184,
    kInitialTacMapZoom = 8,
    kTacMapZoomAfterInit = 9,
    kExternalCamStartDistance = 4,
    kExternalCamZoomedInDistance = 3,
    kExternalCamRestoredDistance = 4,
    kTrackCamPanel = 0x13,
    kTrackCamAheadHeading = 0,
    kTrackCamRearHeading = 0x8000,
    kTrackCamRightHeading = 0x4000,
    kTrackCamLeftHeading = 0xC000,
    kTacMapCenterX = 0x2000,
    kTacMapCenterY = 0x3000,
    kTacMapZoomStart = 8,
    kTacMapZoomMax = 9,
    kTacMapZoomMinSample = 3,
    kRadarRangeStart = 1,
    kRadarRangeZoomedIn = 2,
    kRadarRangeZoomedOut = 1,
    kPaletteLoadIndex = 0,
    kFadeTerrain = 19,
    kFadeDesert = 12,
    kRedrawTheaterDesert = 0,
    kRedrawTheaterNonDesert = 3,
    kFadeNonDesert = 16,
    kRedrawActivePlaneSpriteX = 0xA4,
    kRedrawGroundTargetSpriteX = 0xB0,
    kRedrawWaypointSpriteX = 0xA8,
    kRedrawMapMarkerSpriteY = 0,
    kRedrawMapMarkerSize = 4,
    kRedrawBackCacheWidth = 72,
    kRedrawBackCacheHeight = 56,
    kHudFrameWaypointIndex = 1,
    kHudFrameWaypointX = 0x2400,
    kHudFrameWaypointY = 0x2c00,
    kHudFrameViewX = 0x2000,
    kHudFrameViewY = 0x3000,
    kHudFrameViewportTop = 20,
    kHudFrameViewportBottom = 96,
    kHudFrameViewportLeft = 48,
    kHudFrameViewportRight = 271,
    kHudFrameDamageDacCount = 60,
    kHudFrameTrainingFlag = 0x1000,
    kHudFrameGunCrossFlag = 0x0200,
    kHudFrameCornerSpeed = 300,
    kHudFrameKnots = 240,
    kHudFrameAltitude = 1234,
    kHudFrameAutopilotAltitude = 5000,
    kHudFrameNegativeClimb = -64,
    kHudFrameGroundAlt = 100,
    kHudFrameFlightPathTrim = 0,
    kHudFrameAamWeaponClass = 7,
    kHudFrameMessageTimer = 2,
    kHudFrameDirectorTimer = 3,
    kDrawTacticalMapPage = 1,
    kRadarGroundObjCount = 3,
    kRadarPlaneCount = 5,
    kRadarObjVisibleFlag = 2,
    kRadarObjSpeed = 120,
    kRadarObjHeading = 0x3000,
    kRadarObjAltLow = -2000,
    kRadarObjAltLevel = 0,
    kRadarObjAltHigh = 2000,
    kRadarThreatLabelForObj1 = 0xfffe,
    kRadarThreatMarkerColor = 10,
    kRadarAirTargetMarkerColor = 7,
    kRadarProjectileTtl = 5,
    kRadarProjectileSamSpec = 0,
    kRadarProjectileMissileSpec = 1,
    kRadarProjectileClass3Spec = 2,
    kRadarProjectilePlayerSpec = 3,
    kRadarProjectileSamColor = 0x0c,
    kRadarProjectileMissileColor = 0x0e,
    kRadarProjectileClass3Color = 0x0d,
    kRadarProjectileEvenAltColor = 7,
    kRadarProjectilePlayerColor = 0x0f,
    kRadarProjectileClassSam = 0,
    kRadarProjectileClassMissile = 1,
    kRadarProjectileClassSpecial = 3,
    kRadarPlaneFlagDirectional = 0x201,
    kRadarPlaneFlagLanded = 8,
    kRadarPlaneFlagHidden = 0x80,
    kRadarPlaneActive = 1,
    kRadarPlaneTargetSlotCode = 6,
    kRadarEventTypeChaff = 1,
    kRadarEventTypeFlare = 2,
    kRadarEventTtl = 4,
    kRadarGaugeRowGroundLevel = 0,
    kRadarGaugeRowGroundLow = 1,
    kRadarGaugeRowGroundHigh = 2,
    kRadarGaugeRowPlane = 3,
    kRadarGaugeColOwnship = 0,
    kRadarGaugeColActivePlane = 1,
    kRadarGaugeColChaff = 2,
    kRadarGaugeColFlare = 3,
    kRadarGaugeColGroundLock = 7,
    kRadarGaugeColDirectionalBase = 8,
    kDrawWorldShape = 0,
    kDrawWorldAircraftOffset = 0x20,
    kDrawWorldRelX = 100,
    kDrawWorldRelY = -200,
    kDrawWorldViewX = 1000,
    kDrawWorldViewY = 0,
    kDrawWorldAltitude = 300,
    kDrawWorldViewZ = 100,
    kDrawWorldYaw = 0x1111,
    kDrawWorldPitch = 0x2222,
    kDrawWorldRoll = 0x3333,
    kDrawWorldScaleShift = 3,
    kDrawTargetShape = 0x105,
    kDrawTargetShapeOffset = 0x123,
    kDrawTargetViewX = 50,
    kDrawTargetViewY = 70,
    kDrawTargetWorldX = 60,
    kDrawTargetWorldY = 80,
    kDrawTargetAltitude = 140,
    kDrawTargetViewZ = 100,
    kDrawTargetYaw = 0x0100,
    kDrawTargetPitch = 0x0200,
    kDrawTargetRoll = 0x0300,
    kDrawTargetMode = 3,
    kDrawTargetShift = 0,
    kDrawTargetHead = 0x0400,
    kDrawTargetExternalPitch = 0x0500,
    kDrawTargetOurRoll = 0x0600,
    kDrawTargetTrackMode = 1,
    kDrawTargetRelativeMode = 0,
    kDrawTargetNegativeShift = -1,
    kDrawTargetHorizonTop = 128,
    kDrawTargetHorizonBottom = 184,
    kDrawTargetLabelX = 248,
    kDrawTargetLabelY = 176,
    kDrawTargetLabelColor = 0x0f,
    kDrawTargetRejectTrackRange = 10000,
    kDrawTargetScaleFloorRange = 1000,
    kDrawTargetScaleCapRange = 17,
    kDrawTargetPitchReject = 0x4000,
    kDrawTargetPitchRejectRelZ = 100,
    kDrawTargetCategoryLowOverride = 12,
    kReadAxisDisabledAccum = 77,
    kMapObjectColor = 6,
    kHiddenMapObjectColor = -1,
    kScreenPixelColor = 9,
    kRangeArcColor = 11,
    kRangeArcOverflowCenter = 0xC100,
    kDrawPointColor = 13,
    kIndicatorIdx = 0,
    kIndicatorOldColor = 3,
    kIndicatorNewColor = 5,
    kIndicatorX1 = 10,
    kIndicatorY1 = 20,
    kIndicatorX2 = 30,
    kIndicatorY2 = 40,
    kStringColor = 14,
    kNumberValue = 12345,
    kTimedMessageFrames = 12,
    kMapPointOpcode = 0x3f,
    kMapEdgeRunOpcode = 0x3e,
    kMapPolygonOpcode = 0x01,
    kMapEvenOddBit = 0x40,
    kMapPointColor = 0x0d,
    kAspectInputY = 40,
    kAspectScaledY = 30,
    kMapModelScreenX = 12,
    kMapModelScreenY = 16,
    kMapModelCenterX = 100,
    kMapModelCenterY = 80,
    kMapModelLineX = 112,
    kMapModelLineY = 64,
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
    kSetupSpinStart = 0x3000,
    kSetupFrameRate = 12,
    kSetupStepsThisFrame = 2,
    kSetupSpinAfter = 0x2800,
    kProjectionSphereColor = 7,
    kRenderMapZoomShift = 16,
    kHudProjCenterX = 160,
    kHudProjFullHeightCenterY = 100,
    kHudProjWorldX = 1000,
    kHudProjWorldY = 2000,
    kHudProjViewZ = 320,
    kHudProjDepth = -10,
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
    kProjectObjectsHeading = 0,
    kProjectObjectsNearRangeGate = 0x1000,
    kProjectObjectsWorldX = 0x12345,
    kProjectObjectsWorldY = 0x23456,
    kProjectObjectsWorldZ = 0x40,
    kProjectObjectsExpectedFinalLocalZ = 0x40,
    kProjectObjectsExpectedLodAfterTraversal = 0,
    kProjectObjectsHighDetailRangeGate = 0xF000,
    kProjectObjectsLowDetailRangeGate = 0x1000,
    kProjectObjectsTileShape = 5,
    kProjectObjectsDynamicShape = 0x21,
    kProjectObjectsDynamicOffset = 0x34,
    kProjectObjectsStaticOffset = 0x56,
    kProjectObjectsSceneX = 11,
    kProjectObjectsSceneY = -22,
    kProjectObjectsSceneZ = 33,
    kNearestTileTopGridCell = 4,
    kNearestTileTopGridIndex = 18,
    kNearestTileEntryCount = 2,
    kShapeOffsetTableEntries = 100,
    kNearestTileShape = 5,
    kNearestTileDynamicShape = 7,
    kNearestTileDynamicValue = 0x2345,
    kNearestTileTargetCategory = 1,
    kNearestTileModelOffset = 0,
    kNearestTileRotatingModelFlag = 0x40,
    kNearestTileEntryX = 10,
    kNearestTileEntryY = -20,
    kNearestTileEntryZ = 30,
    kNearestTileExpectedRelX = kNearestTileEntryX + 0x800,
    kNearestTileExpectedRelY = kNearestTileEntryY + 0x800,
};

int g_lastBlitOffset = 0;
int g_lastOverlayMaxX = 0;
int16 *g_lastRotationMatrix = nullptr;
int g_lastRotX = 0;
int g_lastRotY = 0;
int g_lastRotZ = 0;
int g_lastGfxColor = 0;
enum GeometryRecorderLimit : int {
    kMaxRecordedColors = 128,
    kMaxRecordedBlits = 256,
};
int g_colorCalls[kMaxRecordedColors] = {};
int g_colorCallCount = 0;
int g_clipLineCalls = 0;
int g_pageSetCalls = 0;
int g_lastPageSet = -1;
int g_displayPageResult = 0;
int g_blitClippedCalls = 0;
int g_blitOpaqueCalls = 0;
int16 *g_lastBlitDescriptor = nullptr;
struct BlitCall {
    int srcX;
    int srcY;
    int dstX;
    int dstY;
    int width;
    int height;
    int page;
};
BlitCall g_blitCalls[kMaxRecordedBlits] = {};
int g_blitCallCount = 0;
struct FillRectCall {
    const int16 *page;
    int left;
    int top;
    int right;
    int bottom;
};
FillRectCall g_fillCalls[64] = {};
int g_fillCallCount = 0;
struct SwitchColorCall {
    int16 *page;
    int x1;
    int y1;
    int x2;
    int y2;
    int oldColor;
    int newColor;
};
SwitchColorCall g_switchCalls[4] = {};
int g_switchCallCount = 0;
int g_centeredLabelCalls = 0;
int g_lastCenteredPanel = 0;
char g_lastCenteredText[80] = {};
int g_drawStringCalls = 0;
int16 *g_lastStringPage = nullptr;
char g_lastDrawnString[256] = {};
int g_advanceLodCalls = 0;
int g_projectEdgesCalls = 0;
int g_drawModelDisplayListCalls = 0;
int g_transformModelVerticesCalls = 0;
int g_projectionSphereCalls = 0;
int g_lastProjectionSphereColor = -1;
int g_renderSortedCalls = 0;
int g_setBlitOffset2Calls = 0;
int g_transformCullCalls = 0;
int g_transformCullResult = 0;
int g_projectSceneObjectCalls = 0;
int g_rotatePointCalls = 0;
int g_loadColorPaletteCalls = 0;
int g_lastPaletteIndex = -1;
int g_setFadeStepsCalls = 0;
int g_lastFadeSteps = -1;
int g_resetSimObjectLocksCalls = 0;
int g_dacAnimCountCalls = 0;
int g_lastDacAnimCount = -1;
char far *g_lastProjectModel = nullptr;
int g_lastProjectYaw = 0;
int g_lastProjectPitch = 0;
int g_lastProjectRoll = 0;
int g_lastProjectX = 0;
int g_lastProjectY = 0;
int g_lastProjectZ = 0;
struct CopyRectCall {
    int srcPage;
    int srcX;
    int srcY;
    int dstPage;
    int dstX;
    int dstY;
    int width;
    int height;
};
CopyRectCall g_copyCalls[64] = {};
int g_copyCallCount = 0;

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

bool sawDrawColor(int color) {
    for (int i = 0; i < g_colorCallCount && i < kMaxRecordedColors; ++i) {
        if (g_colorCalls[i] == color) return true;
    }
    return false;
}

bool sawGaugeSprite(int srcCol, int srcRow) {
    const int srcX = srcCol * kGaugeSpriteTileSize + kGaugeSpriteSrcBiasX;
    const int srcY = srcRow * kGaugeSpriteTileSize + kGaugeSpriteSrcBiasY;
    for (int i = 0; i < g_blitCallCount && i < kMaxRecordedBlits; ++i) {
        if (g_blitCalls[i].srcX == srcX && g_blitCalls[i].srcY == srcY &&
            g_blitCalls[i].width == kGaugeSpriteSize &&
            g_blitCalls[i].height == kGaugeSpriteSize) {
            return true;
        }
    }
    return false;
}

bool sawSpriteDescriptor(int srcX, int srcY, int width, int height) {
    for (int i = 0; i < g_blitCallCount && i < kMaxRecordedBlits; ++i) {
        if (g_blitCalls[i].srcX == srcX && g_blitCalls[i].srcY == srcY &&
            g_blitCalls[i].width == width && g_blitCalls[i].height == height) {
            return true;
        }
    }
    return false;
}

void setLodObjectCountForTest(int lod, int16 value) {
    const long pageSize = sysconf(_SC_PAGESIZE);
    auto *entry = const_cast<int16 *>(&g_lodObjectCount[lod]);
    const uintptr_t page = reinterpret_cast<uintptr_t>(entry) &
                           ~(static_cast<uintptr_t>(pageSize) - 1u);
    require(mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pageSize),
                     PROT_READ | PROT_WRITE) == 0,
            "test fixture can make LOD-count table writable");
    *entry = value;
    require(mprotect(reinterpret_cast<void *>(page), static_cast<size_t>(pageSize),
                     PROT_READ) == 0,
            "test fixture restores LOD-count table protection");
}

} // namespace

int FAR CDECL gfx_calcRowAddr(int col, int row) {
    return row * 320 + col;
}

void FAR CDECL gfx_setBlitOffset(int offset) {
    g_lastBlitOffset = offset;
}

void FAR CDECL gfx_setOvlVal2(int val) {
    g_lastOverlayMaxX = val;
}

void FAR CDECL gfx_setColor(int color) {
    g_lastGfxColor = color;
    if (g_colorCallCount < kMaxRecordedColors) {
        g_colorCalls[g_colorCallCount++] = color;
    }
}

void FAR CDECL gfx_nop23(void) {}

int FAR CDECL gfx_getDisplayPage(void) {
    return g_displayPageResult;
}

void FAR CDECL gfx_setPageN(uint16 pageNum) {
    g_lastPageSet = pageNum;
    ++g_pageSetCalls;
}

int drawClipLineGlobal(void) {
    ++g_clipLineCalls;
    return 0;
}

int far advanceModelPointerLod(void) {
    ++g_advanceLodCalls;
    return 0;
}

int far projectModelEdgesFar(void) {
    ++g_projectEdgesCalls;
    return 0;
}

int far drawModelDisplayList(void) {
    ++g_drawModelDisplayListCalls;
    return 0;
}

int far transformModelVerticesFar(void) {
    ++g_transformModelVerticesCalls;
    return 0;
}

void drawProjectionSphere(int skyColor) {
    ++g_projectionSphereCalls;
    g_lastProjectionSphereColor = skyColor;
}

int far renderSortedListFar(void) {
    ++g_renderSortedCalls;
    return 0;
}

void FAR CDECL gfx_setBlitOffset2(void) {
    ++g_setBlitOffset2Calls;
}

char far g_world3dData[0x100] = {};

void FAR CDECL gfx_blitSpriteClipped(int16 *ptr) {
    g_lastBlitDescriptor = ptr;
    if (g_blitCallCount < kMaxRecordedBlits) {
        const auto *sprite = reinterpret_cast<const struct SpriteParams *>(ptr);
        g_blitCalls[g_blitCallCount++] = {sprite->srcX, sprite->srcY, sprite->dstX,
                                          sprite->dstY, sprite->width, sprite->height,
                                          sprite->page};
    }
    ++g_blitClippedCalls;
}

void FAR CDECL gfx_blitSpriteOpaque(int16 *ptr) {
    g_lastBlitDescriptor = ptr;
    if (g_blitCallCount < kMaxRecordedBlits) {
        const auto *sprite = reinterpret_cast<const struct SpriteParams *>(ptr);
        g_blitCalls[g_blitCallCount++] = {sprite->srcX, sprite->srcY, sprite->dstX,
                                          sprite->dstY, sprite->width, sprite->height,
                                          sprite->page};
    }
    ++g_blitOpaqueCalls;
}

void FAR CDECL gfx_copyRect(int srcPage, uint16 srcX, uint16 srcY, int dstPage,
                            uint16 dstX, uint16 dstY, int width, int height) {
    require(g_copyCallCount < static_cast<int>(sizeof(g_copyCalls) / sizeof(g_copyCalls[0])),
            "test copy-call recorder capacity");
    g_copyCalls[g_copyCallCount++] = {srcPage, static_cast<int>(srcX), static_cast<int>(srcY),
                                      dstPage, static_cast<int>(dstX), static_cast<int>(dstY),
                                      width, height};
}

int __far fillSpanRect(const int16 *pageDesc, int left, int top, int right, int bottom) {
    require(g_fillCallCount < static_cast<int>(sizeof(g_fillCalls) / sizeof(g_fillCalls[0])),
            "test fill-call recorder capacity");
    g_fillCalls[g_fillCallCount++] = {pageDesc, left, top, right, bottom};
    return 0;
}

int __cdecl drawCenteredLabelBox(int panel, const char *text) {
    ++g_centeredLabelCalls;
    g_lastCenteredPanel = panel;
    std::strncpy(g_lastCenteredText, text, sizeof(g_lastCenteredText) - 1);
    g_lastCenteredText[sizeof(g_lastCenteredText) - 1] = '\0';
    return 0;
}

void FAR CDECL gfx_switchColor(int16 *pageDesc, int x1, int y1, int x2, int y2,
                               int oldColor, int newColor) {
    require(g_switchCallCount < 4, "test switch-color recorder capacity");
    g_switchCalls[g_switchCallCount++] = {pageDesc, x1, y1, x2, y2, oldColor, newColor};
}

void FAR CDECL gfx_drawString(int16 *pageNum, const char *string) {
    ++g_drawStringCalls;
    g_lastStringPage = pageNum;
    std::strncpy(g_lastDrawnString, string, sizeof(g_lastDrawnString) - 1);
    g_lastDrawnString[sizeof(g_lastDrawnString) - 1] = '\0';
}

int far buildRotationMatrixFar(int16 *matrix, int angleX, int angleY, int angleZ) {
    g_lastRotationMatrix = matrix;
    g_lastRotX = angleX;
    g_lastRotY = angleY;
    g_lastRotZ = angleZ;
    return 0;
}

int far transformAndCullObjectFar(int, int, int) {
    ++g_transformCullCalls;
    return g_transformCullResult;
}

void far projectSceneObject(char far *model, int yaw, int pitch, int roll, int x, int y, int z) {
    ++g_projectSceneObjectCalls;
    g_lastProjectModel = model;
    g_lastProjectYaw = yaw;
    g_lastProjectPitch = pitch;
    g_lastProjectRoll = roll;
    g_lastProjectX = x;
    g_lastProjectY = y;
    g_lastProjectZ = z;
}

int far rotatePoint3dFar(void) {
    ++g_rotatePointCalls;
    return 0;
}

struct GameComm *commData = nullptr;
struct Game *gameData = nullptr;
uint8 joyAxes[2] = {};

void loadColorPalette(int idx) {
    ++g_loadColorPaletteCalls;
    g_lastPaletteIndex = idx;
}

int misc_readJoystick(int16) { return 0; }

void FAR CDECL gfx_setFadeSteps(int steps) {
    ++g_setFadeStepsCalls;
    g_lastFadeSteps = steps;
}

void resetSimObjectLocks(void) {
    ++g_resetSimObjectLocksCalls;
}

void FAR CDECL gfx_setDacAnimCount(uint16 count) {
    ++g_dacAnimCountCalls;
    g_lastDacAnimCount = count;
}

int main() {
    for (auto [level, coord, expected] : {std::tuple{4, 0x10000u, (0x10000u + 0x20u) >> 6},
                                          std::tuple{3, 0x10000u, (0x10000u + 8u) >> 4},
                                          std::tuple{2, 0x10000u, (0x10000u + 2u) >> 2},
                                          std::tuple{1, 0x10000u, 0x10000u},
                                          std::tuple{0, 0x10000u, 0x20000u},
                                          std::tuple{-1, 0x1234u, 0x2468u}}) {
        require(scaleCoordToLod(level, coord) == expected,
                "scaleCoordToLod matches original rounded LOD shifts");
    }

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

    for (int i = 0; i < 66; ++i) g_topLodGrid[i] = 0;
    for (int i = 0; i < 0x100; ++i) buf1_3dg[i] = 0;
    for (int i = 0; i < 0x200; ++i) {
        buf2_3dg[i] = 0;
        buf3_3dg[i] = 0;
        buf4_3dg[i] = 0;
    }
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

    g_viewCenterX = 100;
    g_viewCenterY = 50;
    g_mapOriginX = 120;
    g_mapOriginY = 90;
    g_tileWorldSize = 16;
    int tileCol = 0;
    int tileRow = 0;
    worldToTileIndex(140, 80, &tileCol, &tileRow);
    require(tileCol == ((140 - 100 + 120) / 16) &&
                tileRow == (((80 - 50) * 4 / 3 + 90) / 16),
            "worldToTileIndex preserves original centered 4:3 tile conversion");

    g_clipMaxX = 319;
    g_clipMaxY = 199;
    g_tileGridDim = 20;
    g_viewCenterX = 200;
    g_viewCenterY = 160;
    g_mapOriginX = 0;
    g_mapOriginY = 0;
    g_tileWorldSize = 16;
    int minTileX = -1;
    int maxTileX = -1;
    int minTileY = -1;
    int maxTileY = -1;
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

    setViewRotation(100, -200, 300);
    require(g_lastRotationMatrix == g_viewRotMatrix &&
                g_lastRotX == -100 && g_lastRotY == 200 && g_lastRotZ == -300,
            "setViewRotation forwards original negated camera rotation angles");
    setViewPosition(123, -456, 789);
    require(g_viewPosX == 123 && g_viewPosY == -456 && g_viewPosZ == 789,
            "setViewPosition stores original camera position globals");

    std::memset(buf1_3dg, 0, kProcess3dgLod3BufferBytes);
    std::memset(buf2_3dg, 0, kProcess3dgChildBufferBytes);
    std::memset(buf3_3dg, 0, kProcess3dgChildBufferBytes);
    std::memset(buf4_3dg, 0, kProcess3dgChildBufferBytes);
    std::memset(matrix3dt, 0, sizeof(matrix3dt));
    g_detailLevel = 0;
    g_transformCullCalls = 0;
    g_projectSceneObjectCalls = 0;
    projectObjects(kProjectObjectsHeading, kProjectObjectsNearRangeGate,
                   kProjectObjectsWorldX, kProjectObjectsWorldY, kProjectObjectsWorldZ);
    require(g_proj3d.x == kProjectObjectsWorldX &&
                g_proj3d.y == kProjectObjectsWorldY &&
                g_proj3d.z == kProjectObjectsWorldZ &&
                g_curLod == kProjectObjectsExpectedLodAfterTraversal &&
                g_objLocalZ == kProjectObjectsExpectedFinalLocalZ &&
                g_transformCullCalls == 0 &&
                g_projectSceneObjectCalls == 0,
            "projectObjects stores the original world origin and traverses empty low-detail LOD grids without drawing objects");

    {
        const int16 originalLod3Count = g_lodObjectCount[3];
        setLodObjectCountForTest(3, 0);
        g_detailLevel = 0;
        g_projectSceneObjectCalls = 0;
        projectObjects(kProjectObjectsHeading, kProjectObjectsNearRangeGate,
                       kProjectObjectsWorldX, kProjectObjectsWorldY, kProjectObjectsWorldZ);
        setLodObjectCountForTest(3, originalLod3Count);
        require(g_projectSceneObjectCalls == 0 &&
                    g_curLod == kProjectObjectsExpectedLodAfterTraversal,
                "projectObjects preserves original skip of disabled LOD object-count levels");
    }

    std::memset(matrix3dt, 0, sizeof(matrix3dt));
    std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
    std::memset(buf3d3, 0, sizeof(uint16) * kShapeOffsetTableEntries);
    std::memset(g_world3dData, 0, sizeof(g_world3dData));
    g_detailLevel = 2;
    g_transformCullCalls = 0;
    g_transformCullResult = 1;
    g_projectSceneObjectCalls = 0;
    projectObjects(kProjectObjectsHeading, kProjectObjectsHighDetailRangeGate,
                   kProjectObjectsWorldX, kProjectObjectsWorldY, kProjectObjectsWorldZ);
    require(g_curLod == 0 &&
                g_transformCullCalls > 0 &&
                g_projectSceneObjectCalls == 0,
            "projectObjects preserves original high-detail LOD4 cull-reject traversal");

    struct TileSceneObject projectScene = {};
    projectScene.shape = 0x80 | kProjectObjectsTileShape;
    projectScene.x = kProjectObjectsSceneX;
    projectScene.y = kProjectObjectsSceneY;
    projectScene.z = kProjectObjectsSceneZ;
    matrix3dt[3][0] = 1;
    matrix3dt_2[3][0] = &projectScene;
    buf3d3[kProjectObjectsTileShape] = kProjectObjectsDynamicOffset;
    g_detailLevel = 0;
    g_transformCullResult = 0;
    g_projectSceneObjectCalls = 0;
    g_dynTileEntries[0].lod = 3;
    g_dynTileEntries[0].subIndex = 0;
    g_dynTileEntries[0].tileX = 0;
    g_dynTileEntries[0].tileY = 1;
    g_dynTileEntries[0].value = kProjectObjectsDynamicOffset;
    g_tileEntryCount = 1;
    projectObjects(kProjectObjectsHeading, kProjectObjectsLowDetailRangeGate, 0, 0, kProjectObjectsWorldZ);
    require(g_projectSceneObjectCalls > 0,
            "projectObjects projects at least one original near-range neighbor object");
    require(g_lastProjectModel == g_world3dData + kProjectObjectsDynamicOffset,
            "projectObjects preserves original near-range dynamic tile override model lookup");
    require(g_lastProjectX == kProjectObjectsSceneX &&
                g_lastProjectY == kProjectObjectsSceneY &&
                g_lastProjectZ == kProjectObjectsSceneZ,
            "projectObjects preserves original near-range projected tile-entry offsets");

    projectScene.shape = kProjectObjectsTileShape;
    buf3d3[kProjectObjectsTileShape] = kProjectObjectsStaticOffset;
    matrix3dt[4][0] = 1;
    matrix3dt_2[4][0] = &projectScene;
    g_detailLevel = 1;
    g_projectSceneObjectCalls = 0;
    projectObjects(kProjectObjectsHeading, kProjectObjectsHighDetailRangeGate, 0, 0, kProjectObjectsWorldZ);
    require(g_projectSceneObjectCalls > 0,
            "projectObjects projects original far directional LOD4 fallback objects");
    require(g_lastProjectModel == g_world3dData + kProjectObjectsStaticOffset,
            "projectObjects preserves original far directional LOD4 fallback static model lookup");

    reinterpret_cast<int16 *>(flt15_buf1)[kDrawWorldShape] = kDrawWorldAircraftOffset;
    g_ViewX = kDrawWorldViewX;
    g_ViewY = kDrawWorldViewY;
    g_viewZ = kDrawWorldViewZ;
    g_camEyeX = g_camEyeY = g_camEyeZ = 0;
    keyValue = 0;
    g_halfScaleRender = 0;
    g_drawPage = 0;
    g_projectSceneObjectCalls = 0;
    drawWorldObject(kDrawWorldShape,
                    static_cast<long>(kDrawWorldViewX + kDrawWorldRelX),
                    0x01000000L - kDrawWorldViewY + kDrawWorldRelY,
                    kDrawWorldAltitude,
                    kDrawWorldYaw,
                    kDrawWorldPitch,
                    kDrawWorldRoll,
                    kDrawWorldScaleShift);
    require(g_projectSceneObjectCalls == 1 &&
                g_lastProjectModel == g_aircraftModels + kDrawWorldAircraftOffset &&
                g_lastProjectYaw == -kDrawWorldYaw &&
                g_lastProjectPitch == kDrawWorldPitch &&
                g_lastProjectRoll == kDrawWorldRoll &&
                g_lastProjectX == kDrawWorldRelX &&
                g_lastProjectY == -kDrawWorldRelY &&
                g_lastProjectZ == 1 &&
                g_viewPosX == 0 &&
                g_viewPosY == 0 &&
                g_viewPosZ == -(kDrawWorldAltitude - kDrawWorldViewZ),
            "drawWorldObject applies original world-relative transform and projects the model");

    buf3d3[kDrawTargetShape & 0x7f] = kDrawTargetShapeOffset;
    g_drawPage = 1;
    g_viewX_ = kDrawTargetViewX;
    g_viewY_ = kDrawTargetViewY;
    g_viewZ = kDrawTargetViewZ;
    g_ourHead = kDrawTargetHead;
    g_extViewPitch = kDrawTargetExternalPitch;
    g_ourRoll = kDrawTargetOurRoll;
    g_world3dData[0x2f] = 4;
    g_shapeTargetCategory[kDrawTargetShape & 0x7f] = 0;
    g_projectSceneObjectCalls = 0;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetWorldX,
                   kDrawTargetWorldY,
                   kDrawTargetAltitude,
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetMode,
                   kDrawTargetShift);
    require(g_projectSceneObjectCalls == 1 &&
                g_lastProjectModel == g_world3dData + kDrawTargetShapeOffset &&
                g_lastProjectYaw == -kDrawTargetYaw &&
                g_lastProjectPitch == kDrawTargetPitch &&
                g_lastProjectRoll == kDrawTargetRoll &&
                g_lastProjectX == ((kDrawTargetWorldX - kDrawTargetViewX) << 4) &&
                g_lastProjectY == -((kDrawTargetWorldY - kDrawTargetViewY) << 4) &&
                g_lastProjectZ == ((kDrawTargetAltitude - kDrawTargetViewZ) >> 1) &&
                g_trkBearing == kDrawTargetHead &&
                g_trkPitch == kDrawTargetExternalPitch &&
                g_trkRoll == kDrawTargetOurRoll &&
                g_trkScale == 0x20 &&
                g_extraScaleShift == 0 &&
                g_offscreenRender == 0,
            "drawTargetView mode 3 preserves original external-view tracking transform and cleanup");

    g_projectSceneObjectCalls = 0;
    g_fillCallCount = 0;
    g_drawStringCalls = 0;
    g_drawPage = 0;
    g_world3dData[0x2f] = 4;
    g_shapeTargetCategory[kDrawTargetShape & 0x7f] = 0x10;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetWorldX,
                   kDrawTargetWorldY,
                   kDrawTargetAltitude,
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetTrackMode,
                   kDrawTargetNegativeShift);
    require(g_projectSceneObjectCalls == 1 &&
                *g_targetViewParams == 0 &&
                g_fillCallCount == 1 &&
                g_fillCalls[0].top == kDrawTargetHorizonTop &&
                g_fillCalls[0].bottom == kDrawTargetHorizonBottom &&
                g_drawStringCalls == 1 &&
                std::strncmp(g_lastDrawnString, "BRG ", 4) == 0 &&
                g_lastStringPage == g_pageFront,
            "drawTargetView mode 1 preserves original target-view horizon fill and bearing label");

    g_projectSceneObjectCalls = 0;
    g_fillCallCount = 0;
    g_drawStringCalls = 0;
    g_shapeTargetCategory[kDrawTargetShape & 0x7f] = kDrawTargetCategoryLowOverride;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetWorldX,
                   kDrawTargetWorldY,
                   kDrawTargetAltitude,
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetTrackMode,
                   kDrawTargetShift);
    require(g_projectSceneObjectCalls == 1 &&
                g_fillCallCount > 0 &&
                g_targetViewParams[2] == colorLut[1],
            "drawTargetView mode 1 preserves original target-category low-nibble color override");

    g_projectSceneObjectCalls = 0;
    g_fillCallCount = 0;
    g_drawStringCalls = 0;
    g_trkRange = 1;
    g_trkSize = 400;
    g_trkBearing = 0;
    g_trkPitch = 0;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetViewX,
                   kDrawTargetViewY - kDrawTargetScaleFloorRange,
                   kDrawTargetViewZ,
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetRelativeMode,
                   kDrawTargetShift);
    require(g_projectSceneObjectCalls == 1 &&
                g_trkScale == 4,
            "drawTargetView mode 0 preserves original tracking-scale floor and recompute path");

    g_projectSceneObjectCalls = 0;
    g_fillCallCount = 0;
    g_drawStringCalls = 0;
    g_trkRange = kDrawTargetScaleCapRange;
    g_trkSize = 400;
    g_trkBearing = 0;
    g_trkPitch = 0;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetViewX,
                   kDrawTargetViewY - 1,
                   kDrawTargetViewZ,
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetRelativeMode,
                   kDrawTargetShift);
    require(g_projectSceneObjectCalls == 1 &&
                g_trkScale == 0x100,
            "drawTargetView mode 0 preserves original tracking-scale cap");

    g_projectSceneObjectCalls = 0;
    g_fillCallCount = 0;
    g_drawStringCalls = 0;
    g_trkRange = kDrawTargetRejectTrackRange;
    g_trkSize = 400;
    g_trkBearing = 0;
    g_trkPitch = 0;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetViewX,
                   kDrawTargetViewY - 1,
                   kDrawTargetViewZ + (kDrawTargetPitchRejectRelZ << 5),
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetRelativeMode,
                   kDrawTargetShift);
    require(g_projectSceneObjectCalls == 0 &&
                g_fillCallCount == 0 &&
                g_drawStringCalls == 0,
            "drawTargetView mode 0 preserves original large pitch-delta early return");

    g_projectSceneObjectCalls = 0;
    g_fillCallCount = 0;
    g_drawStringCalls = 0;
    g_trkRange = kDrawTargetRejectTrackRange;
    g_trkSize = 400;
    g_trkBearing = kAngleHalfTurn;
    g_trkPitch = 0;
    drawTargetView(kDrawTargetShape,
                   kDrawTargetWorldX,
                   kDrawTargetWorldY,
                   kDrawTargetAltitude + (kDrawTargetPitchReject << 5),
                   kDrawTargetYaw,
                   kDrawTargetPitch,
                   kDrawTargetRoll,
                   kDrawTargetRelativeMode,
                   kDrawTargetShift);
    require(g_projectSceneObjectCalls == 0 &&
                g_fillCallCount == 0 &&
                g_drawStringCalls == 0,
            "drawTargetView mode 0 preserves original large bearing-delta early return");

    struct GameComm axisComm = {};
    commData = &axisComm;
    g_inputDisabled = 1;
    g_axisInputAccum[0] = kReadAxisDisabledAccum;
    require(readAxisInput(0) == 0,
            "readAxisInput preserves original disabled-input zero return");
    g_inputDisabled = 0;

    {
        struct TileSceneObject nearestScene[2] = {};
        std::memset(g_topLodGrid, 0, sizeof(int16) * 66);
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
        std::memset(buf3d3, 0, sizeof(uint16) * kShapeOffsetTableEntries);
        std::memset(g_world3dData, 0, sizeof(g_world3dData));
        g_topLodGrid[kNearestTileTopGridIndex] = kNearestTileTopGridCell;
        matrix3dt[4][kNearestTileTopGridCell] = kNearestTileEntryCount;
        matrix3dt_2[4][kNearestTileTopGridCell] = nearestScene;
        nearestScene[0].shape = kNearestTileShape;
        nearestScene[0].x = kNearestTileEntryX;
        nearestScene[0].y = kNearestTileEntryY;
        nearestScene[0].z = kNearestTileEntryZ;
        buf3d3[kNearestTileShape] = kNearestTileModelOffset;
        g_world3dData[kNearestTileModelOffset + 1] = kNearestTileRotatingModelFlag;
        g_rotatePointCalls = 0;
        g_advanceLodCalls = 0;
        drawNearestTileObject(0, 0, 0);
        require(nearestTile.entry == nearestScene &&
                    nearestTile.dist == std::abs(kNearestTileExpectedRelX) + std::abs(kNearestTileExpectedRelY) &&
                    g_objRelX == kNearestTileExpectedRelX &&
                    g_objRelY == kNearestTileExpectedRelY &&
                    g_objTransform[0] == kNearestTileEntryZ &&
                    g_advanceLodCalls == 1 &&
                    g_rotatePointCalls == 1 &&
                    g_objHasRotation == 0,
                "drawNearestTileObject selects the closest LOD-4 tile object and prepares the original rotating-object transform");
    }

    {
        struct TileSceneObject targetScene = {};
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
        std::memset(buf3d3, 0, sizeof(uint16) * kShapeOffsetTableEntries);
        std::memset(g_shapeTargetCategory, 0, sizeof(g_shapeTargetCategory));
        std::memset(g_world3dData, 0, sizeof(g_world3dData));
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

    require(aspectScaleY(kAspectInputY) == kAspectScaledY,
            "aspectScaleY preserves the original 3/4 vertical scale");

    {
        uint8 pointModel[4] = {};
        pointModel[1] = kMapPointOpcode;
        pointModel[2] = kMapPointColor;
        g_viewCenterX = kMapModelCenterX;
        g_viewCenterY = kMapModelCenterY;
        g_clipLineCalls = 0;
        g_advanceLodCalls = 0;
        drawMapTileObject(reinterpret_cast<char *>(pointModel), kMapModelScreenX, kMapModelScreenY);
        require(g_advanceLodCalls == 1 &&
                    g_lastGfxColor == kMapPointColor &&
                    g_clipLineCalls == 1 &&
                    g_lineX1 == kMapModelLineX &&
                    g_lineY1 == kMapModelLineY &&
                    g_lineX2 == kMapModelLineX &&
                    g_lineY2 == kMapModelLineY,
                "drawMapTileObject dispatches point opcodes through drawModelPoint with centered screen coords");
        pointModel[1] = kMapEdgeRunOpcode;
        g_clipLineCalls = 0;
        drawMapTileObject(reinterpret_cast<char *>(pointModel), kMapModelScreenX, kMapModelScreenY);
        require(g_clipLineCalls == 0,
                "drawMapTileObject skips original edge-run marker opcodes on the tactical map");
        g_modelStreamPtr = reinterpret_cast<char *>(pointModel + 1);
        drawModelPoint(kMapModelScreenX, kMapModelScreenY);
        require(g_lastGfxColor == kMapPointColor,
                "drawModelPoint consumes the color byte after the point opcode");

        uint8 culledModel[4] = {};
        culledModel[1] = 0;
        g_curLod = 3;
        g_modelEvenOddBit = kMapEvenOddBit;
        g_projectEdgesCalls = 0;
        g_drawModelDisplayListCalls = 0;
        drawMapTileObject(reinterpret_cast<char *>(culledModel), kMapModelScreenX, kMapModelScreenY);
        require(g_projectEdgesCalls == 0 && g_drawModelDisplayListCalls == 0,
                "drawMapTileObject preserves original high-LOD even/odd culling before model projection");

        uint8 polygonModel[16] = {};
        polygonModel[1] = kMapPolygonOpcode;
        polygonModel[10] = 0;
        g_curLod = 2;
        g_projectEdgesCalls = 0;
        g_drawModelDisplayListCalls = 0;
        drawMapTileObject(reinterpret_cast<char *>(polygonModel), kMapModelScreenX, kMapModelScreenY);
        require(g_projectEdgesCalls == 1 && g_drawModelDisplayListCalls == 1,
                "drawMapTileObject projects and draws original non-marker tactical map models");
    }

    {
        uint8 edgeStream[20] = {};
        edgeStream[0] = kVertexSignMaskCount;
        setLe16(edgeStream + 5, kVertexNegativeNormal);
        setLe16(edgeStream + 13, kVertexPositiveNormal);
        g_modelStreamPtr = reinterpret_cast<char *>(edgeStream);
        buildVertexSignMask(0, 0);
        require(g_modelEdgeCount == kVertexSignMaskCount &&
                    g_modelWideVtxFlag == 0 &&
                    g_vtxSignMaskLo == kVertexSignMaskAfterNegativeFirstEdge &&
                    g_vtxSignMaskHi == -1,
                "buildVertexSignMask flips the original low sign-mask bit for negative edge normals");
    }

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

    {
        int16 modelDesc[11] = {};
        modelDesc[2] = kProjectionSphereColor;
        modelDesc[7] = 2;
        modelDesc[8] = 20;
        modelDesc[9] = 3;
        modelDesc[10] = 30;
        g_detailLevel = 0;
        g_offscreenRender = 0;
        g_spinAngle = kSetupSpinStart;
        g_frameRateScaling = kSetupFrameRate;
        g_simStepsThisFrame = kSetupStepsThisFrame;
        g_transformModelVerticesCalls = 0;
        g_projectionSphereCalls = 0;
        setup3DTransform(modelDesc, 1, 2, 3, 4, 5, 6, 1);
        require(g_viewPosX == 4 &&
                    g_viewPosY == 5 &&
                    g_viewPosZ == 6 &&
                    g_offscreenRender == 1 &&
                    g_transformModelVerticesCalls == 0 &&
                    g_projectionSphereCalls == 1 &&
                    g_lastProjectionSphereColor == kProjectionSphereColor &&
                    g_sortedObjCount == 0 &&
                    g_spinAngle == kSetupSpinAfter,
                "setup3DTransform sets viewport/camera state, enables offscreen detail-0 render, draws sphere, and advances spin");
        g_renderSortedCalls = 0;
        g_setBlitOffset2Calls = 0;
        g_offscreenRender = 1;
        rasterize3DWorld();
        require(g_renderSortedCalls == 1 &&
                    g_setBlitOffset2Calls == 1 &&
                    g_offscreenRender == 0,
                "rasterize3DWorld flushes sorted objects, restores blit offset, and clears offscreen render");
        renderMapTerrain(modelDesc, 0, 0, kRenderMapZoomShift);
        require(g_renderSortedCalls >= 2,
                "renderMapTerrain runs setup, map tile pass, and final rasterize path");

        g_detailLevel = 1;
        g_offscreenRender = 0;
        g_transformModelVerticesCalls = 0;
        setup3DTransform(modelDesc, 1, 2, 3, 4, 5, 6, 1);
        require(g_transformModelVerticesCalls == 1 &&
                    g_offscreenRender == 0,
                "setup3DTransform transforms model vertices immediately when original offscreen rendering stays disabled");
    }

    {
        struct TileSceneObject mapTileScene = {};
        uint8 tileModel[4] = {};
        std::memset(g_topLodGrid, 0, sizeof(int16) * 66);
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
        std::memset(buf3d3, 0, sizeof(uint16) * kShapeOffsetTableEntries);
        std::memset(g_world3dData, 0, sizeof(g_world3dData));
        g_viewCenterX = 0;
        g_viewCenterY = 0;
        g_clipMaxX = 1;
        g_clipMaxY = 1;
        g_topLodGrid[kNearestTileTopGridIndex] = kNearestTileTopGridCell;
        matrix3dt[4][kNearestTileTopGridCell] = 1;
        matrix3dt_2[4][kNearestTileTopGridCell] = &mapTileScene;
        mapTileScene.shape = kNearestTileShape;
        mapTileScene.z = 0;
        buf3d3[kNearestTileShape] = kNearestTileModelOffset;
        tileModel[1] = kMapPointOpcode;
        tileModel[2] = kMapPointColor;
        std::memcpy(g_world3dData + kNearestTileModelOffset, tileModel, sizeof(tileModel));
        g_clipLineCalls = 0;
        drawMapTiles(0, 0, 4);
        require(g_clipLineCalls == 1,
                "drawMapTiles draws original ground-level tile objects through drawMapTileObject");
    }

    g_planeTable.planes[5].nameIndex = 0x80 | 2;
    g_shapeTargetCategory[2] = kCompatTargetCategory;
    require(missileTargetCompat(kCompatWeaponType, 5) ==
                static_cast<int>(static_cast<int8>(
                    g_targetCompatTable[kCompatWeaponType * 13 + kCompatTargetCategory])),
            "missileTargetCompat masks target name and indexes original weapon/category table");

    g_mapCenterX = 0x2000;
    g_mapCenterY = 0x3000;
    g_mapZoomLevel = 8;
    require(mapXToScreen(0x2400) == (((0x2400 - 0x2000) >> 2) + 60),
            "mapXToScreen uses original center and zoom formula");
    require(mapYToScreen(0x3400) == ((((0x3400 - 0x3000) >> 2) * 3 >> 1 >> 1) + 140),
            "mapYToScreen uses original 3/4 vertical aspect formula");

    g_viewX_ = 0x2000;
    g_viewY_ = 0x3000;
    g_ourHead = 0;
    g_radarScopeRange = 1;
    projectMapPoint(0x2100, 0x2F00);
    {
        const int shift = kRadarBaseShift - static_cast<char>(g_radarScopeRange);
        const int scaledX = (0x2100 - 0x2000) >> shift;
        const int scaledY = (0x3000 - 0x2F00) >> shift;
        require(vtxScratch.vproj.x.lo == kRadarProjectionCenterX + scaledX &&
                    vtxScratch.vproj.y.lo == kRadarProjectionCenterY - scaledY &&
                    g_projDepth == 0,
                "projectMapPoint preserves original radar-center projection for visible points");
    }
    projectMapPoint(0x4000, 0x3000);
    require(g_projDepth == -1 && vtxScratch.vproj.x.lo > kRadarClipRight,
            "projectMapPoint marks points outside the original radar clip as hidden");
    projectMapPoint(0x2000, 0x1000);
    require(g_projDepth == -1 && vtxScratch.vproj.y.lo < kRadarClipTop,
            "projectMapPoint applies the original vertical radar clip bounds");

    vtxScratch.vproj.x.lo = 160;
    vtxScratch.vproj.y.lo = 150;
    g_clipLineCalls = 0;
    g_pageSetCalls = 0;
    drawMapMarkerBox(999, 999, 12);
    require(g_pageFront[2] == 12 && g_pageBack[2] == 12 && g_lastGfxColor == 12,
            "drawMapMarkerBox sets original draw colour on both pages");
    require(g_clipLineCalls == 4 && g_pageSetCalls == 0,
            "drawMapMarkerBox emits exactly four one-page line draws");
    require(g_lineX1 == 156 && g_lineY1 == 153 && g_lineX2 == 156 && g_lineY2 == 147,
            "drawMapMarkerBox uses vtxScratch coords for the final original box edge");

    gfxBufPtr = 0x2222;
    g_drawPage = 1;
    g_blitClippedCalls = 0;
    g_blitCallCount = 0;
    blitGaugeSprite(4, 2, 170, 140);
    require(g_blitClippedCalls == 1 &&
                reinterpret_cast<struct SpriteParams *>(g_lastBlitDescriptor) == &gaugeSpriteParams,
            "blitGaugeSprite sends the gauge descriptor through the clipped sprite path");
    require(gaugeSpriteParams.bufPtr == gfxBufPtr &&
                gaugeSpriteParams.srcX == 4 * kGaugeSpriteTileSize + kGaugeSpriteSrcBiasX &&
                gaugeSpriteParams.srcY == 2 * kGaugeSpriteTileSize + kGaugeSpriteSrcBiasY &&
                gaugeSpriteParams.page == 1 &&
                gaugeSpriteParams.dstX == 170 - kGaugeSpriteCenterBias &&
                gaugeSpriteParams.dstY == 140 - kGaugeSpriteCenterBias &&
                gaugeSpriteParams.width == kGaugeSpriteSize &&
                gaugeSpriteParams.height == kGaugeSpriteSize,
            "blitGaugeSprite preserves original 8x8-cell source and centered 7x7 destination");

    g_drawPage = 0;
    g_blitClippedCalls = 0;
    g_blitOpaqueCalls = 0;
    g_blitCallCount = 0;
    blitSprite(11, 12, 21, 22, 31, 32, 1);
    require(g_blitClippedCalls == 1 && g_blitOpaqueCalls == 0 &&
                reinterpret_cast<struct SpriteParams *>(g_lastBlitDescriptor) == &blitSpriteParams,
            "blitSprite uses clipped path for transparent sprites");
    require(blitSpriteParams.bufPtr == gfxBufPtr && blitSpriteParams.srcX == 21 &&
                blitSpriteParams.srcY == 22 && blitSpriteParams.page == 0 &&
                blitSpriteParams.dstX == 11 && blitSpriteParams.dstY == 12 &&
                blitSpriteParams.width == 31 && blitSpriteParams.height == 32 &&
                blitSpriteParams.pad19[0] == 1 &&
                blitSpriteParams.flags == kSpriteTransparentFlag,
            "blitSprite stores original transparent descriptor fields");
    blitSprite(13, 14, 23, 24, 33, 34, 0);
    require(g_blitOpaqueCalls == 1 && blitSpriteParams.pad19[0] == 0 &&
                blitSpriteParams.flags == kSpriteOpaqueFlag,
            "blitSprite uses opaque path and original opaque flag when transparency is off");

    *g_pageFront = 11;
    *g_pageBack = 22;
    *g_pageOffscreen = 33;
    g_copyCallCount = 0;
    cacheScopePanel();
    require(g_copyCallCount == 1 && g_copyCalls[0].srcPage == 11 &&
                g_copyCalls[0].dstPage == 33 &&
                g_copyCalls[0].srcX == kScopePanelX && g_copyCalls[0].srcY == kScopePanelY &&
                g_copyCalls[0].dstX == kScopePanelX && g_copyCalls[0].dstY == kScopePanelY &&
                g_copyCalls[0].width == kScopePanelWidth &&
                g_copyCalls[0].height == kScopePanelHeight,
            "cacheScopePanel copies the original scope rectangle from front to offscreen");
    g_copyCallCount = 0;
    restoreScopePanel();
    require(g_copyCallCount == 2 && g_copyCalls[0].srcPage == 33 &&
                g_copyCalls[0].dstPage == 11 && g_copyCalls[1].srcPage == 11 &&
                g_copyCalls[1].dstPage == 22,
            "restoreScopePanel restores offscreen to front then mirrors front to back");
    g_copyCallCount = 0;
    g_drawPage = 0;
    captureScopePanel();
    require(g_copyCallCount == 1 && g_copyCalls[0].srcPage == 33 &&
                g_copyCalls[0].dstPage == 11,
            "captureScopePanel targets front page when draw page is zero");
    g_copyCallCount = 0;
    g_drawPage = 1;
    captureScopePanel();
    require(g_copyCallCount == 1 && g_copyCalls[0].srcPage == 33 &&
                g_copyCalls[0].dstPage == 22,
            "captureScopePanel targets back page when draw page is nonzero");

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

    g_hudVisible = 1;
    g_mapMode = 0;
    g_mapCenterX = 0x2000;
    g_mapCenterY = 0x3000;
    g_mapZoomLevel = 8;
    g_scopeClipLeft = 10;
    g_scopeClipRight = 100;
    g_scopeClipTop = 110;
    g_scopeClipBottom = 170;
    g_clipLineCalls = 0;
    g_pageSetCalls = 0;
    require(plotMapObject(0x2000, 0x3000, kMapObjectColor, 1) == 0 &&
                g_clipLineCalls == 8 &&
                g_pageSetCalls == 8 &&
                g_pageFront[2] == kMapObjectColor &&
                g_pageBack[2] == kMapObjectColor,
            "plotMapObject draws four original point pixels for a big in-scope marker");
    require(plotMapObject(0, 0, kMapObjectColor, 0) == 1,
            "plotMapObject reports clipped objects outside the scope rectangle");
    require(plotMapObject(0x2000, 0x3000, kHiddenMapObjectColor, 0) == 1,
            "plotMapObject treats color -1 as hidden");
    g_mapMode = 1;
    require(plotMapObject(0x2000, 0x3000, kMapObjectColor, 0) == 0,
            "plotMapObject exits early while radar mode is active");
    g_mapMode = 0;

    regs.h.al = kScreenPixelColor;
    require(readMapPixelColor(0x2000, 0x3000) == kScreenPixelColor &&
                g_biosPixelX == mapXToScreen(0x2000) &&
                g_biosPixelY == mapYToScreen(0x3000) &&
                g_biosPixelPage == 0 &&
                regs.h.ah == 0x0D,
            "readMapPixelColor clamps to screen and sets original BIOS pixel read registers");
    require(readMapPixelColor(0, 0) == -1,
            "readMapPixelColor returns -1 for clipped boundary points");
    g_mapMode = 1;
    require(readMapPixelColor(0x2000, 0x3000) == 0,
            "readMapPixelColor exits early outside tactical map mode");
    g_mapMode = 0;

    g_clipLineCalls = 0;
    drawMapRangeArc(0x2000, 0x3000, 0, kRangeArcColor, 0, 0x20, 0x10);
    require(g_pageFront[2] == kRangeArcColor &&
                g_clipLineCalls == 0,
            "drawMapRangeArc with end below start preserves original no-plot loop result");
    drawMapRangeArc(0x2000, 0x3000, 0, kRangeArcColor, 0, 0, 0x10);
    require(g_clipLineCalls >= 2,
            "drawMapRangeArc plots arc points when connectLines is disabled");
    g_clipLineCalls = 0;
    drawMapRangeArc(0x2000, 0x3000, 0, kRangeArcColor, 1, 0, 0x10);
    require(g_clipLineCalls >= 2,
            "drawMapRangeArc connects successive arc points when requested");
    g_clipLineCalls = 0;
    drawMapRangeArc(kRangeArcOverflowCenter, kRangeArcOverflowCenter, 0, kRangeArcColor, 0, 0, 0);
    require(g_pageFront[2] == kRangeArcColor &&
                g_clipLineCalls == 0,
            "drawMapRangeArc preserves original overflow clamp to map origin");

    g_clipLineCalls = 0;
    g_pageSetCalls = 0;
    drawFullscreenLine(1, 2, 3, 4);
    require(g_clipMaxX == 319 && g_clipMaxY == 199 &&
                g_lineX1 == 1 && g_lineY1 == 2 &&
                g_lineX2 == 3 && g_lineY2 == 4 &&
                g_clipLineCalls == 2 &&
                g_pageSetCalls == 2 &&
                g_lastBlitOffset == 0,
            "drawFullscreenLine draws the original full-screen region on both pages");
    g_pageFront[7] = 20;
    g_pageFront[8] = 96;
    g_pageFront[9] = 48;
    g_pageFront[10] = 271;
    drawViewportLine(50, 21, 60, 22);
    require(g_clipMaxX == 223 &&
                g_clipMaxY == 76 &&
                g_lineX1 == 50 &&
                g_lineY1 == 21 &&
                g_lastBlitOffset == 20 * 320 + 48,
            "drawViewportLine uses the active viewport rectangle for clip and blit offset");
    g_fillCallCount = 0;
    setDrawColor(kDrawPointColor);
    require(g_pageFront[2] == kDrawPointColor &&
                g_pageBack[2] == kDrawPointColor,
            "setDrawColor updates front and back page descriptors");
    fillRectBoth(1, 2, 3, 4);
    require(g_fillCallCount == 2 &&
                g_fillCalls[0].page == g_pageFront &&
                g_fillCalls[1].page == g_pageBack,
            "fillRectBoth fills the same rectangle on both original page descriptors");
    g_clipLineCalls = 0;
    drawColorPoint(7, 8, kDrawPointColor);
    require(g_clipLineCalls == 2 && g_pageFront[2] == kDrawPointColor,
            "drawColorPoint sets color then draws a full-screen point");
    drawMapPoint(9, 10, kDrawPointColor);
    require(g_lineX1 == 9 && g_lineY1 == 10,
            "drawMapPoint forwards screen coordinates to the full-screen point path");

    g_hudVisible = 1;
    g_tacmapIndicators[kIndicatorIdx * 5 + 3] = kIndicatorX1;
    g_tacmapIndicators[kIndicatorIdx * 5 + 4] = kIndicatorY1;
    g_tacmapIndicators[kIndicatorIdx * 5 + 5] = kIndicatorX2;
    g_tacmapIndicators[kIndicatorIdx * 5 + 6] = kIndicatorY2;
    g_tacmapIndicators[kIndicatorIdx * 5 + 7] = kIndicatorOldColor;
    g_switchCallCount = 0;
    switchIndicatorColor(kIndicatorIdx, kIndicatorNewColor);
    require(g_switchCallCount == 2 &&
                g_switchCalls[0].page == g_pageFront &&
                g_switchCalls[1].page == g_pageBack &&
                g_switchCalls[0].oldColor == kIndicatorOldColor &&
                g_switchCalls[0].newColor == kIndicatorNewColor &&
                g_tacmapIndicators[kIndicatorIdx * 5 + 7] == kIndicatorNewColor,
            "switchIndicatorColor switches both pages and stores the new indicator color");
    switchIndicatorColor(kIndicatorIdx, kIndicatorNewColor);
    require(g_switchCallCount == 2,
            "switchIndicatorColor skips work when the indicator already has the requested color");
    g_hudVisible = 0;
    switchIndicatorColor(kIndicatorIdx, kIndicatorOldColor);
    require(g_switchCallCount == 2,
            "switchIndicatorColor does nothing while the HUD is hidden");
    g_hudVisible = 1;

    g_fillCallCount = 0;
    g_centeredLabelCalls = 0;
    clearStatusPanel();
    require(g_fillCalls[0].left == kStatusPanelLeft &&
                g_fillCalls[0].top == kStatusPanelTop &&
                g_fillCalls[0].right == kStatusPanelRight &&
                g_fillCalls[0].bottom == kStatusPanelBottom &&
                g_lastCenteredPanel == 2 &&
                std::strcmp(g_lastCenteredText, "") == 0,
            "clearStatusPanel clears panel 2 through the original panel text path");
    drawPanelText(1, "map", kDrawPointColor);
    require(g_lastCenteredPanel == 1 &&
                std::strcmp(g_lastCenteredText, "map") == 0,
            "drawPanelText fills the panel and forwards text to the centered label box");
    fillPanelBox(3, kDrawPointColor);
    require(g_fillCalls[g_fillCallCount - 2].left == kRightPanelLeft &&
                g_fillCalls[g_fillCallCount - 2].top == kRightPanelTop &&
                g_fillCalls[g_fillCallCount - 2].right == kRightPanelRight &&
                g_fillCalls[g_fillCallCount - 2].bottom == kRightPanelBottom,
            "fillPanelBox uses the original right-panel rectangle for panel 3");

    struct GameComm tacComm = {};
    struct Game tacGame = {};
    commData = &tacComm;
    gameData = &tacGame;
    g_hudVisible = 0;
    g_activePanelMode = 0;
    setActivePanel(kTrackCamPanel);
    require(g_activePanelMode == 0,
            "setActivePanel preserves the original early return while HUD is hidden");
    g_hudVisible = 1;
    for (auto [heading, label] : {std::pair{kTrackCamAheadHeading, "TrackCam Ahead"},
                                  std::pair{kTrackCamRearHeading, "TrackCam Rear"},
                                  std::pair{kTrackCamRightHeading, "TrackCam Right"},
                                  std::pair{kTrackCamLeftHeading, "TrackCam Left"}}) {
        g_viewHeadingOffset = static_cast<int16>(heading);
        setActivePanel(kTrackCamPanel);
        require(g_activePanelMode == kTrackCamPanel &&
                    g_lastCenteredPanel == 2 &&
                    std::strcmp(g_lastCenteredText, label) == 0,
                "setActivePanel preserves original TrackCam direction labels");
    }
    g_viewHeadingOffset = kTrackCamRightHeading;
    setActivePanel(kTrackCamPanel);
    require(g_activePanelMode == kTrackCamPanel &&
                g_lastCenteredPanel == 2 &&
                std::strcmp(g_lastCenteredText, "TrackCam Right") == 0,
            "setActivePanel renders the original TrackCam direction label and stores the active panel");
    g_centeredLabelCalls = 0;
    refreshActivePanel(kTrackCamPanel);
    require(g_centeredLabelCalls == 1,
            "refreshActivePanel redraws only the currently active panel");
    refreshActivePanel(kTrackCamPanel + 1);
    require(g_centeredLabelCalls == 1,
            "refreshActivePanel ignores inactive panel ids");

    keyValue = 0x80;
    g_externalCamDist = kExternalCamStartDistance;
    initTacMapView();
    require(g_mapMode == 0 &&
                g_scopeClipLeft == kTacMapPanelLeft &&
                g_scopeClipRight == kTacMapPanelRight &&
                g_scopeClipTop == kTacMapPanelTop &&
                g_scopeClipBottom == kTacMapPanelBottom &&
                g_scopeCenterX == 72 &&
                g_scopeCenterY == 56 &&
                g_externalCamDist == kExternalCamZoomedInDistance,
            "initTacMapView installs the original scope rectangle and routes through zoomIn");
    zoomOut();
    require(g_externalCamDist == kExternalCamRestoredDistance,
            "zoomOut adjusts external camera distance when the original high-key flag is set");
    keyValue = 0;
    g_mapMode = 1;
    g_radarScopeRange = kRadarRangeStart;
    zoomIn();
    require(g_radarScopeRange == kRadarRangeZoomedIn,
            "zoomIn increases radar scope range in radar mode");
    zoomOut();
    require(g_radarScopeRange == kRadarRangeZoomedOut,
            "zoomOut decreases nonzero radar scope range in radar mode");

    g_mapMode = 0;
    g_mapZoomLevel = kTacMapZoomMax;
    g_loadColorPaletteCalls = 0;
    zoomIn();
    require(g_loadColorPaletteCalls == 0 && g_mapZoomLevel == kTacMapZoomMax,
            "zoomIn leaves the tactical map unchanged at the original max zoom");
    g_hudVisible = 1;
    g_mapZoomLevel = kTacMapZoomStart;
    g_viewX_ = kTacMapCenterX;
    g_viewY_ = kTacMapCenterY;
    g_ourHead = 0;
    tacGame.theater = kRedrawTheaterDesert;
    g_planeCount = 0;
    g_playerPlaneFlags = 0xffff;
    g_loadColorPaletteCalls = 0;
    g_setFadeStepsCalls = 0;
    g_resetSimObjectLocksCalls = 0;
    redrawTacMap(kTacMapCenterX, kTacMapCenterY);
    require(g_mapMode == 0 &&
                g_lastCenteredPanel == 1 &&
                std::strcmp(g_lastCenteredText, "Map") == 0 &&
                g_loadColorPaletteCalls == 1 &&
                g_lastPaletteIndex == kPaletteLoadIndex &&
                g_setFadeStepsCalls >= 2 &&
                g_lastFadeSteps == kFadeDesert &&
                g_resetSimObjectLocksCalls == 1,
            "redrawTacMap refreshes the original map panel, palette/fade sequence, and sim-object locks");
    g_hudVisible = 0;
    g_loadColorPaletteCalls = 0;
    redrawTacMap(kTacMapCenterX, kTacMapCenterY);
    require(g_mapMode == 0 && g_loadColorPaletteCalls == 0,
            "redrawTacMap preserves original hidden-HUD early return after resetting map mode");
    g_hudVisible = 1;
    g_mapZoomLevel = kTacMapZoomStart;
    g_drawPage = 1;
    g_displayPageResult = 1;
    g_planeCount = 4;
    g_playerPlaneFlags = 0;
    tacGame.theater = kRedrawTheaterNonDesert;
    std::memset(g_planeTable.planes, 0, sizeof(g_planeTable.planes));
    g_planeTable.planes[1].active = 1;
    g_planeTable.planes[1].mapX = kTacMapCenterX;
    g_planeTable.planes[1].mapY = kTacMapCenterY;
    g_planeTable.planes[2].flags = kRadarPlaneFlagDirectional;
    g_planeTable.planes[2].mapX = kTacMapCenterX + 0x20;
    g_planeTable.planes[2].mapY = kTacMapCenterY;
    g_planeTable.planes[3].flags = kRadarPlaneFlagHidden;
    g_planeTable.planes[3].active = 1;
    g_planeTable.planes[3].mapX = kTacMapCenterX;
    g_planeTable.planes[3].mapY = kTacMapCenterY;
    waypoints[1].mapX = kTacMapCenterX + 0x40;
    waypoints[1].mapY = kTacMapCenterY;
    waypoints[2].mapX = kTacMapCenterX + 0x60;
    waypoints[2].mapY = kTacMapCenterY;
    g_blitClippedCalls = 0;
    g_blitOpaqueCalls = 0;
    g_blitCallCount = 0;
    g_copyCallCount = 0;
    g_setFadeStepsCalls = 0;
    g_resetSimObjectLocksCalls = 0;
    redrawTacMap(kTacMapCenterX, kTacMapCenterY);
    require(g_lastFadeSteps == kFadeNonDesert &&
                sawSpriteDescriptor(kRedrawActivePlaneSpriteX, kRedrawMapMarkerSpriteY,
                                    kRedrawMapMarkerSize, kRedrawMapMarkerSize) &&
                sawSpriteDescriptor(kRedrawGroundTargetSpriteX, kRedrawMapMarkerSpriteY,
                                    kRedrawMapMarkerSize, kRedrawMapMarkerSize) &&
                sawSpriteDescriptor(kRedrawWaypointSpriteX, kRedrawMapMarkerSpriteY,
                                    kRedrawMapMarkerSize, kRedrawMapMarkerSize),
            "redrawTacMap preserves original active-plane, ground-target, waypoint, and non-desert fade branches");
    require(g_drawPage == 1 &&
                g_copyCallCount >= 3 &&
                g_copyCalls[0].srcPage == *g_pageBack &&
                g_copyCalls[0].dstPage == *g_pageOffscreen &&
                g_copyCalls[0].width == kRedrawBackCacheWidth &&
                g_copyCalls[0].height == kRedrawBackCacheHeight,
            "redrawTacMap preserves original back-page cache copy and restores draw page");
    g_displayPageResult = 0;
    g_mapZoomLevel = kTacMapZoomStart;
    zoomIn();
    require(g_mapZoomLevel == kTacMapZoomStart + 1 &&
                g_resetSimObjectLocksCalls == 2,
            "zoomIn redraws the tactical map when below max zoom");
    g_mapZoomLevel = kTacMapZoomMinSample;
    zoomOut();
    require(g_mapZoomLevel == kTacMapZoomMinSample - 1 &&
                g_resetSimObjectLocksCalls == 3,
            "zoomOut redraws the tactical map when above min zoom");

    g_hudVisible = 0;
    g_hudDrawnFlag = 0;
    g_hudMsgTimer = 0;
    g_dirMsgTimer = 0;
    waypointIndex = kHudFrameWaypointIndex;
    waypoints[kHudFrameWaypointIndex].mapX = kHudFrameWaypointX;
    waypoints[kHudFrameWaypointIndex].mapY = kHudFrameWaypointY;
    g_viewX_ = kHudFrameViewX;
    g_viewY_ = kHudFrameViewY;
    renderHudFrame(0);
    require(g_hudDrawnFlag == 0 &&
                static_cast<uint16>(g_waypointBearing) ==
                    static_cast<uint16>(expectedBearing(kHudFrameWaypointX - kHudFrameViewX,
                                                        -(kHudFrameWaypointY - kHudFrameViewY))),
            "renderHudFrame always updates the original waypoint bearing before hidden-HUD exit");

    g_hudVisible = 1;
    g_hudDrawnFlag = 0;
    g_damageTakenFlag = 1;
    keyValue = 0;
    g_halfScaleRender = 0;
    tacComm.setupUseJoy = 0;
    joyAxes[0] = 120;
    joyAxes[1] = 120;
    g_pageFront[7] = kHudFrameViewportTop;
    g_pageFront[8] = kHudFrameViewportBottom;
    g_pageFront[9] = kHudFrameViewportLeft;
    g_pageFront[10] = kHudFrameViewportRight;
    g_playerPlaneFlags = kHudFrameGunCrossFlag | kHudFrameTrainingFlag;
    g_cornerSpeed = kHudFrameCornerSpeed;
    g_knots = kHudFrameKnots;
    g_climbRate = kHudFrameNegativeClimb;
    frameTick = 1;
    tacGame.unk4 = 2;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].flags = 0x200;
    g_groundAltitude = kHudFrameGroundAlt;
    g_viewZ = 0;
    g_currentWeaponType = 0;
    g_rollPitchTrim = kHudFrameFlightPathTrim;
    g_altitude = kHudFrameAltitude;
    g_slowMotionMode = 2;
    g_autopilotAltitude = kHudFrameAutopilotAltitude;
    g_nightMode = 0;
    g_directorMode = 0;
    g_hudMsgTimer = 0;
    g_dirMsgTimer = 0;
    g_groundUnitCount = 0;
    g_planeCount = 0;
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 20);
    std::memset(mapEvents, 0, sizeof(struct MapEvent) * 4);
    g_clipLineCalls = 0;
    g_fillCallCount = 0;
    g_blitClippedCalls = 0;
    g_blitOpaqueCalls = 0;
    g_blitCallCount = 0;
    g_drawStringCalls = 0;
    g_dacAnimCountCalls = 0;
    renderHudFrame(0);
    require(g_hudDrawnFlag == 1 &&
                g_damageTakenFlag == 0 &&
                g_dacAnimCountCalls == 1 &&
                g_lastDacAnimCount == kHudFrameDamageDacCount,
            "renderHudFrame preserves original damage flash, DAC timer, and HUD drawn flag");
    require(g_clipLineCalls > 20,
            "renderHudFrame preserves original visible-HUD line drawing");
    require(g_blitClippedCalls > 0,
            "renderHudFrame preserves original visible-HUD marker blit");
    require(g_drawStringCalls >= 8,
            "renderHudFrame preserves original visible-HUD number and status drawing");

    g_hudVisible = 1;
    g_hudDrawnFlag = 0;
    g_damageTakenFlag = 0;
    keyValue = 0;
    g_halfScaleRender = 0;
    g_currentWeaponType = 1;
    g_aamSeekerX = 0;
    g_aamSeekerY = 0;
    missileSpecIndex = 0;
    missleSpec[0].weaponIdx = 0;
    missiles[0].specIndex = 0;
    sams[0].weaponClass = kHudFrameAamWeaponClass;
    g_knots = kHudFrameKnots;
    g_cornerSpeed = kHudFrameCornerSpeed;
    g_climbRate = 0;
    g_groundAltitude = kHudFrameGroundAlt;
    g_viewZ = 0;
    g_altitude = kHudFrameAltitude;
    g_slowMotionMode = 0;
    g_playerPlaneFlags = 0;
    g_autopilotAltitude = 0;
    g_autopilotEngaged = 1;
    g_hudMsgTimer = kHudFrameMessageTimer;
    g_dirMsgTimer = kHudFrameDirectorTimer;
    std::strcpy(tempString, "status");
    std::strcpy(string_3C04A, "director");
    g_clipLineCalls = 0;
    g_blitClippedCalls = 0;
    g_blitCallCount = 0;
    g_drawStringCalls = 0;
    renderHudFrame(0);
    require(g_blitClippedCalls > 0 &&
                g_clipLineCalls > 30,
            "renderHudFrame preserves original AAM seeker sprite and circle drawing");
    require(g_hudMsgTimer == kHudFrameMessageTimer - 1 &&
                g_dirMsgTimer == kHudFrameDirectorTimer - 1 &&
                g_drawStringCalls >= 6,
            "renderHudFrame preserves original HUD and director timed message decrements");

    g_hudVisible = 1;
    keyValue = 0;
    g_halfScaleRender = 0;
    g_currentWeaponType = 2;
    g_playerPlaneFlags = 0;
    frameTick = 1;
    tacGame.unk4 = 2;
    g_climbRate = kHudFrameNegativeClimb;
    g_closestThreatIndex = 0;
    g_planeTable.planes[0].flags = 0;
    g_knots = kHudFrameKnots;
    g_cornerSpeed = kHudFrameCornerSpeed;
    g_groundAltitude = kHudFrameGroundAlt;
    g_viewZ = 0;
    g_hudMsgTimer = 0;
    g_dirMsgTimer = 0;
    g_clipLineCalls = 0;
    g_drawStringCalls = 0;
    renderHudFrame(0);
    require(g_clipLineCalls > 20 &&
                g_drawStringCalls > 0,
            "renderHudFrame preserves original climb cue and stall warning branches");

    g_hudVisible = 1;
    keyValue = 1; /* Nonzero key skips the detailed HUD block and falls through to tactical-map redraw. */
    g_halfScaleRender = 0;
    g_fillCallCount = 0;
    renderHudFrame(0);
    require(g_fillCallCount > 0,
            "renderHudFrame preserves original tactical-map redraw path when detailed HUD is skipped");
    keyValue = 0;

    g_hudVisible = 1;
    g_halfScaleRender = 0;
    g_missionStatus = 0;
    g_groundUnitCount = 0;
    g_planeCount = 0;
    g_radarScopeRange = kRadarRangeStart;
    g_detailLevel = 0;
    g_viewX_ = kHudFrameViewX;
    g_viewY_ = kHudFrameViewY;
    g_ourHead = 0;
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 20);
    std::memset(mapEvents, 0, sizeof(struct MapEvent) * 4);
    g_fillCallCount = 0;
    g_blitClippedCalls = 0;
    g_blitCallCount = 0;
    drawTacticalMap(kDrawTacticalMapPage);
    require(g_fillCallCount > 0 &&
                g_fillCalls[0].page == g_pageBack &&
                g_fillCalls[0].left == kStatusPanelLeft &&
                g_fillCalls[0].top == kStatusPanelTop &&
                g_fillCalls[0].right == kStatusPanelRight &&
                g_fillCalls[0].bottom == kStatusPanelBottom &&
                g_blitClippedCalls >= 1,
            "drawTacticalMap clears the original radar panel page and draws the ownship gauge marker");

    g_hudVisible = 1;
    g_halfScaleRender = 0;
    g_missionStatus = 0;
    g_radarScopeRange = kRadarRangeStart;
    g_detailLevel = 0;
    g_viewX_ = kHudFrameViewX;
    g_viewY_ = kHudFrameViewY;
    g_viewZ = 0;
    g_ourHead = 0;
    g_currentWeaponType = 2;
    g_scopeSweepTimer = 1;
    g_scopeArcColor = kRadarThreatMarkerColor;
    g_threatLabelTarget = kRadarThreatLabelForObj1;
    g_groundUnitCount = kRadarGroundObjCount;
    g_planeCount = kRadarPlaneCount;
    std::memset(g_simObjects, 0, sizeof(struct SimObject) * 32);
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 20);
    std::memset(g_planeTable.planes, 0, sizeof(g_planeTable.planes));
    std::memset(mapEvents, 0, sizeof(struct MapEvent) * 4);
    for (int i = 0; i < kRadarGroundObjCount; ++i) {
        g_simObjects[i].flags.w = kRadarObjVisibleFlag;
        g_simObjects[i].speed = kRadarObjSpeed;
        g_simObjects[i].heading.w = kRadarObjHeading;
        g_simObjects[i].posX = kHudFrameViewX + 0x100 + i * 0x40;
        g_simObjects[i].posY = kHudFrameViewY - 0x100;
    }
    g_simObjects[0].alt = kRadarObjAltLevel;
    g_simObjects[1].alt = kRadarObjAltLow;
    g_simObjects[2].alt = kRadarObjAltHigh;
    sams[kRadarProjectileSamSpec].weaponClass = kRadarProjectileClassSam;
    sams[kRadarProjectileMissileSpec].weaponClass = kRadarProjectileClassMissile;
    sams[kRadarProjectileClass3Spec].weaponClass = kRadarProjectileClassSpecial;
    sams[kRadarProjectilePlayerSpec].weaponClass = kRadarProjectileClassMissile;
    g_projectiles[0].ttl = kRadarProjectileTtl;
    g_projectiles[0].specIdx = kRadarProjectileSamSpec;
    g_projectiles[0].alt = 1; /* Odd altitude avoids the original even-alt color override. */
    g_projectiles[1].ttl = kRadarProjectileTtl;
    g_projectiles[1].specIdx = kRadarProjectileMissileSpec;
    g_projectiles[1].alt = 1;
    g_projectiles[2].ttl = kRadarProjectileTtl;
    g_projectiles[2].specIdx = kRadarProjectileClass3Spec;
    g_projectiles[2].alt = 1;
    g_projectiles[3].ttl = kRadarProjectileTtl;
    g_projectiles[3].specIdx = kRadarProjectileMissileSpec;
    g_projectiles[3].alt = 2; /* Even altitude forces the original neutral color. */
    g_projectiles[8].ttl = kRadarProjectileTtl;
    g_projectiles[8].specIdx = kRadarProjectilePlayerSpec;
    g_projectiles[8].alt = 2; /* Player-fired slots override the even-alt color. */
    for (int i : {0, 1, 2, 3, 8}) {
        g_projectiles[i].mapX = kHudFrameViewX + 0x100 + i * 0x10;
        g_projectiles[i].mapY = kHudFrameViewY - 0x100;
        g_projectiles[i].worldX = kRadarObjHeading;
    }
    g_planeTable.planes[0].mapX = kHudFrameViewX + 0x140;
    g_planeTable.planes[0].mapY = kHudFrameViewY - 0x100;
    g_planeTable.planes[0].flags = kRadarPlaneFlagDirectional;
    g_planeTable.planes[1].mapX = kHudFrameViewX + 0x180;
    g_planeTable.planes[1].mapY = kHudFrameViewY - 0x100;
    g_planeTable.planes[1].active = kRadarPlaneActive;
    g_planeTable.planes[2].mapX = kHudFrameViewX + 0x1c0;
    g_planeTable.planes[2].mapY = kHudFrameViewY - 0x100;
    g_planeTable.planes[2].flags = kRadarPlaneFlagLanded;
    g_planeTable.planes[3].mapX = kHudFrameViewX + 0x200;
    g_planeTable.planes[3].mapY = kHudFrameViewY - 0x100;
    g_planeTable.planes[4].flags = kRadarPlaneFlagHidden;
    g_groundTargetLock = 2;
    g_targetSlots[0].planeIndex = 3;
    g_targetSlots[1].planeIndex = -1;
    mapEvents[0].ttl = kRadarEventTtl;
    mapEvents[0].type = kRadarEventTypeChaff;
    mapEvents[0].mapX = kHudFrameViewX + 0x100;
    mapEvents[0].mapY = kHudFrameViewY - 0x100;
    mapEvents[1].ttl = kRadarEventTtl;
    mapEvents[1].type = kRadarEventTypeFlare;
    mapEvents[1].mapX = kHudFrameViewX + 0x180;
    mapEvents[1].mapY = kHudFrameViewY - 0x100;
    g_fillCallCount = 0;
    g_blitClippedCalls = 0;
    g_blitCallCount = 0;
    g_colorCallCount = 0;
    drawTacticalMap(kDrawTacticalMapPage);
    require(sawGaugeSprite(kRadarGaugeColOwnship, kRadarGaugeRowPlane) &&
                sawGaugeSprite(kRadarGaugeColDirectionalBase, kRadarGaugeRowPlane) &&
                sawGaugeSprite(kRadarGaugeColActivePlane, kRadarGaugeRowPlane) &&
                sawGaugeSprite(kRadarGaugeColGroundLock, kRadarGaugeRowPlane) &&
                sawGaugeSprite(kRadarPlaneTargetSlotCode, kRadarGaugeRowPlane),
            "drawTacticalMap preserves original ownship and plane gauge sprite priority");
    require(sawGaugeSprite((kRadarObjHeading + 0x800) >> 12, kRadarGaugeRowGroundLevel) &&
                sawGaugeSprite((kRadarObjHeading + 0x800) >> 12, kRadarGaugeRowGroundLow) &&
                sawGaugeSprite((kRadarObjHeading + 0x800) >> 12, kRadarGaugeRowGroundHigh),
            "drawTacticalMap preserves original ground-unit heading and altitude-band icons");
    require(sawGaugeSprite(kRadarGaugeColChaff, kRadarGaugeRowPlane) &&
                sawGaugeSprite(kRadarGaugeColFlare, kRadarGaugeRowPlane),
            "drawTacticalMap preserves original countermeasure map event icons");
    require(sawDrawColor(kRadarThreatMarkerColor),
            "drawTacticalMap preserves original unsigned 16-bit threat-label object marker");
    require(sawDrawColor(kRadarProjectileSamColor) &&
                sawDrawColor(kRadarProjectileMissileColor) &&
                sawDrawColor(kRadarProjectileClass3Color) &&
                sawDrawColor(kRadarProjectileEvenAltColor) &&
                sawDrawColor(kRadarProjectilePlayerColor),
            "drawTacticalMap preserves original projectile radar color priority");

    g_radarScopeRange = kRadarRangeStart;
    g_detailLevel = 1; /* Non-zero detail selects the original expanded radar grid step. */
    g_viewX_ = kHudFrameViewX;
    g_viewY_ = kHudFrameViewY;
    g_viewZ = 0;
    g_ourHead = 0;
    g_currentWeaponType = 1;
    g_airTargetLock = 0;
    g_groundUnitCount = 1;
    g_planeCount = 0;
    std::memset(g_simObjects, 0, sizeof(struct SimObject) * 32);
    std::memset(g_projectiles, 0, sizeof(struct Projectile) * 20);
    std::memset(mapEvents, 0, sizeof(struct MapEvent) * 4);
    g_simObjects[0].flags.w = kRadarObjVisibleFlag;
    g_simObjects[0].speed = kRadarObjSpeed;
    g_simObjects[0].posX = kHudFrameViewX + 0x100;
    g_simObjects[0].posY = kHudFrameViewY - 0x100;
    g_clipLineCalls = 0;
    g_colorCallCount = 0;
    drawTacticalMap(kDrawTacticalMapPage);
    require(sawDrawColor(kRadarAirTargetMarkerColor) && g_clipLineCalls > 8,
            "drawTacticalMap preserves original high-detail grid step and air-target marker box");

    g_pageFront[7] = 20;
    g_pageFront[8] = 96;
    g_pageFront[9] = 48;
    g_pageFront[10] = 271;
    g_clipLineCalls = 0;
    g_halfScaleRender = 0;
    g_missionStatus = 1;
    drawHudViewLine(1, 2, 3, 4);
    require(g_clipLineCalls == 1 &&
                g_lineX1 == -47 &&
                g_lineY1 == -13,
            "drawHudViewLine uses the original mission-status HUD clip region");
    g_halfScaleRender = 0;
    g_missionStatus = 0;
    drawHudViewLine(12, 23, 34, 45);
    require(g_lineX1 == 12 &&
                g_lineY1 == 23,
            "drawHudViewLine uses the original viewport path outside mission-status and half-scale modes");
    g_halfScaleRender = 1;
    tacGame.unk4 = 2;
    drawHudViewLine(10, 20, 30, 40);
    require(g_lineX1 == -94 &&
                g_lineY1 == -42,
            "drawHudViewLine uses the original half-scale director clip when gameData->unk4 is at least two");
    tacGame.unk4 = 1;
    drawHudViewLine(50, 21, 60, 22);
    require(g_clipMaxX == 223 &&
                g_clipMaxY == 76 &&
                g_lineX1 == 50 &&
                g_lineY1 == 21,
            "drawHudViewLine falls back to the viewport line path for low half-scale detail");

    g_drawStringCalls = 0;
    egDrawStringCentered(g_pageFront, "mixed", 12, 34, kStringColor);
    require(g_pageFront[6] == 0 &&
                g_pageFront[4] == 12 &&
                g_pageFront[5] == 34 &&
                g_pageFront[2] == kStringColor &&
                std::strcmp(g_lastDrawnString, "MIXED") == 0,
            "egDrawStringCentered uppercases text and writes original page descriptor fields");
    drawStringBothPages("both", 1, 2, kStringColor);
    require(g_drawStringCalls == 3 &&
                g_lastStringPage == g_pageBack &&
                std::strcmp(g_lastDrawnString, "BOTH") == 0,
            "drawStringBothPages draws front then back page centered strings");
    g_drawPage = 0;
    drawStringActivePage("front", 3, 4, kStringColor);
    require(g_lastStringPage == g_pageFront,
            "drawStringActivePage uses the front page when drawPage is zero");
    g_drawPage = 1;
    drawStringActivePage("back", 5, 6, kStringColor);
    require(g_lastStringPage == g_pageBack,
            "drawStringActivePage uses the back page when drawPage is nonzero");
    drawNumber(kNumberValue, 7, 8, kStringColor);
    require(std::strcmp(g_lastDrawnString, "12345") == 0,
            "drawNumber formats decimal values through original both-page string path");
    g_frameRateScaling = 4;
    tempStrcpy("status");
    require(std::strcmp(tempString, "status") == 0 &&
                g_hudMsgTimer == kTimedMessageFrames,
            "tempStrcpy copies HUD text and sets the original three-second timer");
    char timedMessage[] = "director";
    setTimedMessage(timedMessage);
    require(std::strcmp(string_3C04A, "director") == 0 &&
                g_dirMsgTimer == kTimedMessageFrames,
            "setTimedMessage copies director text and sets the original three-second timer");

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

    for (int i = 0; i < 9; ++i) {
        g_camRotMatrix[i] = 0;
    }
    g_camRotMatrix[0] = kRangeMax;
    g_camRotMatrix[5] = kRangeMax;
    g_camRotMatrix[7] = kRangeMax;
    g_viewX_ = kHudProjWorldX;
    g_viewY_ = kHudProjWorldY;
    g_viewZ = kHudProjViewZ;
    keyValue = 0;
    g_halfScaleRender = 0;
    g_pageFront[8] = 199;
    projectWorldToHud(kHudProjWorldX, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo == kHudProjCenterX &&
                vtxScratch.vproj.y.lo == kHudProjFullHeightCenterY &&
                g_projDepth == kHudProjStoredDepth,
            "projectWorldToHud projects centered points through the original full-height HUD center");
    keyValue = kHudCrashCamFlag;
    g_ViewX = g_camEyeX + kHudCamEyeDelta;
    g_ViewY = g_camEyeY;
    g_camEyeZ = g_viewZ;
    projectWorldToHud(kHudProjWorldX, kHudProjWorldY, 0);
    require(vtxScratch.vproj.x.lo != -1,
            "projectWorldToHud preserves original crash-camera relative-eye adjustment path");
    keyValue = 0;
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

    g_ourPitch = 0x1000;
    g_viewZ = 0x2000;
    require(computeLoftAngle() ==
                static_cast<int>((static_cast<unsigned long>((0x4000 - std::abs(g_ourPitch)) << 12) /
                                  static_cast<unsigned int>(g_viewZ + 0x1000)) -
                                 0x4000),
            "computeLoftAngle matches original unsigned divide formula");

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

    {
        struct TileSceneObject waypointScene = {};
        std::memset(matrix3dt, 0, sizeof(matrix3dt));
        std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
        std::memset(g_shapeTargetCategory, 0, sizeof(g_shapeTargetCategory));
        std::memset(g_planeTable.planes, 0, sizeof(g_planeTable.planes));
        std::memset(g_world3dData, 0, sizeof(g_world3dData));
        std::memset(buf3d3, 0, sizeof(uint16) * 100);
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

    int16 rect[] = {0, 0, 0, 0, 0, 0, 0, 10, 109, 20, 219};
    g_halfScaleRender = 0;
    g_hudVisible = 0;
    setupViewport(rect);
    require(g_viewCenterX == 99 && g_viewCenterY == 49,
            "setupViewport centers non-top viewport from rectangle size");
    require(g_clipMaxX == 199 && g_clipMaxY == 99,
            "setupViewport updates clip bounds");
    require(g_lastOverlayMaxX == 199 && g_lastBlitOffset == 10 * 320 + 20,
            "setupViewport forwards original overlay bounds and row address");
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
