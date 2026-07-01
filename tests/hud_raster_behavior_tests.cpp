#include "egcode.h"
#include "egdata.h"
#include "egtypes.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

extern void FAR CDECL hudComplex(int bx, int dx, int cx, int si);
extern int FAR drawClipLineGlobal(void);
extern void FAR CDECL hudRotateLadder(int di);
extern void __cdecl __far drawInstrumentGaugesFar(void);
extern uint8 g_tapeChar;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum HudRasterOriginalConstant : int {
    kPageWidth = 320,
    kPageHeight = 200,
    kPageBytes = kPageWidth * kPageHeight,
    kFrontPage = 0,
    kBackPage = 1,
    kFrontPageSeg = 0x1110,
    kBackPageSeg = 0x2220,
    kSavedPageSeg = 0x3330,
    kFillColor = 0x0A,
    kFillLeft = 3,
    kFillTop = 4,
    kFillRight = 6,
    kFillBottom = 7,
    kPresetOffset1 = 0x5580,
    kPresetOffset2 = 0x1950,
    kActiveTapeOriginX = 94,
    kActiveTapeCursorBackShift = 17,
    kActiveTapeTickPitch = 20,
    kActiveSpeedTapeTickStep = 0x31,
    kActiveAltTapeTickStep = 0xFF,
    kActiveHeadingBase = 109,
    kActiveHeadingPixPerDeg = 45,
    kActiveCompassWrapLimit = 248,
    kActivePitchVtxX0 = 0xFFC4,
    kActivePitchVtxX3 = 60,
    kActivePitchRungVStep = 52,
    kActivePitchCenterY = 104,
    kActivePitchBlitOfs = kPresetOffset2,
    kActivePitchClipMaxX = 160,
    kActivePitchClipMaxY = 76,
    kLadderStartBx = 87,
    kLadderAdjustedBx = 98,
    kLadderSkipBx = 120,
    kLadderTopRow = 86,
    kLadderSkippedRow = 85,
    kLadderBaseCol = 71,
    kLadderAdjustedBaseCol = 120,
    kLadderAdjustedRow = 98,
    kLadderColor = 0x0F,
    kLadderCxAdjust = 1,
    kClipMaxX = 100,
    kClipMaxY = 50,
    kInsideX1 = 10,
    kInsideY1 = 11,
    kInsideX2 = 90,
    kInsideY2 = 40,
    kClipLeftX1 = -10,
    kClipLeftY1 = 10,
    kClipLeftX2 = 10,
    kClipLeftY2 = 30,
    kClipLeftExpectedX1 = 0,
    kClipLeftExpectedY1 = 20,
    kClipRightX2 = 120,
    kClipRightExpectedX2 = 100,
    kClipBottomY2 = 70,
    kClipBottomExpectedX2 = 62,
    kClipBottomExpectedY2 = 50,
    kClipTopY1 = -10,
    kClipTopExpectedX1 = 26,
    kClipTopExpectedY1 = 0,
    kRejectAboveY = -5,
    kRotateVertexOffset = 0,
    kRotateInputX = 100,
    kRotateInputY = 50,
    kRotateZeroRollExpectedX = 99,
    kRotateZeroRollExpectedY = 37,
    kCompassPitchXBase = 0xEC,
    kCompassPitchYBase = 0x15C,
    kGaugeDisplayPage = 1,
    kGaugeKnots = 275,
    kGaugeAltitude = 12345,
    kGaugeHeading = 0x3000,
    kGaugePitch = 0,
    kGaugeRoll = 0,
    kGaugeSpriteBuffer = 0x2468,
    kGaugeGlyphSlotMax = 8,
    kGaugeExpectedGlyphSlot1 = 1,
    kGaugeExpectedGlyphSlot2 = 2,
    kGaugeExpectedGlyphSlot3 = 3,
    kGaugeExpectedGlyphSlot4 = 4,
    kGaugeFrameColor = 8,
    kGaugeBrightColor = 0x0F,
    kGaugeAltBelowThousand = 450,
    kGaugeAltBelowHundred = 50,
    kGaugeAltHundredsToThousandsJoin = 950,
    kGaugeAltExactlyOneThousand = 1000,
    kGaugeHighKnots = 1200,
    kGaugeNorthHeading = 0x2000,
    kGaugeNoCompassWrapLimit = 0,
    kGaugeFullScaleVerticalLineX = 103,
    kGaugeFullScaleVerticalLineTopY = 63,
    kGaugeFullScaleVerticalLineBottomY = 96,
    kGaugeFullScaleFrameColor = 8,
    kGaugePitchUp = 0x1800,
    kGaugePitchExtremeUp = 0x4200,
    kGaugePitchExtremeDown = -0x4200,
    kGaugePitchLabelOverflow = 0x7FFF,
    kGaugeRollRight = 0x2000,
    kGaugePitchLabelSlot = 6,
    kGaugePitchLadderColor = 7,
    kGaugePositiveExtremeRungSegments = 13,
    kGaugeNegativeExtremeRungSegments = 18,
    kExpectedOneCall = 1,
    kTestFailureExitCode = 1,
};

uint8 g_frontPage[kPageBytes] = {};
uint8 g_backPage[kPageBytes] = {};
uint16 g_currentPageSeg = kSavedPageSeg;
uint16 g_lastSetPage = 0;
uint16 g_restoredSeg = 0;
int g_setPageCalls = 0;
int g_restoreSegCalls = 0;
int g_drawLineCalls = 0;
uint16 g_drawLineX1 = 0;
uint16 g_drawLineY1 = 0;
uint16 g_drawLineX2 = 0;
uint16 g_drawLineY2 = 0;
int g_lastSetColor = -1;
int g_setColorCalls = 0;
int g_lastBlitOffset = -1;
int g_setBlitOffsetCalls = 0;
struct LineCall {
    uint16 x1;
    uint16 y1;
    uint16 x2;
    uint16 y2;
};
enum HudRasterRecorderLimit : int {
    kMaxRecordedLines = 64,
};
LineCall g_lineCalls[kMaxRecordedLines] = {};
int g_glyphCalls = 0;
int g_glyphSlotCalls[kGaugeGlyphSlotMax] = {};
const int16 *g_lastGlyphDesc = nullptr;
const char *g_lastGlyphString = nullptr;
int g_blitSpriteCalls = 0;
struct SpriteParams *g_lastSprite = nullptr;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

int offset(int x, int y) {
    return y * kPageWidth + x;
}

void writeWord(uint8 *ptr, int value) {
    *reinterpret_cast<int16 *>(ptr) = static_cast<int16>(value);
}

int readWord(const uint8 *ptr) {
    return *reinterpret_cast<const int16 *>(ptr);
}

void resetHudRasterState() {
    std::memset(g_frontPage, 0, sizeof(g_frontPage));
    std::memset(g_backPage, 0, sizeof(g_backPage));
    g_currentPageSeg = kSavedPageSeg;
    g_lastSetPage = 0;
    g_restoredSeg = 0;
    g_setPageCalls = 0;
    g_restoreSegCalls = 0;
    g_drawLineCalls = 0;
    g_drawLineX1 = 0;
    g_drawLineY1 = 0;
    g_drawLineX2 = 0;
    g_drawLineY2 = 0;
    g_lastSetColor = -1;
    g_setColorCalls = 0;
    g_lastBlitOffset = -1;
    g_setBlitOffsetCalls = 0;
    std::memset(g_lineCalls, 0, sizeof(g_lineCalls));
    g_glyphCalls = 0;
    std::memset(g_glyphSlotCalls, 0, sizeof(g_glyphSlotCalls));
    g_lastGlyphDesc = nullptr;
    g_lastGlyphString = nullptr;
    g_blitSpriteCalls = 0;
    g_lastSprite = nullptr;
    g_halfScaleRender = -1;
    gfxBufPtr = kFrontPageSeg;
    g_clipMaxX = kClipMaxX;
    g_clipMaxY = kClipMaxY;
    g_lineX1 = 0;
    g_lineY1 = 0;
    g_lineX2 = 0;
    g_lineY2 = 0;
    g_ourRoll = 0;
}

bool sawLine(uint16 x1, uint16 y1, uint16 x2, uint16 y2) {
    for (int i = 0; i < g_drawLineCalls && i < kMaxRecordedLines; ++i) {
        if (g_lineCalls[i].x1 == x1 && g_lineCalls[i].y1 == y1 &&
            g_lineCalls[i].x2 == x2 && g_lineCalls[i].y2 == y2) {
            return true;
        }
    }
    return false;
}

} // namespace

void FAR CDECL gfx_setPageN(uint16 pageNum) {
    ++g_setPageCalls;
    g_lastSetPage = pageNum;
    g_currentPageSeg = pageNum == kFrontPage ? kFrontPageSeg : kBackPageSeg;
}

int FAR CDECL gfx_getCurPageSeg(void) {
    return g_currentPageSeg;
}

uint8 *gfx_pagePixelsForSeg(uint16 seg, int *pitchOut) {
    if (pitchOut) *pitchOut = kPageWidth;
    if (seg == kFrontPageSeg) return g_frontPage;
    if (seg == kBackPageSeg) return g_backPage;
    return nullptr;
}

void FAR CDECL gfx_setCurPageSegReg(uint16 seg) {
    ++g_restoreSegCalls;
    g_restoredSeg = seg;
    g_currentPageSeg = seg;
}

void FAR CDECL gfx_drawLine(uint16 x1, uint16 y1, uint16 x2, uint16 y2) {
    ++g_drawLineCalls;
    if (g_drawLineCalls <= kMaxRecordedLines) {
        g_lineCalls[g_drawLineCalls - 1] = {x1, y1, x2, y2};
    }
    g_drawLineX1 = x1;
    g_drawLineY1 = y1;
    g_drawLineX2 = x2;
    g_drawLineY2 = y2;
}

void FAR CDECL gfx_setColor(int color) {
    ++g_setColorCalls;
    g_lastSetColor = color;
}

void FAR CDECL gfx_setBlitOffset(int offset) {
    ++g_setBlitOffsetCalls;
    g_lastBlitOffset = offset;
}

int FAR CDECL gfx_getDisplayPage(void) {
    return kGaugeDisplayPage;
}

int FAR CDECL gfx_blitSprite(struct SpriteParams *sprite) {
    ++g_blitSpriteCalls;
    g_lastSprite = sprite;
    return 0;
}

void FAR CDECL gfx_drawGlyphStr(int16 *desc, const char *str, int slot) {
    ++g_glyphCalls;
    if (slot >= 0 && slot < kGaugeGlyphSlotMax) {
        ++g_glyphSlotCalls[slot];
    }
    g_lastGlyphDesc = desc;
    g_lastGlyphString = str;
}

int FAR CDECL gfx_getPresetOffset1(void) {
    return kPresetOffset1;
}

int FAR CDECL gfx_getPresetOffset2(void) {
    return kPresetOffset2;
}

int main() {
    resetHudRasterState();
    int16 pageDesc[] = {kBackPage, 0, kFillColor};
    fillSpanRect(pageDesc, kFillLeft, kFillTop, kFillRight, kFillBottom);
    require(g_setPageCalls == kExpectedOneCall && g_lastSetPage == kBackPage,
            "fillSpanRect selects the original page descriptor page");
    require(g_restoreSegCalls == kExpectedOneCall && g_restoredSeg == kSavedPageSeg,
            "fillSpanRect restores the original current page segment");
    require(g_backPage[offset(kFillLeft, kFillTop)] == kFillColor &&
                g_backPage[offset(kFillRight, kFillBottom)] == kFillColor,
            "fillSpanRect fills the inclusive original rectangle bounds");
    require(g_backPage[offset(kFillLeft - 1, kFillTop)] == 0 &&
                g_backPage[offset(kFillRight + 1, kFillBottom)] == 0,
            "fillSpanRect leaves pixels outside the original rectangle untouched");

    resetHudRasterState();
    setupInstrumentLayoutFar();
    require(g_halfScaleRender == 0 &&
                g_tapeSprite0[0] == kFrontPageSeg &&
                g_tapeSprite1[0] == kFrontPageSeg &&
                g_tapeSprite2[0] == kFrontPageSeg &&
                g_tapeSprite3[0] == kFrontPageSeg,
            "setupInstrumentLayoutFar selects the active original half-scale layout and sprite source");
    require(g_tapeOriginX == kActiveTapeOriginX &&
                g_tapeCursorBackShift == kActiveTapeCursorBackShift &&
                g_tapeTickPitch == kActiveTapeTickPitch &&
                g_speedTapeTickStep == kActiveSpeedTapeTickStep &&
                g_altTapeTickStep == kActiveAltTapeTickStep,
            "setupInstrumentLayoutFar preserves original active speed/altitude tape scalars");
    require(g_headingBase == kActiveHeadingBase &&
                g_headingPixPerDeg == kActiveHeadingPixPerDeg &&
                g_compassWrapLimit == kActiveCompassWrapLimit,
            "setupInstrumentLayoutFar preserves original active compass scalars");
    require(static_cast<uint16>(g_pitchVtxX0) == kActivePitchVtxX0 &&
                g_pitchVtxX3 == kActivePitchVtxX3 &&
                g_pitchRungVStep == kActivePitchRungVStep &&
                g_pitchCenterY == kActivePitchCenterY &&
                g_pitchBlitOfs == kActivePitchBlitOfs &&
                g_pitchClipMaxX == kActivePitchClipMaxX &&
                g_pitchClipMaxY == kActivePitchClipMaxY,
            "setupInstrumentLayoutFar preserves original active pitch-ladder layout scalars");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_knots = kGaugeKnots;
    g_altitude = kGaugeAltitude;
    g_ourHead = kGaugeHeading;
    g_ourPitch = kGaugePitch;
    g_ourRoll = kGaugeRoll;
    g_hudDrawnFlag = 1;
    g_highGeeFlag[0] = 0;
    std::strcpy(g_geeStringBuf, "1.0G");
    drawInstrumentGaugesFar();
    require(g_tapeText0[0] == kGaugeDisplayPage &&
                g_tapeText1[0] == kGaugeDisplayPage &&
                g_tapeText2[0] == kGaugeDisplayPage &&
                g_tapeText3[0] == kGaugeDisplayPage &&
                g_tapeSprite0[3] == kGaugeDisplayPage &&
                g_tapeSprite1[3] == kGaugeDisplayPage,
            "drawInstrumentGaugesFar writes the original display page into text and sprite descriptors");
    require(g_glyphSlotCalls[kGaugeExpectedGlyphSlot1] > 0 &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot2] > 0 &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot3] > 0 &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot4] > 0 &&
                g_lastGlyphDesc == g_tapeText3 &&
                g_lastGlyphString == g_geeStringBuf,
            "drawInstrumentGaugesFar draws original tape, compass, wrapped compass, and G-meter glyph strings");
    require(g_setColorCalls > 0 &&
                g_drawLineCalls > 0 &&
                g_lastSetColor == kGaugeFrameColor,
            "drawInstrumentGaugesFar sets original gauge fill colours and emits gauge-frame lines");
    require(g_blitSpriteCalls >= 2 &&
                g_lastSprite == reinterpret_cast<struct SpriteParams *>(g_tapeSprite1),
            "drawInstrumentGaugesFar blits the original compass marker and HUD gunsight sprites");
    require(g_setBlitOffsetCalls >= 1 &&
                g_lastBlitOffset == 0,
            "drawInstrumentGaugesFar starts from the original zero blit offset for gauge-frame drawing");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_halfScaleRender = 1; /* The full-detail render branch is selected from this global at draw time. */
    g_knots = kGaugeKnots;
    g_altitude = kGaugeAltBelowThousand;
    g_ourHead = kGaugeNorthHeading;
    g_ourPitch = kGaugePitch;
    g_ourRoll = kGaugeRoll;
    g_compassWrapLimit = kGaugeNoCompassWrapLimit;
    g_hudDrawnFlag = 0;
    g_highGeeFlag[0] = 0;
    std::strcpy(g_geeStringBuf, "0.9G");
    drawInstrumentGaugesFar();
    require(g_glyphSlotCalls[kGaugeExpectedGlyphSlot1] > 0 &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot2] > 0 &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot3] == 0,
            "drawInstrumentGaugesFar preserves original sub-1000 altitude tape and no-wrap compass branch");
    require(g_blitSpriteCalls == 0,
            "drawInstrumentGaugesFar skips original compass-marker and gunsight sprites when both branches are disabled");
    require(g_lastSetColor == kGaugeFullScaleFrameColor &&
                sawLine(kGaugeFullScaleVerticalLineX, kGaugeFullScaleVerticalLineBottomY,
                        kGaugeFullScaleVerticalLineX, kGaugeFullScaleVerticalLineTopY),
            "drawInstrumentGaugesFar emits the original full-detail gauge-frame branch");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_knots = kGaugeKnots;
    g_altitude = kGaugeAltitude;
    g_ourHead = kGaugeHeading;
    g_ourPitch = kGaugePitchUp;
    g_ourRoll = kGaugeRollRight;
    g_hudDrawnFlag = 0;
    g_highGeeFlag[0] = 1;
    std::strcpy(g_geeStringBuf, "2.4G");
    drawInstrumentGaugesFar();
    require(g_glyphSlotCalls[kGaugePitchLabelSlot] > 0 &&
                g_lastSetColor == kGaugePitchLadderColor,
            "drawInstrumentGaugesFar draws original high-G pitch-ladder lines and labels");
    require(g_clipMaxX == kClipMaxX && g_clipMaxY == kClipMaxY &&
                g_lastBlitOffset == 0 && g_setBlitOffsetCalls >= 2,
            "drawInstrumentGaugesFar restores original clipping and blit offset after pitch-ladder drawing");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_knots = kGaugeKnots;
    g_altitude = kGaugeAltitude;
    g_ourHead = kGaugeHeading;
    g_ourPitch = kGaugePitchLabelOverflow;
    g_ourRoll = kGaugeRollRight;
    g_hudDrawnFlag = 0;
    g_highGeeFlag[0] = 1;
    std::strcpy(g_geeStringBuf, "3.1G");
    drawInstrumentGaugesFar();
    require(g_glyphSlotCalls[kGaugePitchLabelSlot] > 0 &&
                g_tapeDrawStr[0] == 0 &&
                g_lastGlyphString == reinterpret_cast<const char *>(g_tapeDrawStr),
            "drawInstrumentGaugesFar preserves original blank labels when pitch-ladder index exceeds the label table");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_knots = kGaugeHighKnots;
    g_altitude = kGaugeAltHundredsToThousandsJoin;
    g_ourHead = kGaugeHeading;
    g_ourPitch = kGaugePitchExtremeUp;
    g_ourRoll = kGaugeRoll;
    g_hudDrawnFlag = 0;
    g_highGeeFlag[0] = 0;
    std::strcpy(g_geeStringBuf, "1.1G");
    drawInstrumentGaugesFar();
    require(g_tapeChar == kGaugePositiveExtremeRungSegments &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot1] > 0,
            "drawInstrumentGaugesFar preserves original high-speed tape rollover and positive extreme pitch rungs");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_knots = kGaugeKnots;
    g_altitude = kGaugeAltExactlyOneThousand;
    g_ourHead = kGaugeHeading;
    g_ourPitch = kGaugePitchExtremeDown;
    g_ourRoll = kGaugeRoll;
    g_hudDrawnFlag = 0;
    g_highGeeFlag[0] = 0;
    std::strcpy(g_geeStringBuf, "1.2G");
    drawInstrumentGaugesFar();
    require(g_tapeChar == kGaugeNegativeExtremeRungSegments &&
                g_glyphSlotCalls[kGaugeExpectedGlyphSlot1] > 0,
            "drawInstrumentGaugesFar preserves original one-thousand-foot tape entry and negative extreme pitch rungs");

    resetHudRasterState();
    gfxBufPtr = kGaugeSpriteBuffer;
    setupInstrumentLayoutFar();
    g_halfScaleRender = 1; /* Selects the full-detail compass marker table during drawing. */
    g_knots = kGaugeKnots;
    g_altitude = kGaugeAltBelowHundred;
    g_ourHead = kGaugeHeading;
    g_ourPitch = kGaugePitch;
    g_ourRoll = kGaugeRoll;
    g_hudDrawnFlag = 0;
    g_highGeeFlag[0] = 0;
    std::strcpy(g_geeStringBuf, "0.8G");
    drawInstrumentGaugesFar();
    require(g_blitSpriteCalls == kExpectedOneCall &&
                g_lastSprite == reinterpret_cast<struct SpriteParams *>(g_tapeSprite0),
            "drawInstrumentGaugesFar preserves original below-100-foot tape rollover and full-detail compass marker blit");

    resetHudRasterState();
    g_currentPageSeg = kFrontPageSeg;
    hudComplex(kLadderStartBx, 0, 0, 0);
    require(g_frontPage[offset(kLadderBaseCol, kLadderTopRow)] == kLadderColor &&
                g_frontPage[offset(kLadderBaseCol - 1, kLadderTopRow)] == kLadderColor &&
                g_frontPage[offset(kLadderBaseCol - 2, kLadderTopRow)] == kLadderColor,
            "hudComplex draws the original thick first pitch-ladder mark");
    require(g_frontPage[offset(kLadderBaseCol - 3, kLadderTopRow)] == 0 &&
                g_frontPage[offset(kLadderBaseCol + 1, kLadderTopRow)] == 0,
            "hudComplex keeps the first pitch-ladder mark at the original width and direction");

    resetHudRasterState();
    g_currentPageSeg = kFrontPageSeg;
    hudComplex(kLadderAdjustedBx, 0, kLadderCxAdjust, 0);
    require(g_frontPage[offset(kLadderAdjustedBaseCol, kLadderAdjustedRow)] == kLadderColor,
            "hudComplex applies the original CX-based ladder column and geometry adjustment");

    resetHudRasterState();
    g_currentPageSeg = kFrontPageSeg;
    hudComplex(kLadderSkipBx, 0, 0, 0);
    require(g_frontPage[offset(kLadderBaseCol, kLadderSkippedRow)] == kLadderColor,
            "hudComplex skips back down to the original visible ladder band when BX starts below it");

    resetHudRasterState();
    g_lineX1 = kInsideX1;
    g_lineY1 = kInsideY1;
    g_lineX2 = kInsideX2;
    g_lineY2 = kInsideY2;
    drawClipLineGlobal();
    require(g_drawLineCalls == kExpectedOneCall &&
                g_drawLineX1 == kInsideX1 &&
                g_drawLineY1 == kInsideY1 &&
                g_drawLineX2 == kInsideX2 &&
                g_drawLineY2 == kInsideY2,
            "drawClipLineGlobal emits fully visible original HUD lines unchanged");

    resetHudRasterState();
    g_lineX1 = kClipLeftX1;
    g_lineY1 = kClipLeftY1;
    g_lineX2 = kClipLeftX2;
    g_lineY2 = kClipLeftY2;
    drawClipLineGlobal();
    require(g_drawLineCalls == kExpectedOneCall &&
                g_drawLineX1 == kClipLeftExpectedX1 &&
                g_drawLineY1 == kClipLeftExpectedY1 &&
                g_drawLineX2 == kClipLeftX2 &&
                g_drawLineY2 == kClipLeftY2,
            "drawClipLineGlobal clips the original left-edge crossing with integer interpolation");

    resetHudRasterState();
    g_lineX1 = kInsideX1;
    g_lineY1 = kInsideY1;
    g_lineX2 = kClipRightX2;
    g_lineY2 = kInsideY2;
    drawClipLineGlobal();
    require(g_drawLineCalls == kExpectedOneCall &&
                g_drawLineX1 == kInsideX1 &&
                g_drawLineY1 == kInsideY1 &&
                g_drawLineX2 == kClipRightExpectedX2,
            "drawClipLineGlobal clips the original right-edge crossing with integer interpolation");

    resetHudRasterState();
    g_lineX1 = kInsideX1;
    g_lineY1 = kInsideY1;
    g_lineX2 = kInsideX2;
    g_lineY2 = kClipBottomY2;
    drawClipLineGlobal();
    require(g_drawLineCalls == kExpectedOneCall &&
                g_drawLineX2 == kClipBottomExpectedX2 &&
                g_drawLineY2 == kClipBottomExpectedY2,
            "drawClipLineGlobal clips the original bottom-edge crossing with integer interpolation");

    resetHudRasterState();
    g_lineX1 = kInsideX1;
    g_lineY1 = kClipTopY1;
    g_lineX2 = kInsideX2;
    g_lineY2 = kInsideY2;
    drawClipLineGlobal();
    require(g_drawLineCalls == kExpectedOneCall &&
                g_drawLineX1 == kClipTopExpectedX1 &&
                g_drawLineY1 == kClipTopExpectedY1,
            "drawClipLineGlobal clips the original top-edge crossing with integer interpolation");

    resetHudRasterState();
    g_lineX1 = kInsideX1;
    g_lineY1 = kRejectAboveY;
    g_lineX2 = kInsideX2;
    g_lineY2 = kRejectAboveY;
    drawClipLineGlobal();
    require(g_drawLineCalls == 0,
            "drawClipLineGlobal rejects original same-side invisible lines without drawing");

    resetHudRasterState();
    writeWord(g_compassTapeBuf + kCompassPitchXBase + kRotateVertexOffset, kRotateInputX);
    writeWord(g_compassTapeBuf + kCompassPitchYBase + kRotateVertexOffset, kRotateInputY);
    hudRotateLadder(kRotateVertexOffset);
    require(readWord(g_compassTapeBuf + kCompassPitchXBase + kRotateVertexOffset) ==
                    kRotateZeroRollExpectedX &&
                readWord(g_compassTapeBuf + kCompassPitchYBase + kRotateVertexOffset) ==
                    kRotateZeroRollExpectedY,
            "hudRotateLadder preserves the original zero-roll high-word rotation math");

    std::cout << "hud_raster_behavior_tests passed\n";
    return 0;
}
