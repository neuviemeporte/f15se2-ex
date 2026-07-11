/*
 * asset_compare.h - replacement comparison helpers.
 *
 * Asset loaders should load modern media/table formats. Equivalence proof is
 * run by tests/validation tooling, not by normal gameplay. The helpers remain
 * isolated here so comparison policy does not leak into individual loaders.
 */
#ifndef F15_ASSET_COMPARE_H
#define F15_ASSET_COMPARE_H

#include "../inttype.h"
#include <stddef.h>

int assetCompareEnabled(void);
void assetCompareNamedBytes(const char *label,
                            const char *matchText,
                            const char *diffText,
                            const char *replacementValueName,
                            const uint8 *legacyData,
                            size_t legacySize,
                            const uint8 *replacementData,
                            size_t replacementSize,
                            const char *replacementPath);
void assetCompareStructuredBytes(const char *filename,
                                 const uint8 *legacyData,
                                 size_t legacySize,
                                 const uint8 *replacementData,
                                 size_t replacementSize,
                                 const char *replacementPath);
void assetCompareIndexedPixels2D(const char *label,
                                 const uint8 *legacyPixels,
                                 int legacyPitch,
                                 const uint8 *replacementPixels,
                                 int replacementPitch,
                                 int width,
                                 int height,
                                 const char *replacementPath);
void assetCompareRgbPalettes(const char *label,
                             const uint8 *legacyRgb,
                             int legacyCount,
                             const uint8 *replacementRgb,
                             int replacementCount,
                             const char *replacementPath);
void assetCompareFont96(const char *label,
                        const uint8 *legacyBitmap,
                        const uint8 *legacyWidths,
                        int legacyHeight,
                        int legacyWidth,
                        const uint8 *replacementBitmap,
                        const uint8 *replacementWidths,
                        int replacementHeight,
                        int replacementWidth,
                        const char *replacementPath);
void assetCompareSoundCueRange(const char *cueId,
                               uint16 legacyStart,
                               uint16 legacyEndInclusive,
                               int legacySampleRate,
                               const uint8 *legacyBlob,
                               size_t legacyBlobSize,
                               const uint8 *replacementSamples,
                               size_t replacementSampleSize,
                               int replacementSampleRate,
                               const char *replacementPath);

typedef struct AssetCompare3dShapeStats {
    int shapeId;
    int legacyForm;
    int legacyFaces;
    int legacyTriangles;
    int legacyLines;
    int legacyPoints;
    int glbTriangles;
    int glbLines;
    int glbPoints;
    int missingSourceMeta;
    int missingSourceColor;
    int badSourceOrder;
    int badSourceMode;
    int canCompareSourceColors;
    const int *legacyFaceColor;
    const int *legacyLineColor;
    const int *legacyPointColor;
    const int *glbFaceColor;
    const int *glbLineColor;
    const int *glbPointColor;
    const char *replacementPath;
} AssetCompare3dShapeStats;

void assetCompare3dShapeStats(const AssetCompare3dShapeStats *stats);

#endif /* F15_ASSET_COMPARE_H */
