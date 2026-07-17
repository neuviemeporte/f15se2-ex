// LINK_CORE + headless. Exercises the real 2D render engine (gfx_impl.c + the
// r2d software backend) against the current API. Everything here is observable
// by reading page pixels back after a draw — no draw-capture spy hook is needed.
//
// gfx_videoInit() brings up SDL's dummy video driver + a software renderer and
// registers the software rasterization hooks (gfx_swLine/gfx_swImage), so the
// full gfx_drawLine / gfx_blitSprite paths rasterize into the page surface and
// are read back here. Verified CI-safe headless (no display/GPU).
#include "gfx.h"
#include "gfx_impl.h"
#include "struct.h"
#include "headless.h"
#include "golden.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

// clearRect is declared per-program (endcode.h/stcode.h) with differing arg
// names; declare it locally to avoid pulling in a program-specific header. Word
// 0 of the pageDesc is the page index, word 3 the fill colour.
void clearRect(int16 *pageDesc, int16 x1, int16 y1, int16 x2, int16 y2);
int loadReplacementPngToPage(const char *filename, int page);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum GfxRenderConstant : int {
    kLogicalWidth = 320,
    kLogicalHeight = 200,
    // gfx_impl.c MAX_SPRITE_BUFS: the fixed pool of sprite-sheet handles.
    kMaxSpriteBufs = 8,
    kNoHandle = 0,
    // gfx_blitSprite is unconditionally transparent on palette index 0.
    kTransparentIndex = 0,
    // Font 0 is the fixed-pitch (advance 4) in-flight HUD font; every glyph.
    kFont0Advance = 4,
    // gfx_setFont fallback advance for a NULL/absent table or a control char.
    kFontFallbackAdvance = 8,
    kReplacementFontAdvance = 11,
    // A char >= 0x80 is an inline colour escape: no glyph, zero advance.
    kColorEscapeChar = 0x90,
    kFirstPrintable = 0x20,
    // gfx_dirtyRect2 reads the max-span buffer 0x1b8 bytes past the min buffer.
    kDirtySpanMaxOffsetBytes = 0x1b8,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

uint8 pagePixel(int page, int x, int y) {
    int pitch = 0;
    uint8 *px = gfx_pagePixels(page, &pitch);
    return px[(size_t)y * pitch + x];
}

// Fill a rectangular region of a page with a solid colour by writing the
// surface directly, independent of the gfx clear/fill primitives under test.
void fillPageRaw(int page, uint8 color) {
    int pitch = 0;
    uint8 *px = gfx_pagePixels(page, &pitch);
    for (int y = 0; y < kLogicalHeight; y++)
        std::memset(px + (size_t)y * pitch, color, kLogicalWidth);
}

void writeReplacementBdf(const std::filesystem::path &root, int fontId) {
    const auto fontPath = root / "converted_assets_all" / "fonts" / ("font_" + std::to_string(fontId) + ".bdf");
    std::filesystem::create_directories(fontPath.parent_path());
    std::ofstream out(fontPath);
    out << "STARTFONT 2.1\n"
           "FONT replacement\n"
           "SIZE 5 75 75\n"
           "FONTBOUNDINGBOX 4 5 0 0\n"
           "STARTPROPERTIES 2\n"
           "FONT_ASCENT 5\n"
           "FONT_DESCENT 0\n"
           "ENDPROPERTIES\n"
           "CHARS 99\n";
    for (int ch = 0x20; ch < 0x80; ch++) {
        out << "STARTCHAR U+" << std::hex << ch << std::dec << "\n"
            << "ENCODING " << ch << "\n"
            << "SWIDTH 500 0\n"
            << "DWIDTH " << kReplacementFontAdvance << " 0\n"
            << "BBX 4 5 0 0\n"
            << "BITMAP\n"
            << "F0\n90\nF0\n90\nF0\n"
            << "ENDCHAR\n";
    }
    out << "STARTCHAR U+041F\n"
        << "ENCODING 1055\n"
        << "SWIDTH 500 0\n"
        << "DWIDTH " << kReplacementFontAdvance << " 0\n"
        << "BBX 4 5 0 0\n"
        << "BITMAP\n"
        << "F0\n90\n90\n90\n90\n"
        << "ENDCHAR\n";
    out << "STARTCHAR U+00C4\n"
        << "ENCODING 196\n"
        << "SWIDTH 500 0\n"
        << "DWIDTH " << kReplacementFontAdvance << " 0\n"
        << "BBX 4 5 0 0\n"
        << "BITMAP\n"
        << "90\nF0\n90\nF0\n90\n"
        << "ENDCHAR\n";
    out << "STARTCHAR U+0141\n"
        << "ENCODING 321\n"
        << "SWIDTH 500 0\n"
        << "DWIDTH " << kReplacementFontAdvance << " 0\n"
        << "BBX 4 5 0 0\n"
        << "BITMAP\n"
        << "80\nA0\nC0\nA0\nF0\n"
        << "ENDCHAR\n";
    out << "ENDFONT\n";
}

void writeReplacementPng(const std::filesystem::path &root) {
    static const unsigned char kRedPng[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,2,8,6,0,0,0,114,182,13,36,0,0,0,17,73,68,65,84,120,156,99,248,207,192,240,31,132,25,96,12,0,71,202,7,249,103,89,110,183,0,0,0,0,73,69,78,68,174,66,96,130
    };
    const auto pngPath = root / "converted_assets_all" / "TITLE.png";
    std::filesystem::create_directories(pngPath.parent_path());
    std::ofstream out(pngPath, std::ios::binary);
    out.write(reinterpret_cast<const char *>(kRedPng), sizeof(kRedPng));
}

void writeReplacementFontPng(const std::filesystem::path &root, int fontId) {
    static const unsigned char kFontAtlasPng[] = {
        137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,111,
        0,0,0,41,8,6,0,0,0,59,196,119,17,0,0,0,134,73,68,65,
        84,120,218,237,215,49,14,0,16,20,68,65,247,191,52,10,18,17,250,191,
        201,108,233,117,10,49,173,207,181,207,180,226,173,175,105,129,109,31,190,162,
        86,188,157,7,119,212,138,55,23,17,220,60,67,217,205,82,231,11,142,10,
        26,42,104,168,128,10,26,42,104,168,96,190,217,168,160,161,130,134,10,168,
        224,34,80,65,67,5,67,5,84,208,80,65,67,5,13,21,80,65,67,5,
        67,5,84,208,80,65,67,5,13,21,80,65,67,5,67,5,205,23,28,21,
        52,84,208,80,1,21,60,81,17,109,0,253,218,112,113,4,124,59,36,0,
        0,0,0,73,69,78,68,174,66,96,130
    };
    const auto pngPath = root / "converted_assets_all" / "fonts" / ("font_" + std::to_string(fontId) + ".png");
    std::filesystem::create_directories(pngPath.parent_path());
    std::ofstream out(pngPath, std::ios::binary);
    out.write(reinterpret_cast<const char *>(kFontAtlasPng), sizeof(kFontAtlasPng));
}

void writeInvalidReplacementBdf(const std::filesystem::path &root, int fontId) {
    const auto fontPath = root / "converted_assets_all" / "fonts" / ("font_" + std::to_string(fontId) + ".bdf");
    std::filesystem::create_directories(fontPath.parent_path());
    std::ofstream out(fontPath);
    out << "STARTFONT 2.1\nFONT invalid\nCHARS 0\nENDFONT\n";
}

// ---- gfx_pagePixels + page-1 aliasing -------------------------------------
void test_pagePixels_and_alias() {
    int pitch0 = 0, pitch1 = 0, pitch2 = 0;
    uint8 *p0 = gfx_pagePixels(0, &pitch0);
    uint8 *p1 = gfx_pagePixels(1, &pitch1);
    uint8 *p2 = gfx_pagePixels(2, &pitch2);
    require(p0 && p1 && p2, "gfx_pagePixels returns a backing buffer for each page");
    require(pitch0 >= kLogicalWidth, "page pitch spans at least the logical width");
    // Pages 0 (front) and 1 (back) alias the single native back buffer.
    require(p1 == p0, "page 1 aliases page 0 (single native back buffer)");
    require(p2 != p0, "page 2 is a distinct surface");
}

// ---- clearRect fill + clamping --------------------------------------------
void test_clearRect() {
    int16 pd[4] = {0, 0, 0, 0};
    pd[0] = 2;   /* target page */
    pd[3] = 0x21; /* fill colour (clearRect reads word 3) */
    fillPageRaw(2, 0);
    clearRect(pd, 10, 20, 15, 25);
    require(pagePixel(2, 10, 20) == 0x21 && pagePixel(2, 15, 25) == 0x21,
            "clearRect fills the inclusive rectangle with pageDesc[3]");
    require(pagePixel(2, 9, 20) == 0 && pagePixel(2, 16, 25) == 0,
            "clearRect leaves pixels outside the rectangle untouched");
    // Out-of-range x2/y2 clamp to the surface; a right/bottom-edge fill must not
    // crash or spill into the next row.
    clearRect(pd, 315, 195, 400, 400);
    require(pagePixel(2, 319, 199) == 0x21, "clearRect clamps x2/y2 to the surface");
}

// ---- gfx_switchColor selective recolour -----------------------------------
void test_switchColor() {
    int16 pageDesc = 2;
    fillPageRaw(2, 5);
    // Paint a lone pixel a different colour so we can prove only `oldColor`
    // pixels within the rect change.
    int pitch = 0;
    uint8 *px = gfx_pagePixels(2, &pitch);
    px[(size_t)30 * pitch + 30] = 9;
    gfx_switchColor(&pageDesc, 20, 20, 40, 40, /*old*/ 5, /*new*/ 12);
    require(pagePixel(2, 30, 31) == 12, "gfx_switchColor replaces oldColor inside the rect");
    require(pagePixel(2, 30, 30) == 9, "gfx_switchColor leaves non-matching colours alone");
    require(pagePixel(2, 10, 10) == 5, "gfx_switchColor leaves pixels outside the rect alone");
}

// ---- gfx_copyRect between pages -------------------------------------------
void test_copyRect() {
    fillPageRaw(2, 0);
    fillPageRaw(0, 0);
    int16 srcDesc = 2;
    // Stamp a 3x3 block of colour 7 on page 2.
    gfx_switchColor(&srcDesc, 0, 0, 2, 2, 0, 7);
    require(pagePixel(2, 1, 1) == 7, "fixture block present on source page");
    gfx_copyRect(/*src*/ 2, 0, 0, /*dst*/ 0, 100, 50, 3, 3);
    require(pagePixel(0, 100, 50) == 7 && pagePixel(0, 102, 52) == 7,
            "gfx_copyRect copies the block to the destination page/offset");
    require(pagePixel(0, 103, 50) == 0, "gfx_copyRect copies only the requested width");
}

// ---- gfx_blitSprite transparency (skip index 0) + edge clipping -----------
void test_blitSprite() {
    int handle = gfx_allocSpriteBuf();
    require(handle >= 1, "gfx_allocSpriteBuf returns a live handle");
    SDL_Surface *sprite = gfx_getSpriteSurface(handle);
    require(sprite != nullptr, "sprite handle resolves to a surface");
    // 4x4 sprite: top-left pixel transparent (index 0), the rest colour 9.
    uint8 *sp = (uint8 *)sprite->pixels;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            sp[(size_t)y * sprite->pitch + x] = (x == 0 && y == 0) ? kTransparentIndex : 9;

    fillPageRaw(0, 3); /* known background */
    struct SpriteParams p;
    std::memset(&p, 0, sizeof(p));
    p.bufPtr = (int16)handle;
    p.srcX = 0; p.srcY = 0; p.width = 4; p.height = 4;
    p.dstX = 50; p.dstY = 60; p.page = 0;
    gfx_blitSprite(&p);
    require(pagePixel(0, 50, 60) == 3, "gfx_blitSprite skips palette index 0 (transparent)");
    require(pagePixel(0, 51, 60) == 9 && pagePixel(0, 53, 63) == 9,
            "gfx_blitSprite copies non-zero sprite pixels");

    // Right-edge clip: dstX so only two of four columns land on-screen. The
    // r2d_blit intersection must not read/write past either surface.
    fillPageRaw(0, 3);
    p.dstX = kLogicalWidth - 2; p.dstY = 70;
    gfx_blitSprite(&p);
    require(pagePixel(0, kLogicalWidth - 1, 70) == 9, "clipped sprite draws its visible columns");

    gfx_freeSpriteBuf(handle);
}

// ---- sprite-buffer handle alloc/free + exhaustion -------------------------
void test_spriteBufExhaustion() {
    int handles[kMaxSpriteBufs];
    for (int i = 0; i < kMaxSpriteBufs; i++) {
        handles[i] = gfx_allocSpriteBuf();
        require(handles[i] >= 1 && handles[i] <= kMaxSpriteBufs,
                "sprite handles are 1-based within the pool");
    }
    require(gfx_allocSpriteBuf() == kNoHandle,
            "gfx_allocSpriteBuf returns 0 (none) when the pool is exhausted");
    // Freeing one slot lets the next alloc succeed again.
    gfx_freeSpriteBuf(handles[3]);
    int reused = gfx_allocSpriteBuf();
    require(reused >= 1, "freeing a sprite buffer lets a later alloc reuse the slot");
    // Clean up.
    for (int i = 0; i < kMaxSpriteBufs; i++)
        if (i != 3) gfx_freeSpriteBuf(handles[i]);
    gfx_freeSpriteBuf(reused);
}

// ---- gfx_drawLine: fill colour, blitOffset origin, Cohen-Sutherland clip --
void test_drawLine() {
    fillPageRaw(0, 0);
    gfx_setColor(7);
    gfx_setBlitOffset(0);
    gfx_drawLine(10, 10, 10, 20); /* vertical segment */
    int hits = 0;
    for (int y = 10; y <= 20; y++)
        if (pagePixel(0, 10, y) == 7) hits++;
    require(hits == 11, "gfx_drawLine draws the full vertical segment in the fill colour");

    // blitOffset is the viewport origin: (col,row) = (off%320, off/320).
    fillPageRaw(0, 0);
    gfx_setBlitOffset(5 * kLogicalWidth + 3); /* origin (3,5) */
    gfx_drawLine(0, 0, 0, 4);
    require(pagePixel(0, 3, 5) == 7 && pagePixel(0, 3, 9) == 7,
            "gfx_drawLine offsets endpoints by the blitOffset viewport origin");
    gfx_setBlitOffset(0);

    // Endpoint off the left edge: Cohen-Sutherland clips to x>=0 without looping
    // forever or writing out of bounds (the historical 16-bit-overflow hang).
    fillPageRaw(0, 0);
    gfx_drawLine((uint16)(int16)-50, 100, 50, 100);
    require(pagePixel(0, 0, 100) == 7 && pagePixel(0, 50, 100) == 7,
            "gfx_drawLine clips a left-offscreen endpoint to the page edge");

    // Fully off-screen segment draws nothing and does not crash.
    fillPageRaw(0, 4);
    gfx_drawLine(400, 400, 500, 500);
    require(pagePixel(0, 0, 0) == 4, "gfx_drawLine discards a wholly off-screen segment");
}

// ---- gfx_dirtyRect2 span fill + degenerate-row skip -----------------------
void test_dirtyRect2() {
    fillPageRaw(0, 0);
    gfx_setColor(6);
    gfx_setBlitOffset(0);
    // The min buffer is int16[200]; the max buffer sits 0x1b8 bytes later. Size
    // the backing array to cover both windows.
    int16 spanBuf[512];
    std::memset(spanBuf, 0, sizeof(spanBuf));
    uint16 *minBuf = (uint16 *)spanBuf;
    uint16 *maxBuf = (uint16 *)((char *)spanBuf + kDirtySpanMaxOffsetBytes);

    minBuf[50] = 100; maxBuf[50] = 110; /* normal span */
    minBuf[60] = 200; maxBuf[60] = 100; /* hi < lo -> skipped */
    minBuf[70] = 5;   maxBuf[70] = 5;   /* hi==lo, not 0/319 -> single pixel */

    gfx_dirtyRect2(spanBuf, 0, kLogicalHeight - 1);

    require(pagePixel(0, 100, 50) == 6 && pagePixel(0, 110, 50) == 6,
            "gfx_dirtyRect2 fills [lo..hi] inclusive on a normal row");
    require(pagePixel(0, 99, 50) == 0 && pagePixel(0, 111, 50) == 0,
            "gfx_dirtyRect2 fills only within the span");
    require(pagePixel(0, 150, 60) == 0 && pagePixel(0, 100, 60) == 0,
            "gfx_dirtyRect2 skips a row whose hi < lo (non-span sentinel)");
    require(pagePixel(0, 5, 70) == 6, "gfx_dirtyRect2 draws a single pixel when hi==lo (and not an edge)");
    // A zero-initialised row (lo==hi==0) is treated as empty, not a col-0 fill.
    require(pagePixel(0, 0, 30) == 0, "gfx_dirtyRect2 treats a lo==hi==0 row as empty");
}

// ---- gfx_setFont advance-width rules --------------------------------------
void test_setFont() {
    require(gfx_setFont('A', 0) == kFont0Advance && gfx_setFont(' ', 0) == kFont0Advance,
            "font 0 is fixed-pitch (advance 4)");
    require(gfx_setFont('A', 2) == kFontFallbackAdvance,
            "an absent (NULL) font table falls back to advance 8");
    require(gfx_setFont('A', 8) == kFontFallbackAdvance,
            "an out-of-range font index falls back to advance 8");
    require(gfx_setFont(kColorEscapeChar, 0) == 0,
            "a >=0x80 inline colour escape has zero advance");
    require(gfx_setFont(0x10, 0) == kFontFallbackAdvance,
            "a control char below the first printable falls back to advance 8");
    int16 textParams[7] = {};
    textParams[0] = 0;  // page
    textParams[2] = 7;  // color
    textParams[4] = 10; // x
    textParams[5] = 10; // y
    textParams[6] = 4;  // font id
    gfx_drawString(textParams, "A");
    require(gfx_setFont('A', 4) == kReplacementFontAdvance,
            "gfx_drawString lazily installs a BDF replacement font width table");
    require(gfx_setFont(0x041f, 4) == kReplacementFontAdvance,
            "BDF replacement font exposes Unicode glyph widths");
    require(gfx_setFont(0x00c4, 4) == kReplacementFontAdvance,
            "BDF replacement font exposes German Unicode glyph widths");
    require(gfx_setFont(0x0141, 4) == kReplacementFontAdvance,
            "BDF replacement font exposes Polish Unicode glyph widths");

    fillPageRaw(0, 0);
    textParams[4] = 70;
    textParams[5] = 70;
    textParams[6] = 4;
    gfx_drawString(textParams, "\xd0\x9f");
    require(pagePixel(0, 70, 70) == 7,
            "gfx_drawString decodes UTF-8 and draws a BDF Unicode glyph");
    require(textParams[4] == 70 + kReplacementFontAdvance,
            "UTF-8 BDF Unicode glyph advances by its replacement width");

    fillPageRaw(0, 0);
    textParams[4] = 40;
    textParams[5] = 40;
    textParams[6] = 5;
    gfx_drawString(textParams, " ");
    require(pagePixel(0, 40, 40) == 7,
            "gfx_drawString lazily installs a PNG atlas replacement font when BDF is absent");
}

// ---- gfx_calcRowAddr / gfx_getRowOffset / blitOffset round-trip -----------
void test_rowAddrAndBlitOffset() {
    require(gfx_getRowOffset(0) == 0 && gfx_getRowOffset(5) == 5 * kLogicalWidth,
            "gfx_getRowOffset returns row*320");
    // calcRowAddr takes (col, row) and returns rowTable[row]+col.
    require(gfx_calcRowAddr(3, 5) == 5 * kLogicalWidth + 3,
            "gfx_calcRowAddr(col,row) == row*320 + col");
    gfx_setBlitOffset(1234);
    require(gfx_getBlitOffset() == 1234, "gfx_setBlitOffset/gfx_getBlitOffset round-trip");
    gfx_setBlitOffset(0);
    // Baked preset offsets are constants, not the live blitOffset.
    require(gfx_getPresetOffset1() == 0x5580 && gfx_getPresetOffset2() == 0x1950,
            "preset offsets are baked constants");
}

// ---- DAC palette: build + gfx_setDacRange 6-bit -> 8-bit upload -----------
void test_dacPalette() {
    SDL_Palette *pal = gfx_getPalette();
    require(pal && pal->ncolors == 256, "gfx_getPalette builds a 256-entry table");

    // Upload one register (index 0x20) as a 6-bit VGA triple; the loader expands
    // 6-bit v to 8-bit via (v<<2) | (v>>4).
    uint8 triple[3] = {0x3F, 0x2A, 0x00}; /* max, 42, 0 */
    gfx_setDacRange(0x20, 1, triple);
    uint8 r = 0, g = 0, b = 0;
    gfx_paletteRGB(0x20, &r, &g, &b);
    require(r == 255, "gfx_setDacRange expands 6-bit 0x3F to 8-bit 255");
    require(g == 170, "gfx_setDacRange expands 6-bit 0x2A to 8-bit 170");
    require(b == 0, "gfx_setDacRange expands 6-bit 0x00 to 8-bit 0");
}

void test_replacementPngLoader() {
    uint8 redDac[3] = {0x3f, 0x00, 0x00};
    gfx_setDacRange(12, 1, redDac);
    fillPageRaw(0, 0);
    require(loadReplacementPngToPage("TITLE.PIC", 0) != 0,
            "loadReplacementPngToPage loads a modern PNG replacement");
    require(pagePixel(0, 0, 0) != 0 && pagePixel(0, 1, 1) != 0,
            "loadReplacementPngToPage writes decoded PNG pixels into the page surface");
}

// Deterministically compose a small scene into `page` using the primitives
// under test: a background fill, two recoloured rectangles, a diagonal line and
// a transparent sprite. Kept self-contained so the golden is reproducible.
void composeScene(int page) {
    int16 pageDesc = (int16)page;
    fillPageRaw(page, 1); /* background index 1 */
    // Two nested rectangles via switchColor (recolour the whole-page bg inside
    // each rect).
    gfx_switchColor(&pageDesc, 20, 20, 120, 90, 1, 4);
    gfx_switchColor(&pageDesc, 40, 40, 100, 70, 4, 12);
    // A diagonal line across the inner rect. drawLine targets page 0 (the single
    // back buffer); compose on page 0 so the line lands where we read.
    if (page == 0) {
        gfx_setColor(15);
        gfx_setBlitOffset(0);
        gfx_drawLine(40, 40, 100, 70);
    }
    // A 6x6 sprite with a transparent (index 0) border, opaque core index 9.
    int h = gfx_allocSpriteBuf();
    SDL_Surface *sp = gfx_getSpriteSurface(h);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 6; x++)
            ((uint8 *)sp->pixels)[(size_t)y * sp->pitch + x] =
                (x == 0 || y == 0 || x == 5 || y == 5) ? kTransparentIndex : 9;
    struct SpriteParams p;
    std::memset(&p, 0, sizeof(p));
    p.bufPtr = (int16)h;
    p.width = 6; p.height = 6;
    p.dstX = 150; p.dstY = 30; p.page = 0; /* sprite always lands on page 0 */
    gfx_blitSprite(&p);
    gfx_freeSpriteBuf(h);
}

// ---- Golden-image: render a scene and compare to a committed reference ------
void test_goldenComposedScene() {
    composeScene(0);
    SDL_Surface *surf = gfx_getPageSurface(0);
    require(golden_check(surf, "composed_scene"),
            "the composed 2D scene matches its golden image (tests/goldens/composed_scene.bmp)");
}

// ---- Golden-image: inject a committed image as initial page state -----------
void test_goldenInitialStateInjection() {
    SDL_Surface *seed = golden_loadSurface("composed_scene");
    // The golden is written by the run above (bootstrap) or committed, so it is
    // available here. Copy it into page 2 as the initial state, recolour one
    // rectangle, and compare the result to a second golden.
    require(seed != nullptr, "golden_loadSurface loads the committed scene as an initial state");
    SDL_Surface *dst = gfx_getPageSurface(2);
    require(seed->w == dst->w && seed->h == dst->h, "injected initial state matches page dimensions");
    for (int y = 0; y < dst->h; y++)
        std::memcpy((uint8 *)dst->pixels + (size_t)y * dst->pitch,
                    (uint8 *)seed->pixels + (size_t)y * seed->pitch, dst->w);
    SDL_DestroySurface(seed);
    int16 pageDesc = 2;
    gfx_switchColor(&pageDesc, 40, 40, 100, 70, 12, 6); /* recolour the inner rect */
    require(golden_check(gfx_getPageSurface(2), "composed_scene_recolored"),
            "recolouring the injected initial state matches its golden");
}

} // namespace

int main() {
    const auto replacementRoot = std::filesystem::temp_directory_path() / "f15se2-ex-gfx-replacements";
    std::filesystem::remove_all(replacementRoot);
    writeReplacementBdf(replacementRoot, 4);
    writeReplacementFontPng(replacementRoot, 5);
    writeInvalidReplacementBdf(replacementRoot, 5);
    writeReplacementPng(replacementRoot);
#if !defined(_WIN32)
    setenv("F15_REPLACEMENT_ROOT", replacementRoot.string().c_str(), 1);
#endif

    test_headless_init();
    gfx_videoInit();
    // The game enters the 320x200 mode before any flight drawing; this also
    // builds the row-offset table gfx_dirtyRect2 / gfx_calcRowAddr rely on.
    gfx_setMode13();

    test_pagePixels_and_alias();
    test_clearRect();
    test_switchColor();
    test_copyRect();
    test_blitSprite();
    test_spriteBufExhaustion();
    test_drawLine();
    test_dirtyRect2();
    test_setFont();
    test_rowAddrAndBlitOffset();
    test_dacPalette();
    test_replacementPngLoader();
    test_goldenComposedScene();
    test_goldenInitialStateInjection();

#if !defined(_WIN32)
    unsetenv("F15_REPLACEMENT_ROOT");
#endif
    std::filesystem::remove_all(replacementRoot);

    std::cout << "gfx_render_behavior_tests passed\n";
    return 0;
}
