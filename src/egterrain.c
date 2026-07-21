/* Collision query for the visual 3DT/3D3 terrain.
 *
 * The original flight model only gave g_groundAltitude values to runways and
 * carriers, so mountains were decorative and could be flown through. This
 * module deliberately consumes the same loaded tile tables and decoded 3D3
 * faces as the renderer. It does not invent a separate height map that could
 * drift from customized replacement models.
 */
#include "egterrain.h"

#include "eg3dgrid.h"
#include "eg3dmap.h"
#include "eg3dproj.h"
#include "egdata.h"
#include "egtypes.h"

#include <stdint.h>
#include <string.h>

enum {
    TERRAIN_LOD = 4,
    TERRAIN_TILE_SHIFT = 12,
    TERRAIN_TILE_HALF = 0x800,
    TERRAIN_FINE_SCALE = 64
};

int g_terrainAltitude = EG_TERRAIN_NO_HEIGHT;

static Mesh s_terrainMesh;

static int64_t cross2(int ax, int ay, int bx, int by) {
    return (int64_t)ax * by - (int64_t)ay * bx;
}

/* Recover the vertex cycle from the stream's unordered boundary-edge set. The
 * same degree-two walk is used by r3d_gl.c before submitting GL_POLYGON. */
static int buildFaceRing(const MeshLod *lod, const MeshFace *face,
                         int *ring, int ringCapacity) {
    uint8 neighbors[R3DMESH_MAX_VERTS][2];
    uint8 degree[R3DMESH_MAX_VERTS];
    int first, current, previous, count, i;

    if (face->nEdges < 3 || face->nEdges > ringCapacity) return 0;
    memset(degree, 0, sizeof degree);
    for (i = 0; i < face->nEdges; i++) {
        int edgeIndex = face->edge[i];
        const MeshEdge *edge;
        if (edgeIndex >= lod->nEdges) return 0;
        edge = &lod->edges[edgeIndex];
        if (edge->va >= lod->nVerts || edge->vb >= lod->nVerts) return 0;
        if (degree[edge->va] >= 2 || degree[edge->vb] >= 2) return 0;
        neighbors[edge->va][degree[edge->va]++] = edge->vb;
        neighbors[edge->vb][degree[edge->vb]++] = edge->va;
    }

    first = lod->edges[face->edge[0]].va;
    current = first;
    previous = -1;
    count = 0;
    do {
        int next;
        if (count >= face->nEdges || degree[current] != 2) return 0;
        ring[count++] = current;
        next = neighbors[current][0] != previous
                   ? neighbors[current][0]
                   : neighbors[current][1];
        previous = current;
        current = next;
    } while (current != first);

    return count == face->nEdges ? count : 0;
}

/* Intersect a vertical ray with one triangle. The renderer's apparent `2 *`
 * vertex factor converts a Q15 matrix product to its Q16 camera-space storage;
 * it does not double physical model size. Model vertices and 3DT object origins
 * therefore share one coordinate scale. Integer barycentric arithmetic keeps
 * record/replay deterministic across platforms. */
static int triangleHeight(const MeshVtx *a, const MeshVtx *b, const MeshVtx *c,
                          int x, int y, int *height) {
    int ax = a->x, ay = a->y;
    int bx = b->x, by = b->y;
    int cx = c->x, cy = c->y;
    int64_t area = cross2(bx - ax, by - ay, cx - ax, cy - ay);
    int64_t wa, wb, wc, zNumerator;
    if (area == 0) return 0; /* vertical/degenerate faces have no ground area */

    wa = cross2(bx - x, by - y, cx - x, cy - y);
    wb = cross2(cx - x, cy - y, ax - x, ay - y);
    wc = cross2(ax - x, ay - y, bx - x, by - y);
    if (area > 0) {
        if (wa < 0 || wb < 0 || wc < 0) return 0;
    } else if (wa > 0 || wb > 0 || wc > 0) {
        return 0;
    }

    zNumerator = (int64_t)a->z * wa +
                 (int64_t)b->z * wb +
                 (int64_t)c->z * wc;
    *height = (int)(zNumerator / area);
    return 1;
}

int egTerrainMeshHeight(const MeshLod *lod, int localX, int localY, int *outZ) {
    int best = EG_TERRAIN_NO_HEIGHT;
    int faceIndex;
    if (!lod || !outZ || lod->form != MESH_FORM_MODEL) return 0;

    for (faceIndex = 0; faceIndex < lod->nFaces; faceIndex++) {
        const MeshFace *face = &lod->faces[faceIndex];
        int ring[R3DMESH_MAX_FACE_EDGES];
        int ringCount, i;
        /* 0xff is transparent in both renderers and must not become collision. */
        if (face->colorByte == 0xff) continue;
        ringCount = buildFaceRing(lod, face, ring, R3DMESH_MAX_FACE_EDGES);
        if (ringCount < 3) continue;
        for (i = 1; i + 1 < ringCount; i++) {
            int z;
            if (triangleHeight(&lod->verts[ring[0]], &lod->verts[ring[i]],
                               &lod->verts[ring[i + 1]], localX, localY, &z) &&
                (best == EG_TERRAIN_NO_HEIGHT || z > best)) {
                best = z;
            }
        }
    }
    if (best == EG_TERRAIN_NO_HEIGHT) return 0;
    *outZ = best;
    return 1;
}

static int lod4TileInBounds(int tileX, int tileY) {
    /* process3dg applies the LOD-4 grid's +2 origin bias. */
    tileX += 2;
    tileY += 2;
    return tileX >= 0 && tileY >= 0 &&
           tileX < g_lodGridDim[TERRAIN_LOD] &&
           tileY < g_lodGridDim[TERRAIN_LOD];
}

static const uint8 *tileModel(int tileX, int tileY, int subIndex,
                              const struct TileSceneObject *entry) {
    int offset;
    if (entry->shape & 0x80) {
        offset = lookupTileEntry(TERRAIN_LOD, subIndex, tileX, tileY);
        if (offset == 0) {
            int shape = entry->shape & 0x7f;
            if ((size_t)shape >= size3d3) return NULL;
            offset = buf3d3[shape];
        }
    } else {
        int shape = entry->shape;
        if ((size_t)shape >= size3d3) return NULL;
        offset = buf3d3[shape];
    }
    if (offset < 0 || (unsigned)offset >= WORLD3D_DATA_SIZE) return NULL;
    return (const uint8 *)g_world3dData + offset;
}

int egTerrainHeightAt(int32 worldX, int32 worldY) {
    MeshVtxPools pools;
    int32 scaledX = (int32)scaleCoordToLod(TERRAIN_LOD, (uint32)worldX);
    int32 scaledY = (int32)scaleCoordToLod(TERRAIN_LOD, (uint32)worldY);
    int tileX = scaledX >> TERRAIN_TILE_SHIFT;
    int tileY = scaledY >> TERRAIN_TILE_SHIFT;
    int best = EG_TERRAIN_NO_HEIGHT;
    int gridY, gridX;

    r3dmesh_gamePools(&pools);
    /* Objects can extend beyond their owning tile, so include immediate
     * neighbours rather than consulting only the point's nominal cell. */
    for (gridY = -1; gridY <= 1; gridY++) {
        for (gridX = -1; gridX <= 1; gridX++) {
            int sampleX = tileX + gridX;
            int sampleY = tileY + gridY;
            int cell, subIndex;
            struct TileSceneObject *entry;
            if (!lod4TileInBounds(sampleX, sampleY)) continue;
            cell = process3dg(TERRAIN_LOD, sampleX, sampleY);
            if (cell < 0 || cell >= 32 || !matrix3dt_2[TERRAIN_LOD][cell]) continue;
            entry = matrix3dt_2[TERRAIN_LOD][cell];
            for (subIndex = 0; subIndex < matrix3dt[TERRAIN_LOD][cell]; subIndex++, entry++) {
                const uint8 *model = tileModel(sampleX, sampleY, subIndex, entry);
                int objectX, objectY, localHeight;
                if (!model ||
                    r3dmesh_decode(model,
                                   (const uint8 *)g_world3dData + WORLD3D_DATA_SIZE,
                                   &pools, colorLut, &s_terrainMesh) < 0) {
                    continue;
                }
                objectX = (sampleX << TERRAIN_TILE_SHIFT) + TERRAIN_TILE_HALF + entry->x;
                objectY = (sampleY << TERRAIN_TILE_SHIFT) + TERRAIN_TILE_HALF + entry->y;
                if (egTerrainMeshHeight(&s_terrainMesh.lods[0],
                                        scaledX - objectX, scaledY - objectY,
                                        &localHeight)) {
                    int height = entry->z + localHeight;
                    if (best == EG_TERRAIN_NO_HEIGHT || height > best) best = height;
                }
            }
        }
    }
    return best == EG_TERRAIN_NO_HEIGHT ? best : best * TERRAIN_FINE_SCALE;
}

void egTerrainUpdateHeight(int32 worldX, int32 worldY) {
    g_terrainAltitude = egTerrainHeightAt(worldX, worldY);
}

unsigned int egTerrainViewToFlightAltitude(int viewZ) {
    if (viewZ < 0x2000) return (unsigned int)viewZ;
    if (viewZ < 0x3000) return (unsigned int)(0x2000 + (viewZ - 0x2000) * 2);
    return (unsigned int)(0x4000 + (viewZ - 0x3000) * 4);
}
