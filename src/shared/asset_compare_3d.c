/*
 * asset_compare_3d.c - renderer-independent 3D replacement diagnostics.
 */

#include "asset_compare.h"
#include "../log.h"

enum {
    ASSET_COMPARE_3D_FORM_POINT = 1,
    ASSET_COMPARE_3D_FORM_EDGERUN = 2
};

static void compareColorCoverage(const char *label,
                                 int shapeId,
                                 const int *legacy,
                                 const int *replacement,
                                 const char *replacementPath) {
    int i;

    if (!legacy || !replacement) return;
    for (i = 0; i < 256; i++) {
        if (legacy[i] != replacement[i]) {
            LogWarn((
                "asset replacement compare: shape %d GLB %s color coverage differs at raw color 0x%02x "
                "(legacy=%d glb=%d, source=%s)",
                shapeId,
                label,
                i,
                legacy[i],
                replacement[i],
                replacementPath ? replacementPath : ""
            ));
            return;
        }
    }
}

void assetCompare3dShapeStats(const AssetCompare3dShapeStats *stats) {
    if (!assetCompareEnabled() || !stats) return;

    if (stats->missingSourceMeta) {
        LogWarn(("asset replacement compare: shape %d runtime mesh cache lacks order-sensitive source metadata; custom GLB can still render, but original .3D3 order/color proof is unavailable", stats->shapeId));
    } else if (stats->missingSourceColor) {
        LogWarn(("asset replacement compare: shape %d runtime mesh cache lacks raw source color metadata; custom GLB can still render, but original .3D3 color proof is unavailable", stats->shapeId));
    } else if (stats->badSourceOrder) {
        LogWarn(("asset replacement compare: shape %d runtime mesh cache source primitive order is not monotonic; coplanar faces or line details may render differently", stats->shapeId));
    } else if (stats->badSourceMode) {
        LogWarn(("asset replacement compare: shape %d runtime mesh cache source primitive kind does not match GL draw mode; regenerate cache from GLB", stats->shapeId));
    }

    if (stats->legacyForm == ASSET_COMPARE_3D_FORM_POINT) {
        if (stats->glbPoints == 1 && stats->glbTriangles == 0 && stats->glbLines == 0) {
            LogInfo(("asset replacement compare: shape %d GLB point form matches legacy point form", stats->shapeId));
        } else {
            LogWarn(("asset replacement compare: shape %d GLB primitive form differs from legacy point (tri=%d line=%d point=%d, source=%s)",
                     stats->shapeId, stats->glbTriangles, stats->glbLines, stats->glbPoints, stats->replacementPath ? stats->replacementPath : ""));
        }
        if (stats->canCompareSourceColors) {
            compareColorCoverage("point", stats->shapeId, stats->legacyPointColor, stats->glbPointColor, stats->replacementPath);
        }
        return;
    }

    if (stats->legacyForm == ASSET_COMPARE_3D_FORM_EDGERUN) {
        if (stats->glbPoints == stats->legacyPoints && stats->glbTriangles == 0 && stats->glbLines == 0) {
            LogInfo(("asset replacement compare: shape %d GLB point run count matches legacy (%d)", stats->shapeId, stats->legacyPoints));
        } else {
            LogWarn(("asset replacement compare: shape %d GLB point run differs (legacy_points=%d glb_tri=%d glb_line=%d glb_point=%d, source=%s)",
                     stats->shapeId, stats->legacyPoints, stats->glbTriangles, stats->glbLines, stats->glbPoints, stats->replacementPath ? stats->replacementPath : ""));
        }
        if (stats->canCompareSourceColors) {
            compareColorCoverage("point-run", stats->shapeId, stats->legacyPointColor, stats->glbPointColor, stats->replacementPath);
        }
        return;
    }

    if (stats->glbTriangles == stats->legacyTriangles && stats->glbLines == stats->legacyLines) {
        LogInfo(("asset replacement compare: shape %d GLB topology matches legacy (legacy_faces=%d legacy_tri=%d legacy_lines=%d)",
                 stats->shapeId, stats->legacyFaces, stats->legacyTriangles, stats->legacyLines));
    } else {
        LogWarn(("asset replacement compare: shape %d GLB topology differs (legacy_faces=%d legacy_tri=%d legacy_lines=%d glb_tri=%d glb_line=%d, source=%s)",
                 stats->shapeId, stats->legacyFaces, stats->legacyTriangles, stats->legacyLines,
                 stats->glbTriangles, stats->glbLines, stats->replacementPath ? stats->replacementPath : ""));
    }

    if (stats->canCompareSourceColors) {
        compareColorCoverage("face", stats->shapeId, stats->legacyFaceColor, stats->glbFaceColor, stats->replacementPath);
        compareColorCoverage("line", stats->shapeId, stats->legacyLineColor, stats->glbLineColor, stats->replacementPath);
    }
}
