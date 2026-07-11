#ifndef R3D_REPLACEMENT_H
#define R3D_REPLACEMENT_H

#include "inttype.h"

typedef struct R3DReplacementPrim {
    int mode; /* GL-style primitive mode: 4 triangles, 1 lines, 0 points. */
    int nVerts;
    int sourceKind;
    int sourceIndex;
    int sourceColor;
    uint32 sourceFlags;
    float rgba[4];
    float *xyz;
} R3DReplacementPrim;

typedef struct R3DReplacementMesh {
    char path[512];
    int loaded;
    int failed;
    int compared;
    int hasSourceMd5;
    char sourceMd5[33];
    int nPrims;
    R3DReplacementPrim *prims;
} R3DReplacementMesh;

/* Shared modern 3D replacement loader. Renderers consume only mode/color/vertex
 * data. source* fields are strict-validation proof metadata and are optional for
 * third-party GLBs. */
R3DReplacementMesh *r3d_replacementMesh(const char *containerLegacyName, int shapeId);

#endif /* R3D_REPLACEMENT_H */
