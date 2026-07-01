#include "gfx.h"
#include "gfx_impl.h"
#include "inttype.h"
#include "struct.h"

#include <SDL3/SDL.h>

#include <cstdarg>
#include <cstdlib>
#include <iostream>
#include "posix_test_compat.h"

void FAR CDECL gfx_setCurPageSegReg(uint16 seg);
void FAR CDECL gfx_getCurPage(int page);
void FAR CDECL gfx_fillRow(uint16 rowOffset, uint16 srcBuf, uint16 rowNum);
void FAR CDECL gfx_copyRow(uint16 rowOffset);
void FAR CDECL gfx_setPageBuf(void);
void FAR CDECL gfx_storePageSeg(void);
void FAR CDECL gfx_setPageSeg(void);
void FAR CDECL gfx_plotPixel(void);
void FAR CDECL gfx_drawGlyphStr(int16 *desc, const char *str, int slot);
void clearRect(int16 *pageNum, int x1, int y1, int x2, int y2);
void drawLineWrapper(void);

int16 lineX1 = 0;
int16 lineY1 = 0;
int16 lineX2 = 0;
int16 lineY2 = 0;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum GfxImplBehaviorConstant : int {
    kVisibleVgaSegment = 0xa000,
    kPage0 = 0,
    kPage1 = 1,
    kPage2 = 2,
    kPage1SegmentToken = 0xc001,
    kPage2SegmentToken = 0xc002,
    kInvalidPage = 16,
    kInvalidSegmentToken = 0xd123,
    kLogicalWidth = 320,
    kLogicalHeight = 200,
    kRowFixture = 7,
    kColumnFixture = 13,
    kExpectedRowOffset = kLogicalWidth * kRowFixture,
    kExpectedPixelOffset = kExpectedRowOffset + kColumnFixture,
    kPageBufferSize = 0xfa00,
    kAuxBufferSize = 0x1950,
    kPresetOffset1 = 0x5580,
    kPresetOffset2 = 0x1950,
    kConstOne = 1,
    kFirstSpriteHandle = 1,
    kPixelFixture = 0x7f,
    kSecondPixelFixture = 0x33,
    kFillColor = 0x2c,
    kReplacementColor = 0x09,
    kLineColor = 0x0e,
    kGlyphColor = 0x05,
    kSpriteColor = 0x44,
    kCoreBlitColor = 0x55,
    kBlitOffsetFixture = 0x1234,
    kCustomCurPageSegment = 0xbeef,
    kDacAnimCount = 3,
    kMaxSpriteBuffers = 8,
    kFont0Advance = 4,
    kFont1CapitalAAdvance = 8,
    kInlineColorEscape = 0x80,
    kUnknownFontAdvance = 8,
    kSpanRows = 220,
    kDirtyMaxOffsetBytes = 0x1b8,
    kModeCodeMcga = 3,
    kFillRowNearPtr = 0x4000,
    kFillRowPatternBias = 0x21,
    kFireCycleSnapDelayUs = 400000,
    kExpectedChildExitOk = 0,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void expectAllocPageBootstrapInChild() {
    pid_t pid = fork();
    require(pid >= 0, "fork should be available for gfx first-use bootstrap test");
    if (pid == 0) {
        gfx_initState();
        const int seg = gfx_allocPage(kPage1);
        const bool ok = seg == kPage1SegmentToken &&
                        gfx_getPageSeg(kPage0) == kVisibleVgaSegment;
        std::exit(ok ? kExpectedChildExitOk : kTestFailureExitCode);
    }
    int status = 0;
    require(waitpid(pid, &status, 0) == pid, "gfx first-use bootstrap child should exit");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kExpectedChildExitOk,
            "gfx_allocPage bootstraps the original MCGA page table on first native allocation");
}

} // namespace

void log_critical(const char *, ...) {
    std::cerr << "failed: gfx_impl called log_critical\n";
    std::exit(kTestFailureExitCode);
}

void log_info(const char *, ...) {}
void log_message(const char *, ...) {}
void log_verbose(const char *, ...) {}
void log_debug(const char *, ...) {}
void log_warn(const char *, ...) {}
void log_error(const char *, ...) {}
void log_set_app(const char *) {}

int main() {
    gfx_initState();

    require(gfx_getModeFlag() == 1 && gfx_getModeFlag2() == 1,
            "gfx_initState preserves the original mode-flag default");
    require(gfx_getDisplayPage() == kPage1,
            "gfx_initState preserves page 1 as the display/back buffer");
    expectAllocPageBootstrapInChild();
    gfx_setPageN(kPage1);
    require(gfx_curPage() == kPage1 &&
                gfx_getPageSeg(kPage0) == kVisibleVgaSegment,
            "gfx_setPageN bootstraps the original MCGA page table before explicit allocation");

    const int page1Seg = gfx_allocPage(kPage1);
    require(page1Seg == kPage1SegmentToken,
            "gfx_allocPage returns stable page-index segment tokens");
    require(gfx_getPageSurface(kPage1) != nullptr,
            "gfx_allocPage creates a 320x200 indexed page surface");

    int pitch = 0;
    uint8 *page1Pixels = gfx_pagePixelsForSeg(
        static_cast<uint16>(page1Seg), &pitch);
    require(page1Pixels != nullptr && pitch >= kLogicalWidth,
            "page segment tokens map back to writable page pixels");

    page1Pixels[0] = kPixelFixture;
    gfx_clearPage(static_cast<uint16>(page1Seg));
    require(page1Pixels[0] == 0,
            "gfx_clearPage clears the page named by the legacy segment token");

    const int page0Seg = gfx_allocPage(kPage0);
    require(page0Seg == 0xc000,
            "gfx_allocPage still hands out the page 0 allocation token");
    require(gfx_getPageSeg(kPage0) == kVisibleVgaSegment,
            "page 0 remains the visible VGA framebuffer segment");

    const int page2Seg = gfx_allocPage(kPage2);
    require(page2Seg == kPage2SegmentToken,
            "gfx_allocPage assigns the expected token for page 2");
    gfx_setPageN(kPage2);
    require(gfx_curPage() == kPage2 &&
                gfx_getCurPageSeg() == kPage2SegmentToken,
            "gfx_setPageN selects the current page and segment token");
    gfx_initOverlay();
    require(gfx_curPage() == kPage1 &&
                gfx_getCurPageSeg() == kPage1SegmentToken,
            "gfx_initOverlay restores page 1 as the current draw page");

    gfx_setCurPageSeg(kCustomCurPageSegment);
    require(gfx_getCurPageSeg() == kCustomCurPageSegment,
            "gfx_setCurPageSeg stores the current page segment token");
    gfx_setCurPageSegReg(kPage1SegmentToken);
    require(gfx_getCurPageSeg() == kPage1SegmentToken,
            "gfx_setCurPageSegReg restores the current page segment token");
    require(gfx_setPage1(kPage2) == kPage2SegmentToken &&
                gfx_curPage() == kPage2,
            "gfx_setPage1 selects a page by index despite the legacy name");
    gfx_getCurPage(kPage0);
    require(gfx_curPage() == kPage2,
            "gfx_getCurPage is the original inert slot stub");
    gfx_initOverlay();

    require(gfx_getRowOffset(kRowFixture) == kExpectedRowOffset,
            "gfx_getRowOffset uses original 320-byte row stride");
    require(gfx_getRowOffset(kLogicalHeight) ==
                static_cast<int>(static_cast<uint16>(kLogicalHeight) * kLogicalWidth),
            "gfx_getRowOffset preserves original uint16 row fallback outside the table");
    require(gfx_getModecode() == kModeCodeMcga,
            "gfx_getModecode returns the original MCGA mode code");
    require(gfx_getCurPageSurface() != nullptr,
            "gfx_getCurPageSurface returns the current page backing surface");
    require(gfx_getPageSurface(kInvalidPage) == nullptr &&
                gfx_pagePixelsForSeg(kInvalidSegmentToken, nullptr) == nullptr,
            "invalid native page and segment lookups preserve original no-op behavior");
    require(gfx_calcRowAddr(kColumnFixture, kRowFixture) ==
                kExpectedPixelOffset,
            "gfx_calcRowAddr keeps original column-first argument order");
    require(gfx_getBufSize() == kPageBufferSize,
            "gfx_getBufSize returns the original 64000-byte page size");
    require(gfx_getAuxBufSize() == kAuxBufferSize,
            "gfx_getAuxBufSize returns the original auxiliary buffer size");
    require(gfx_getPresetOffset1() == kPresetOffset1 &&
                gfx_getPresetOffset2() == kPresetOffset2,
            "preset offset slots return the original baked constants");
    require(gfx_getFreeMem() == 0,
            "native gfx_getFreeMem keeps the no-DOS-memory stub value");
    require(gfx_getVal() == 0 && gfx_getVal2() == 0,
            "native overlay value getters keep the START sprite-load value");
    require(gfx_getConst1() == kConstOne,
            "gfx_getConst1 preserves the baked overlay constant");

    uint8 decodedRow[kLogicalWidth] = {};
    for (int col = 0; col < kLogicalWidth; ++col) {
        decodedRow[col] = static_cast<uint8>(col + kFillRowPatternBias);
    }
    gfx_setPageN(kPage1);
    gfx_setNearReadBuffer(kFillRowNearPtr, decodedRow, sizeof(decodedRow));
    gfx_fillRow(kExpectedRowOffset, kFillRowNearPtr, kRowFixture);
    gfx_clearNearReadBuffer();
    require(page1Pixels[kRowFixture * pitch] == decodedRow[0] &&
                page1Pixels[kRowFixture * pitch + kColumnFixture] ==
                    decodedRow[kColumnFixture],
            "gfx_fillRow resolves caller-DS near row data and copies one MCGA row");

    gfx_setBlitOffset(kBlitOffsetFixture);
    require(gfx_getBlitOffset() == kBlitOffsetFixture,
            "gfx_setBlitOffset stores the live blit offset");
    gfx_nop22();
    require(gfx_getBlitOffset() == kBlitOffsetFixture,
            "gfx_nop22 preserves the live blit offset");
    gfx_setBlitOffset2();
    require(gfx_getBlitOffset() == 0,
            "gfx_setBlitOffset2 clears the live blit offset");
    gfx_setBlitOffset(kBlitOffsetFixture);
    gfx_setBlitOffset3();
    require(gfx_getBlitOffset() == 0,
            "gfx_setBlitOffset3 clears the live blit offset");
    gfx_setBlitOffset(kBlitOffsetFixture);
    gfx_setBlitOffsetReg();
    require(gfx_getBlitOffset() == kBlitOffsetFixture,
            "register blit-offset stub is inert in the native build");
    gfx_setBlitOffset(0);

    int16 pageDesc[4] = {};
    pageDesc[0] = kPage1;
    pageDesc[3] = kFillColor;
    clearRect(pageDesc, 0, 0, 1, 1);
    require(page1Pixels[0] == kFillColor &&
                page1Pixels[1] == kFillColor &&
                page1Pixels[pitch] == kFillColor &&
                page1Pixels[pitch + 1] == kFillColor,
            "clearRect fills the requested page rectangle with descriptor color");
    gfx_switchColor(pageDesc, 0, 0, 1, 1, kFillColor, kReplacementColor);
    require(page1Pixels[0] == kReplacementColor &&
                page1Pixels[pitch + 1] == kReplacementColor,
            "gfx_switchColor replaces matching colors in the rectangle");

    uint8 *page2Pixels = gfx_pagePixelsForSeg(kPage2SegmentToken, nullptr);
    require(page2Pixels != nullptr,
            "page 2 segment token maps to writable page pixels");
    page1Pixels[0] = kPixelFixture;
    gfx_copyRect(kPage1, 0, 0, kPage2, 2, 3, 1, 1);
    require(page2Pixels[3 * pitch + 2] == kPixelFixture,
            "gfx_copyRect copies pixels between page surfaces");
    gfx_setPageN(kPage2);
    page1Pixels[0] = kSecondPixelFixture;
    gfx_blitToCurrent(static_cast<int16>(page1Seg));
    require(page2Pixels[0] == kSecondPixelFixture,
            "gfx_blitToCurrent copies a whole source page to the current page");
    gfx_dacAnimate();
    uint8 *page0Pixels = gfx_pagePixelsForSeg(kVisibleVgaSegment, nullptr);
    require(page0Pixels != nullptr && page0Pixels[0] == kSecondPixelFixture,
            "gfx_dacAnimate copies the current composited page to visible VGA page");
    usleep(kFireCycleSnapDelayUs);
    gfx_dacAnimate();
    require(gfx_getDisplayPage() == kPage1,
            "gfx_dacAnimate snaps the fixed fire-cycle clock forward after a long pause");
    page0Pixels[0] = kPixelFixture;
    gfx_clearVga();
    require(page0Pixels[0] == 0,
            "gfx_clearVga clears the physical VGA framebuffer page");

    gfx_setPageN(kPage1);
    gfx_setDrawColor(kFillColor);
    alignas(int16) unsigned char spans[kDirtyMaxOffsetBytes + kSpanRows * sizeof(int16)] = {};
    auto *spanMin = reinterpret_cast<int16 *>(spans);
    auto *spanMax = reinterpret_cast<int16 *>(spans + kDirtyMaxOffsetBytes);
    spanMin[kRowFixture] = kColumnFixture;
    spanMax[kRowFixture] = kColumnFixture + 1;
    gfx_dirtyRect2(spanMin, kRowFixture, kRowFixture);
    require(page1Pixels[kExpectedPixelOffset] == kFillColor &&
                page1Pixels[kExpectedPixelOffset + 1] == kFillColor,
            "gfx_dirtyRect2 fills the original dirty span interval");
    gfx_setColor(kReplacementColor);
    gfx_dirtyRect(spanMin, kRowFixture, kRowFixture);
    require(page1Pixels[kExpectedPixelOffset] == kReplacementColor,
            "gfx_dirtyRect delegates to dirtyRect2 with the live fill color");

    require(gfx_setFont('A', 0) == kFont0Advance &&
                gfx_setFont('A', 1) == kFont1CapitalAAdvance &&
                gfx_setFont(kInlineColorEscape, 1) == 0 &&
                gfx_setFont('A', 7) == kUnknownFontAdvance,
            "gfx_setFont returns original advance widths and inline-color width");

    int16 textParams[11] = {};
    textParams[0] = kPage1;
    textParams[2] = kGlyphColor;
    textParams[4] = 4;
    textParams[5] = 4;
    textParams[6] = 0;
    gfx_drawString(textParams, "A");
    require(textParams[4] == 4 + kFont0Advance,
            "gfx_drawString advances x by the original font width");
    char inlineColorText[] = {
        static_cast<char>(kInlineColorEscape | kReplacementColor),
        'A',
        0,
    };
    textParams[4] = 4;
    gfx_drawString(textParams, inlineColorText);
    require(textParams[4] == 4 + kFont0Advance,
            "gfx_drawString preserves original inline color escape without drawing a glyph");
    textParams[4] = 8;
    textParams[5] = 8;
    textParams[7] = 0;
    textParams[8] = 199;
    textParams[9] = 0;
    textParams[10] = 319;
    gfx_fillDirty(textParams, "B");
    gfx_blitTransparent(textParams, "C");
    gfx_blitVariant(textParams, "D");
    gfx_copyBlock(textParams, "E");
    gfx_drawStringUnclipped(textParams, "F");
    gfx_drawGlyphStr(textParams, "G", 0x01);
    gfx_drawGlyphStr(textParams, "H", 0x02);
    gfx_drawGlyphStr(textParams, "I", 0x03);
    gfx_drawGlyphStr(textParams, "J", 0x04);
    gfx_drawGlyphStr(textParams, "K", 0x06);

    gfx_setPageN(kPage1);
    gfx_setDrawColor(kLineColor);
    gfx_drawLine(0, 0, 2, 0);
    require(page1Pixels[0] == kLineColor &&
                page1Pixels[1] == kLineColor &&
                page1Pixels[2] == kLineColor,
            "gfx_drawLine draws a clipped line on the current page");
    gfx_drawLine(static_cast<uint16>(static_cast<int16>(-4)), 2, 4, 2);
    gfx_drawLine(318, 3, 324, 3);
    gfx_drawLine(5, static_cast<uint16>(static_cast<int16>(-4)), 5, 4);
    gfx_drawLine(6, 198, 6, 204);
    gfx_drawLine(static_cast<uint16>(static_cast<int16>(-4)),
                 static_cast<uint16>(static_cast<int16>(-4)),
                 static_cast<uint16>(static_cast<int16>(-1)),
                 static_cast<uint16>(static_cast<int16>(-1)));
    require(page1Pixels[2 * pitch] == kLineColor &&
                page1Pixels[3 * pitch + 319] == kLineColor &&
                page1Pixels[5] == kLineColor &&
                page1Pixels[199 * pitch + 6] == kLineColor,
            "gfx_drawLine clips original off-screen endpoints to all page boundaries");
    lineX1 = 0;
    lineY1 = 1;
    lineX2 = 2;
    lineY2 = 1;
    drawLineWrapper();
    require(page1Pixels[pitch] == kLineColor &&
                page1Pixels[pitch + 2] == kLineColor,
            "drawLineWrapper marshals the legacy line globals");

    gfx_storeBufPtr(kPage1SegmentToken, kPage1);
    require(gfx_getPageSeg(kPage1) == kPage1SegmentToken,
            "gfx_storeBufPtr stores a page segment token");

    page1Pixels[10] = kSpriteColor;
    page2Pixels[20] = 0;
    SpriteParams sprite = {};
    sprite.bufPtr = static_cast<int16>(page1Seg);
    sprite.srcX = 10;
    sprite.srcY = 0;
    sprite.page = kPage2;
    sprite.dstX = 20;
    sprite.dstY = 0;
    sprite.width = 1;
    sprite.height = 1;
    require(gfx_blitSprite(&sprite) == 0 &&
                page2Pixels[20] == kSpriteColor,
            "gfx_blitSprite copies nonzero sprite pixels transparently");
    page2Pixels[21] = 0;
    sprite.dstX = 21;
    gfx_blitSpriteClipped(reinterpret_cast<int16 *>(&sprite));
    require(page2Pixels[21] == kSpriteColor,
            "gfx_blitSpriteClipped delegates to the sprite core");
    page2Pixels[22] = 0;
    sprite.dstX = 22;
    gfx_blitSpriteOpaque(reinterpret_cast<int16 *>(&sprite));
    require(page2Pixels[22] == kSpriteColor,
            "gfx_blitSpriteOpaque delegates to the sprite core in this build");
    gfx_blitSpriteClipped2();
    gfx_blitSpriteOpaque2();

    int16 coreBlk[8] = {};
    page1Pixels[11] = kCoreBlitColor;
    page2Pixels[23] = 0;
    coreBlk[0] = static_cast<int16>(page1Seg);
    coreBlk[1] = 11;
    coreBlk[2] = 0;
    coreBlk[3] = kPage2;
    coreBlk[4] = 23;
    coreBlk[5] = 0;
    coreBlk[6] = 1;
    coreBlk[7] = 1;
    gfx_blitCore(coreBlk);
    require(page2Pixels[23] == kCoreBlitColor,
            "gfx_blitCore skips zero pixels and copies nonzero pixels");

    gfx_complexRender(90, 0, 0, 0);
    gfx_complexRender(90, 1, 1, 0);

    uint8 dacTriples[6] = {
        0x3f, 0x00, 0x00,
        0x00, 0x3f, 0x00,
    };
    gfx_setDacRange(0, 2, dacTriples);
    gfx_setDacRange(0, 0, dacTriples);
    gfx_setDac(0);
    gfx_setDac(5);
    gfx_setDacAnimCount(kDacAnimCount);
    gfx_dacCycle();
    gfx_setFadeSteps(kDacAnimCount);
    require(gfx_getHiResSurface() != nullptr,
            "gfx_getHiResSurface lazily creates the hi-res title surface");
    gfx_presentHiRes();
    gfx_flipPage();
    require(!video_setHiRes(),
            "video_setHiRes reports failure without an initialized renderer");

    // Use SDL's dummy video backend so the native window lifecycle can be
    // exercised in headless test runs.
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    gfx_videoInit();
    gfx_toggleFullscreen();
    gfx_toggleFullscreen();
    gfx_setDacAnimCount(0);
    gfx_flipPage();
    require(video_setHiRes(),
            "video_setHiRes succeeds with an initialized dummy renderer");
    gfx_flipPage();
    gfx_setMode13();
    require(gfx_getDisplayPage() == kPage1 &&
                gfx_getCurPageSeg() == kPage1SegmentToken,
            "gfx_getDisplayPage resyncs the original current page segment after MCGA mode is active");
    gfx_setDacAnimCount(kDacAnimCount);
    gfx_dacCycle();
    gfx_flipPage();

    gfx_copyRow(0);
    gfx_fillRow2(0, 0);
    gfx_nop15();
    gfx_nop16();
    gfx_nop23();
    gfx_nop36();
    gfx_nop37();
    gfx_nop51();
    gfx_clipRight();
    gfx_clipTop();
    gfx_clipLeft();
    gfx_clipBottom();
    gfx_spriteVariant1();
    gfx_spriteVariant2();
    gfx_setOvlVal1(kPixelFixture);
    gfx_setOvlVal2(kSecondPixelFixture);
    gfx_plotPixel();
    gfx_storePageSeg();
    gfx_setPageSeg();
    gfx_setPageBuf();
    gfx_commitPage();

    int spriteHandles[kMaxSpriteBuffers] = {};
    const int spriteHandle = gfx_allocSpriteBuf();
    require(spriteHandle == kFirstSpriteHandle,
            "first native sprite buffer uses handle 1");
    require(gfx_getSpriteSurface(spriteHandle) != nullptr,
            "sprite buffer handles map to indexed SDL surfaces");
    gfx_freeSpriteBuf(spriteHandle);
    require(gfx_getSpriteSurface(spriteHandle) == nullptr,
            "gfx_freeSpriteBuf releases the sprite surface handle");
    for (int idx = 0; idx < kMaxSpriteBuffers; ++idx) {
        spriteHandles[idx] = gfx_allocSpriteBuf();
        require(spriteHandles[idx] == idx + 1,
                "native sprite buffer handles are allocated in original slot order");
    }
    require(gfx_allocSpriteBuf() == 0,
            "gfx_allocSpriteBuf preserves original failure when all sprite buffers are occupied");
    for (int idx = 0; idx < kMaxSpriteBuffers; ++idx) {
        gfx_freeSpriteBuf(spriteHandles[idx]);
    }

    gfx_videoShutdown();
    std::cout << "gfx_impl behavior tests passed\n";
    return 0;
}
