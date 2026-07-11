/*
 * asset_compare_bytes.c - byte-stream replacement diagnostics.
 */

#include "asset_compare.h"
#include "../log.h"

void assetCompareNamedBytes(const char *label,
                            const char *matchText,
                            const char *diffText,
                            const char *replacementValueName,
                            const uint8 *legacyData,
                            size_t legacySize,
                            const uint8 *replacementData,
                            size_t replacementSize,
                            const char *replacementPath) {
    size_t commonSize;
    size_t firstDiff;

    if (!assetCompareEnabled()) return;
    if (!label || !legacyData || !replacementData) return;
    if (!matchText) matchText = "replacement bytes match legacy bytes";
    if (!diffText) diffText = "replacement differs";
    if (!replacementValueName) replacementValueName = "replacement";

    if (legacySize == replacementSize) {
        size_t i;
        int same = 1;
        for (i = 0; i < legacySize; i++) {
            if (legacyData[i] != replacementData[i]) {
                same = 0;
                break;
            }
        }
        if (same) {
            LogInfo(("asset replacement compare: %s %s (%zu bytes)", label, matchText, replacementSize));
            return;
        }
    }

    commonSize = legacySize < replacementSize ? legacySize : replacementSize;
    firstDiff = 0;
    while (firstDiff < commonSize && legacyData[firstDiff] == replacementData[firstDiff]) {
        firstDiff++;
    }
    if (firstDiff < commonSize) {
        LogWarn((
            "asset replacement compare: %s %s at byte %zu "
            "(legacy=0x%02x %s=0x%02x, legacy_size=%zu %s_size=%zu, replacement=%s)",
            label,
            diffText,
            firstDiff,
            (unsigned int)legacyData[firstDiff],
            replacementValueName,
            (unsigned int)replacementData[firstDiff],
            legacySize,
            replacementValueName,
            replacementSize,
            replacementPath ? replacementPath : ""
        ));
    } else {
        LogWarn((
            "asset replacement compare: %s %s length differs "
            "(legacy_size=%zu %s_size=%zu, replacement=%s)",
            label,
            diffText,
            legacySize,
            replacementValueName,
            replacementSize,
            replacementPath ? replacementPath : ""
        ));
    }
}

void assetCompareStructuredBytes(const char *filename,
                                 const uint8 *legacyData,
                                 size_t legacySize,
                                 const uint8 *replacementData,
                                 size_t replacementSize,
                                 const char *replacementPath) {
    assetCompareNamedBytes(
        filename,
        "JSON rebuild matches legacy bytes",
        "JSON rebuild differs",
        "replacement",
        legacyData,
        legacySize,
        replacementData,
        replacementSize,
        replacementPath
    );
}
