#ifndef R3DMESH_H
#define R3DMESH_H

/*
 * Display-list -> structured Mesh decoder.
 *
 * A "model" in the original is a raw DOS display-list byte stream inside
 * g_world3dData, re-decoded every frame by the software rasterizer. A GPU
 * backend can't consume that, so this module parses the stream once into clean
 * geometry that can be uploaded to a VBO/IBO. The software backend keeps
 * rasterizing the raw stream (it's proven and low-end fast); this struct is the
 * GPU upload contract and the documented reference for the byte format.
 *
 * Topology (faithful to the stream): vertices -> edges (vertex pairs) -> faces
 * (loops of edge indices) + lines (single edge index). Faces don't carry a
 * stream visibility mask (they cull via the per-face sign-mask bits in the
 * opcode); edges and lines do. Back-face normals are a parallel table.
 *
 * Coordinates are model-space int16. Colours stay as the raw colorByte (final
 * palette index = colorLut[colorByte] + shade) so palette/shade changes need no
 * re-decode. The decoder resolves the shared-vertex indirection
 * (buf3d3_1/2/3 -> the vertexX / modelVertY / modelVertZ pools) into flat coords.
 */

#include "inttype.h"

#define R3DMESH_MAX_LOD 6      /* g_curLod is 0..4; a few headers max */
#define R3DMESH_MAX_VERTS 128  /* g_vtxCamX[121] caps per-model vertices */
#define R3DMESH_MAX_NORMALS 32 /* face-normal count is 5 bits (<=31) */
#define R3DMESH_MAX_EDGES 256
#define R3DMESH_MAX_FACES 256
#define R3DMESH_MAX_LINES 256
#define R3DMESH_MAX_FACE_EDGES 32 /* edges per face (countByte) */
#define R3DMESH_MAX_RUNREFS 256   /* edge-run point refs */

enum {
    MESH_FORM_MODEL = 0, /* normals + verts + edges + faces/lines */
    MESH_FORM_POINT,     /* single shaded point (opcode 0x3f) */
    MESH_FORM_EDGERUN    /* run of shaded points (opcode 0x3e) */
};

typedef struct {
    int16 x, y, z;
} MeshVtx; /* model space */
typedef struct {
    int16 nx, ny, nz, threshold;
} MeshNormal;
typedef struct {
    uint8 va, vb;
    uint32 visMask;
} MeshEdge; /* va/vb index verts */
typedef struct {
    uint8 nEdges;
    uint8 edge[R3DMESH_MAX_FACE_EDGES]; /* indices into MeshLod.edges */
    uint8 colorByte;
    uint8 cullNormal; /* index into normals[] whose back-facing sign gates this face
                       * (from the face opcode: ((opc>>3)&0xf) + (opc&0x80 ? 16 : 0)).
                       * The original tests bit `cullNormal` of the per-object sign
                       * mask; a face is culled when that normal faces away. */
} MeshFace;
typedef struct {
    uint8 edge;
    uint8 colorByte;
    uint32 visMask;
} MeshLine;

typedef struct {
    int32 gateDistance; /* colorLut[0x10+bits] distance gate; -1 = terminal LOD */
    uint8 form;         /* MESH_FORM_* */
    uint8 sortFlag;     /* nonzero if the object needs depth sorting (0x40 bit) */

    /* MESH_FORM_MODEL */
    MeshVtx verts[R3DMESH_MAX_VERTS];
    int nVerts;
    MeshNormal normals[R3DMESH_MAX_NORMALS];
    int nNormals;
    MeshEdge edges[R3DMESH_MAX_EDGES];
    int nEdges;
    MeshFace faces[R3DMESH_MAX_FACES];
    int nFaces;
    MeshLine lines[R3DMESH_MAX_LINES];
    int nLines;
    int explicitVerts; /* 1 = coords were inline in the stream, 0 = shared pool */

    /* MESH_FORM_POINT / MESH_FORM_EDGERUN */
    uint8 pointColor;
    uint8 runRefs[R3DMESH_MAX_RUNREFS];
    int nRunRefs;

    int consumed;  /* body bytes the decoder consumed (for the self-test anchor) */
    uint8 usedRle; /* this LOD's primitives came via the 0xff reordered path */
} MeshLod;

typedef struct {
    MeshLod lods[R3DMESH_MAX_LOD];
    int nLods;
} Mesh;

/* Source for shared-vertex resolution. The decoder reads model-space coords from
 * these pools when a model uses the shared/indexed vertex encoding. Pass the
 * loaded game tables (eg3dload.c) via r3dmesh_useGamePools(), or NULL to skip
 * coordinate resolution (parse-only). */
typedef struct {
    const uint8 *idxX, *idxY, *idxZ; /* buf3d3_1/2/3: ref -> coord index */
    const int16 *coordX, *coordY, *coordZ;
    int nRefs;      /* size3d3_3 */
    int nX, nY, nZ; /* size3d3_4/5/6 */
} MeshVtxPools;

/* Decode one LOD body (starting just after its 3-byte header, or at the model
 * start for a header-less model). Fills *out and returns bytes consumed, or -1
 * on a parse error / capacity overflow. `limit` bounds the read for safety. */
int r3dmesh_decodeLod(const uint8 *body, const uint8 *limit,
                      const MeshVtxPools *pools, MeshLod *out);

/* Decode a model's *finest* LOD, starting at its render-mode byte. The LOD chain
 * is a tree of headers (near branch = descend to more detail, far branch = signed
 * skip to a sibling/shared header); the finest LOD is the near branch at every
 * header, so this never follows a skip and is immune to the signed/shared-skip
 * graph. The GPU backend uses the finest LOD always (docs/render-3d-backend.md,
 * resolved Q3). Result lands in out->lods[0] with out->nLods == 1. `colorLut`
 * supplies the distance gate (may be NULL). Returns body bytes consumed, or -1. */
int r3dmesh_decode(const uint8 *model, const uint8 *limit,
                   const MeshVtxPools *pools, const uint8 *colorLut,
                   Mesh *out);

/* Run a self-test over every loaded world shape (buf3d3) using the game pools,
 * cross-checking each non-terminal LOD's consumed length against the header's
 * declared skip offset (byte-exact). Logs a summary; returns the number of
 * shapes that failed to decode cleanly (0 = all good). */
int r3dmesh_selfTest(void);

#endif /* R3DMESH_H */
