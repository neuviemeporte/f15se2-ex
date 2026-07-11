/*
 * asset_compare_font.c - bitmap font replacement diagnostics.
 */

#include "asset_compare.h"
#include "../log.h"

void assetCompareFont96(const char *label,
                        const uint8 *legacyBitmap,
                        const uint8 *legacyWidths,
                        int legacyHeight,
                        int legacyWidth,
                        const uint8 *replacementBitmap,
                        const uint8 *replacementWidths,
                        int replacementHeight,
                        int replacementWidth,
                        const char *replacementPath) {
    int i;
    int row;

    if (!assetCompareEnabled()) return;
    if (!label || !legacyBitmap || !legacyWidths || !replacementBitmap || !replacementWidths) return;
    if (legacyHeight <= 0 || legacyWidth <= 0 || replacementHeight <= 0 || replacementWidth <= 0) return;

    if (replacementHeight != legacyHeight || replacementWidth != legacyWidth) {
        LogWarn((
            "asset replacement compare: %s metrics differ "
            "(builtin=%dx%d replacement=%dx%d, source=%s)",
            label,
            legacyWidth,
            legacyHeight,
            replacementWidth,
            replacementHeight,
            replacementPath ? replacementPath : ""
        ));
        return;
    }

    for (i = 0; i < 96; i++) {
        if (replacementWidths[i] != legacyWidths[i]) {
            LogWarn((
                "asset replacement compare: %s width differs for glyph 0x%02x "
                "(builtin=%u replacement=%u, source=%s)",
                label,
                i + 0x20,
                (unsigned)legacyWidths[i],
                (unsigned)replacementWidths[i],
                replacementPath ? replacementPath : ""
            ));
            return;
        }
        for (row = 0; row < legacyHeight; row++) {
            uint8 legacyByte = legacyBitmap[(i * legacyHeight) + row];
            uint8 replacementByte = replacementBitmap[(i * replacementHeight) + row];
            if (replacementByte != legacyByte) {
                LogWarn((
                    "asset replacement compare: %s bitmap differs for glyph 0x%02x row %d "
                    "(builtin=0x%02x replacement=0x%02x, source=%s)",
                    label,
                    i + 0x20,
                    row,
                    (unsigned)legacyByte,
                    (unsigned)replacementByte,
                    replacementPath ? replacementPath : ""
                ));
                return;
            }
        }
    }

    LogInfo(("asset replacement compare: %s replacement matches built-in glyph rows and widths", label));
}
