#ifndef F15_SE2_EGTERRAIN_H
#define F15_SE2_EGTERRAIN_H

#include "inttype.h"
#include "r3dmesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* No rendered terrain face covered the queried horizontal position. */
#define EG_TERRAIN_NO_HEIGHT (-32768)

/* Most recent height sampled by egTerrainUpdateHeight(), in the compressed
 * world-Z coordinate used by the renderer and g_viewZ. */
extern int g_terrainAltitude;

/* Sample the finest rendered LOD-4 tile geometry around a world position. */
int egTerrainHeightAt(int32 worldX, int32 worldY);
void egTerrainUpdateHeight(int32 worldX, int32 worldY);

/* Convert renderer/world Z back to the flight model's expanded altitude. */
unsigned int egTerrainViewToFlightAltitude(int viewZ);

/* Geometry-level entry point kept public for deterministic tests. localX/Y and
 * outZ use the same model/object units as decoded vertices and 3DT entries. */
int egTerrainMeshHeight(const MeshLod *lod, int localX, int localY, int *outZ);

#ifdef __cplusplus
}
#endif

#endif
