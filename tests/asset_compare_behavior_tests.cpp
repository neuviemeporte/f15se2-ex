#include "shared/asset_compare.h"
#include "gfx_impl.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

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

} // namespace

int eg3drast_testReplacementColorFromMaterial(float r, float g, float b, int sourceColor);
int eg3drast_testReplacementLineNearClip(long aDepth, long bDepth);

int main() {
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

    const uint8 legacyBytes[] = {1, 2, 3, 4};
    const uint8 modernBytes[] = {1, 2, 3, 4};
    assetCompareNamedBytes("bytes",
                           "modern bytes match",
                           "modern bytes differ",
                           "modern",
                           legacyBytes,
                           sizeof(legacyBytes),
                           modernBytes,
                           sizeof(modernBytes),
                           "bytes.replacement");
    assetCompareStructuredBytes("VN.WLD",
                                legacyBytes,
                                sizeof(legacyBytes),
                                modernBytes,
                                sizeof(modernBytes),
                                "VN/VN.WLD.json");

    const uint8 legacyPixels[] = {1, 2, 3, 4};
    const uint8 modernPixels[] = {1, 2, 3, 4};
    assetCompareIndexedPixels2D("TITLE.PIC",
                                legacyPixels,
                                kImageWidth,
                                modernPixels,
                                kImageWidth,
                                kImageWidth,
                                kImageHeight,
                                "TITLE.png");

    const uint8 legacyRgb[kPaletteEntries * 3] = {0, 0, 0, 63, 31, 0};
    const uint8 modernRgb[kPaletteEntries * 3] = {0, 0, 0, 63, 31, 0};
    assetCompareRgbPalettes("TITLE.PIC",
                            legacyRgb,
                            kPaletteEntries,
                            modernRgb,
                            kPaletteEntries,
                            "TITLE.png");

    uint8 legacyFontBitmap[kFontGlyphs * kFontHeight] = {};
    uint8 modernFontBitmap[kFontGlyphs * kFontHeight] = {};
    uint8 legacyFontWidths[kFontGlyphs] = {};
    uint8 modernFontWidths[kFontGlyphs] = {};
    for (int i = 0; i < kFontGlyphs; i++) {
        legacyFontBitmap[i] = modernFontBitmap[i] = static_cast<uint8>(i & 0xff);
        legacyFontWidths[i] = modernFontWidths[i] = kFontWidth;
    }
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

    const uint8 legacySound[] = {9, 8, 7, 6};
    const uint8 modernSound[] = {9, 8, 7, 6};
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
    assetCompare3dShapeStats(&stats);

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
    std::cout << "asset_compare_behavior_tests passed\n";
    return 0;
}
