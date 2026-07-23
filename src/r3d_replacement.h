/*
 * r3d_replacement.h - Runtime representation of an editable per-shape model.
 */
#ifndef R3D_REPLACEMENT_H
#define R3D_REPLACEMENT_H

#include "inttype.h"

typedef struct R3DReplacementPrim {
    int mode; /* OpenGL primitive mode: 4 triangles, 1 lines, 0 points. */
    int nVerts;
    int sourceKind;
    int sourceIndex;
    int sourceColor;
    uint32 sourceFlags;
    float rgba[4];
    float *xyz;
} R3DReplacementPrim;

typedef struct R3DReplacementMesh {
    int nPrims;
    R3DReplacementPrim *prims;
} R3DReplacementMesh;

/*
 * Return a cached replacement for one legacy shape slot, or NULL to use the
 * original model. GLB extras are optional; drawing consumes only mode, RGBA,
 * and vertex positions from the generated cache.
 */
R3DReplacementMesh *r3dReplacementMesh(const char *container_name,
                                       int shape_id);
void r3dReplacementShutdown(void);

#endif
