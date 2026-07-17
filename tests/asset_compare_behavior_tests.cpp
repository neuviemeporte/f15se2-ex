#include "shared/asset_compare.h"
#include "gfx_impl.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

enum AssetCompareTestConstant : int {
    kPaletteEntries = 2,
    kImageWidth = 2,
    kImageHeight = 2,
    kFontGlyphs = 96,
    kFontHeight = 1,
    kFontWidth = 8,
    kCueRate = 7850,
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void fillColorCoverage(int *values, int color, int count) {
    std::memset(values, 0, sizeof(int) * 256);
    values[color & 0xff] = count;
}

struct CapturedLog {
    int calls = 0;
    SDL_LogPriority priority = SDL_LOG_PRIORITY_INVALID;
    std::string message;
};

CapturedLog g_log;

void captureLog(void *, int, SDL_LogPriority priority, const char *message) {
    g_log.calls++;
    if (priority > g_log.priority) g_log.priority = priority;
    if (!g_log.message.empty()) g_log.message += "\n";
    g_log.message += message ? message : "";
}

void resetLogCapture() {
    g_log = CapturedLog{};
}

void requireLogContains(SDL_LogPriority priority, const char *needle, const char *message) {
    require(g_log.calls > 0, message);
    require(g_log.priority == priority, message);
    require(g_log.message.find(needle) != std::string::npos, message);
}

} // namespace

int eg3drast_testReplacementColorFromMaterial(float r, float g, float b, int sourceColor);
int eg3drast_testReplacementLineNearClip(long aDepth, long bDepth);

int main() {
    SDL_SetLogOutputFunction(captureLog, nullptr);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);

#if !defined(_WIN32)
    unsetenv("F15_COMPARE_REPLACEMENTS");
#endif
    require(!assetCompareEnabled(),
            "assetCompareEnabled is off during normal test/runtime execution");

#if !defined(_WIN32)
    setenv("F15_COMPARE_REPLACEMENTS", "1", 1);
#endif
    require(!assetCompareEnabled(),
            "assetCompareEnabled ignores runtime environment variables");

#ifdef DEBUG
    assetCompareTestSetEnabled(1);
#endif
    require(assetCompareEnabled(),
            "test-only compare switch enables deterministic comparison branches");

    const uint8 legacyBytes[] = {1, 2, 3, 4};
    const uint8 modernBytes[] = {1, 2, 3, 4};
    resetLogCapture();
    assetCompareNamedBytes("bytes",
                           "modern bytes match",
                           "modern bytes differ",
                           "modern",
                           legacyBytes,
                           sizeof(legacyBytes),
                           modernBytes,
                           sizeof(modernBytes),
                           "bytes.replacement");
    requireLogContains(SDL_LOG_PRIORITY_INFO, "modern bytes match",
                       "matching byte comparison logs success");

    const uint8 differentBytes[] = {1, 9, 3, 4};
    resetLogCapture();
    assetCompareNamedBytes("bytes",
                           "modern bytes match",
                           "modern bytes differ",
                           "modern",
                           legacyBytes,
                           sizeof(legacyBytes),
                           differentBytes,
                           sizeof(differentBytes),
                           "bytes.replacement");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "modern bytes differ at byte 1",
                       "byte comparison logs first differing offset");

    resetLogCapture();
    assetCompareNamedBytes("bytes",
                           "modern bytes match",
                           "modern bytes differ",
                           "modern",
                           legacyBytes,
                           sizeof(legacyBytes),
                           legacyBytes,
                           sizeof(legacyBytes) - 1,
                           "bytes.replacement");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "length differs",
                       "byte comparison logs length mismatch");

    resetLogCapture();
    assetCompareStructuredBytes("VN.WLD",
                                legacyBytes,
                                sizeof(legacyBytes),
                                modernBytes,
                                sizeof(modernBytes),
                                "VN/VN.WLD.json");
    requireLogContains(SDL_LOG_PRIORITY_INFO, "JSON rebuild matches legacy bytes",
                       "structured byte comparison uses JSON-specific success text");

    const uint8 legacyPixels[] = {1, 2, 3, 4};
    const uint8 modernPixels[] = {1, 2, 3, 4};
    resetLogCapture();
    assetCompareIndexedPixels2D("TITLE.PIC",
                                legacyPixels,
                                kImageWidth,
                                modernPixels,
                                kImageWidth,
                                kImageWidth,
                                kImageHeight,
                                "TITLE.png");
    requireLogContains(SDL_LOG_PRIORITY_INFO, "PNG pixels match",
                       "matching image comparison logs success");

    const uint8 differentPixels[] = {1, 2, 9, 4};
    resetLogCapture();
    assetCompareIndexedPixels2D("TITLE.PIC",
                                legacyPixels,
                                kImageWidth,
                                differentPixels,
                                kImageWidth,
                                kImageWidth,
                                kImageHeight,
                                "TITLE.png");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "PNG pixels differ at 0,1",
                       "image comparison logs first differing pixel");

    const uint8 legacyRgb[kPaletteEntries * 3] = {0, 0, 0, 63, 31, 0};
    const uint8 modernRgb[kPaletteEntries * 3] = {0, 0, 0, 63, 31, 0};
    resetLogCapture();
    assetCompareRgbPalettes("TITLE.PIC",
                            legacyRgb,
                            kPaletteEntries,
                            modernRgb,
                            kPaletteEntries,
                            "TITLE.png");
    requireLogContains(SDL_LOG_PRIORITY_INFO, "PNG embedded palette matches",
                       "matching palette comparison logs success");

    const uint8 differentRgb[kPaletteEntries * 3] = {0, 0, 0, 63, 30, 0};
    resetLogCapture();
    assetCompareRgbPalettes("TITLE.PIC",
                            legacyRgb,
                            kPaletteEntries,
                            differentRgb,
                            kPaletteEntries,
                            "TITLE.png");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "PNG palette differs at index 1",
                       "palette comparison logs first differing entry");

    resetLogCapture();
    assetCompareRgbPalettes("TITLE.PIC",
                            legacyRgb,
                            kPaletteEntries,
                            legacyRgb,
                            kPaletteEntries - 1,
                            "TITLE.png");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "palette entry count differs",
                       "palette comparison logs entry-count mismatch");

    uint8 legacyFontBitmap[kFontGlyphs * kFontHeight] = {};
    uint8 modernFontBitmap[kFontGlyphs * kFontHeight] = {};
    uint8 legacyFontWidths[kFontGlyphs] = {};
    uint8 modernFontWidths[kFontGlyphs] = {};
    for (int i = 0; i < kFontGlyphs; i++) {
        legacyFontBitmap[i] = modernFontBitmap[i] = static_cast<uint8>(i & 0xff);
        legacyFontWidths[i] = modernFontWidths[i] = kFontWidth;
    }
    resetLogCapture();
    assetCompareFont96("font_3",
                       legacyFontBitmap,
                       legacyFontWidths,
                       kFontHeight,
                       kFontWidth,
                       modernFontBitmap,
                       modernFontWidths,
                       kFontHeight,
                       kFontWidth,
                       "fonts/font_3.bdf");
    requireLogContains(SDL_LOG_PRIORITY_INFO, "replacement matches built-in glyph rows",
                       "matching font comparison logs success");

    modernFontWidths[10] = kFontWidth - 1;
    resetLogCapture();
    assetCompareFont96("font_3",
                       legacyFontBitmap,
                       legacyFontWidths,
                       kFontHeight,
                       kFontWidth,
                       modernFontBitmap,
                       modernFontWidths,
                       kFontHeight,
                       kFontWidth,
                       "fonts/font_3.bdf");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "width differs for glyph",
                       "font comparison logs glyph width mismatch");
    modernFontWidths[10] = kFontWidth;

    modernFontBitmap[20] ^= 0x10;
    resetLogCapture();
    assetCompareFont96("font_3",
                       legacyFontBitmap,
                       legacyFontWidths,
                       kFontHeight,
                       kFontWidth,
                       modernFontBitmap,
                       modernFontWidths,
                       kFontHeight,
                       kFontWidth,
                       "fonts/font_3.bdf");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "bitmap differs for glyph",
                       "font comparison logs glyph bitmap mismatch");
    modernFontBitmap[20] ^= 0x10;

    resetLogCapture();
    assetCompareFont96("font_3",
                       legacyFontBitmap,
                       legacyFontWidths,
                       kFontHeight,
                       kFontWidth,
                       modernFontBitmap,
                       modernFontWidths,
                       kFontHeight + 1,
                       kFontWidth,
                       "fonts/font_3.bdf");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "metrics differ",
                       "font comparison logs metric mismatch");

    const uint8 legacySound[] = {9, 8, 7, 6};
    const uint8 modernSound[] = {9, 8, 7, 6};
    resetLogCapture();
    assetCompareSoundCueRange("voice_cue_000_sample0",
                              0,
                              sizeof(legacySound) - 1,
                              kCueRate,
                              legacySound,
                              sizeof(legacySound),
                              modernSound,
                              sizeof(modernSound),
                              kCueRate,
                              "sounds/voice_cue_000_sample0.wav");
    requireLogContains(SDL_LOG_PRIORITY_INFO, "WAV samples match legacy range",
                       "matching sound cue comparison logs success");

    resetLogCapture();
    assetCompareSoundCueRange("voice_cue_000_sample0",
                              0,
                              sizeof(legacySound) - 1,
                              kCueRate,
                              legacySound,
                              sizeof(legacySound),
                              modernSound,
                              sizeof(modernSound),
                              kCueRate + 1,
                              "sounds/voice_cue_000_sample0.wav");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "WAV sample rate differs",
                       "sound comparison logs sample-rate mismatch before byte proof");

    resetLogCapture();
    assetCompareSoundCueRange("voice_cue_000_sample0",
                              3,
                              1,
                              kCueRate,
                              legacySound,
                              sizeof(legacySound),
                              modernSound,
                              sizeof(modernSound),
                              kCueRate,
                              "sounds/voice_cue_000_sample0.wav");
    requireLogContains(SDL_LOG_PRIORITY_WARN, "legacy cue range invalid",
                       "sound comparison logs invalid legacy cue range");

    int legacyFaceColor[256];
    int legacyLineColor[256];
    int legacyPointColor[256];
    int glbFaceColor[256];
    int glbLineColor[256];
    int glbPointColor[256];
    fillColorCoverage(legacyFaceColor, 12, 1);
    fillColorCoverage(glbFaceColor, 12, 1);
    fillColorCoverage(legacyLineColor, 13, 1);
    fillColorCoverage(glbLineColor, 13, 1);
    fillColorCoverage(legacyPointColor, 14, 0);
    fillColorCoverage(glbPointColor, 14, 0);

    AssetCompare3dShapeStats stats = {};
    stats.shapeId = 10;
    stats.legacyFaces = 1;
    stats.legacyTriangles = 2;
    stats.legacyLines = 1;
    stats.glbTriangles = 2;
    stats.glbLines = 1;
    stats.canCompareSourceColors = 1;
    stats.legacyFaceColor = legacyFaceColor;
    stats.legacyLineColor = legacyLineColor;
    stats.legacyPointColor = legacyPointColor;
    stats.glbFaceColor = glbFaceColor;
    stats.glbLineColor = glbLineColor;
    stats.glbPointColor = glbPointColor;
    stats.replacementPath = "15FLT/shape_010.glb";
    resetLogCapture();
    assetCompare3dShapeStats(&stats);
    requireLogContains(SDL_LOG_PRIORITY_INFO, "GLB topology matches legacy",
                       "matching 3D topology logs success");

    stats.glbLines = 2;
    resetLogCapture();
    assetCompare3dShapeStats(&stats);
    requireLogContains(SDL_LOG_PRIORITY_WARN, "GLB topology differs",
                       "3D comparison logs topology mismatch");
    stats.glbLines = 1;

    stats.missingSourceMeta = 1;
    resetLogCapture();
    assetCompare3dShapeStats(&stats);
    requireLogContains(SDL_LOG_PRIORITY_WARN, "lacks order-sensitive source metadata",
                       "3D comparison logs missing source metadata");
    stats.missingSourceMeta = 0;

    glbLineColor[13] = 2;
    resetLogCapture();
    assetCompare3dShapeStats(&stats);
    requireLogContains(SDL_LOG_PRIORITY_WARN, "line color coverage differs",
                       "3D comparison logs color coverage mismatch");
    glbLineColor[13] = 1;

    uint8 r = 0, g = 0, b = 0;
    gfx_paletteRGB(12, &r, &g, &b);
    const int colorFromMaterialA = eg3drast_testReplacementColorFromMaterial(
        r / 255.0f, g / 255.0f, b / 255.0f, 1);
    const int colorFromMaterialB = eg3drast_testReplacementColorFromMaterial(
        r / 255.0f, g / 255.0f, b / 255.0f, 255);
    require(colorFromMaterialA == colorFromMaterialB,
            "software replacement renderer ignores sourceColor while using material RGB");
    require(eg3drast_testReplacementLineNearClip(0, 65536L * 2) != 0,
            "software replacement renderer keeps a line crossing the near plane");
    require(eg3drast_testReplacementLineNearClip(0, 32768L) == 0,
            "software replacement renderer rejects a line fully behind the near plane");

#if !defined(_WIN32)
    unsetenv("F15_COMPARE_REPLACEMENTS");
#endif
#ifdef DEBUG
    assetCompareTestSetEnabled(0);
#endif
    SDL_SetLogOutputFunction(nullptr, nullptr);
    SDL_ResetLogPriorities();
    std::cout << "asset_compare_behavior_tests passed\n";
    return 0;
}
