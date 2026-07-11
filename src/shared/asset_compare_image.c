/*
 * asset_compare_image.c - indexed image and palette replacement diagnostics.
 */

#include "asset_compare.h"
#include "../log.h"

void assetCompareIndexedPixels2D(const char *label,
                                 const uint8 *legacyPixels,
                                 int legacyPitch,
                                 const uint8 *replacementPixels,
                                 int replacementPitch,
                                 int width,
                                 int height,
                                 const char *replacementPath) {
    int x;
    int y;

    if (!assetCompareEnabled()) return;
    if (!label || !legacyPixels || !replacementPixels || width <= 0 || height <= 0) return;

    for (y = 0; y < height; y++) {
        const uint8 *legacyRow = legacyPixels + (size_t)y * (size_t)legacyPitch;
        const uint8 *replacementRow = replacementPixels + (size_t)y * (size_t)replacementPitch;
        for (x = 0; x < width; x++) {
            if (legacyRow[x] != replacementRow[x]) {
                LogWarn((
                    "asset replacement compare: %s PNG pixels differ at %d,%d "
                    "(legacy=0x%02x png=0x%02x, png=%s)",
                    label,
                    x,
                    y,
                    (unsigned int)legacyRow[x],
                    (unsigned int)replacementRow[x],
                    replacementPath ? replacementPath : ""
                ));
                return;
            }
        }
    }

    LogInfo(("asset replacement compare: %s PNG pixels match legacy decode in %dx%d surface", label, width, height));
}

void assetCompareRgbPalettes(const char *label,
                             const uint8 *legacyRgb,
                             int legacyCount,
                             const uint8 *replacementRgb,
                             int replacementCount,
                             const char *replacementPath) {
    int i;
    int commonCount;

    if (!assetCompareEnabled()) return;
    if (!label || !legacyRgb || !replacementRgb || legacyCount <= 0 || replacementCount <= 0) return;

    commonCount = legacyCount < replacementCount ? legacyCount : replacementCount;
    for (i = 0; i < commonCount; i++) {
        const uint8 *legacy = legacyRgb + i * 3;
        const uint8 *replacement = replacementRgb + i * 3;
        if (legacy[0] != replacement[0] || legacy[1] != replacement[1] || legacy[2] != replacement[2]) {
            LogWarn((
                "asset replacement compare: %s PNG palette differs at index %d "
                "(legacy=%02x,%02x,%02x png=%02x,%02x,%02x, png=%s)",
                label,
                i,
                (unsigned int)legacy[0],
                (unsigned int)legacy[1],
                (unsigned int)legacy[2],
                (unsigned int)replacement[0],
                (unsigned int)replacement[1],
                (unsigned int)replacement[2],
                replacementPath ? replacementPath : ""
            ));
            return;
        }
    }

    if (legacyCount != replacementCount) {
        LogWarn((
            "asset replacement compare: %s PNG palette entry count differs "
            "(legacy=%d png=%d, png=%s)",
            label,
            legacyCount,
            replacementCount,
            replacementPath ? replacementPath : ""
        ));
        return;
    }

    LogInfo(("asset replacement compare: %s PNG embedded palette matches active legacy DAC (%d entries)", label, replacementCount));
}
