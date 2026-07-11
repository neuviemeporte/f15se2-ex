/*
 * asset_compare_sound.c - digitized cue WAV replacement diagnostics.
 */

#include "asset_compare.h"
#include "../log.h"

#include <stdio.h>

void assetCompareSoundCueRange(const char *cueId,
                               uint16 legacyStart,
                               uint16 legacyEndInclusive,
                               int legacySampleRate,
                               const uint8 *legacyBlob,
                               size_t legacyBlobSize,
                               const uint8 *replacementSamples,
                               size_t replacementSampleSize,
                               int replacementSampleRate,
                               const char *replacementPath) {
    size_t start;
    size_t end;
    char label[128];

    if (!assetCompareEnabled() || !cueId || !replacementSamples) return;
    if (legacySampleRate > 0 && replacementSampleRate > 0 && legacySampleRate != replacementSampleRate) {
        LogWarn((
            "asset replacement compare: cue %s WAV sample rate differs "
            "(legacy=%d wav=%d, replacement=%s)",
            cueId,
            legacySampleRate,
            replacementSampleRate,
            replacementPath ? replacementPath : ""
        ));
    }
    if (!legacyBlob || legacyBlobSize == 0) {
        LogWarn(("asset replacement compare: no legacy F15DGTL.BIN available for cue WAV %s", replacementPath));
        return;
    }

    start = (size_t)legacyStart;
    end = (size_t)legacyEndInclusive + 1u;
    if (start >= end || end > legacyBlobSize) {
        LogWarn(("asset replacement compare: legacy cue range invalid for %s", cueId));
        return;
    }

    snprintf(label, sizeof(label), "cue %s", cueId);
    assetCompareNamedBytes(
        label,
        "WAV samples match legacy range",
        "WAV differs",
        "wav",
        legacyBlob + start,
        end - start,
        replacementSamples,
        replacementSampleSize,
        replacementPath
    );
}
