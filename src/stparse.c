/* Grid/terrain parsing entry point */
#include "stcode.h"
#include "stdata.h"
#include "stparse.h"
#include "shared/common.h"
#include "log.h"
#include "slot.h"

#include <stdio.h>
#include "dos_compat.h"

/* Private helpers for this translation unit. */
void parseTerrain(char *dest);
void parseGrid();
int16 showMsgWaitKey(const char *);
void replaceExtension(char *dest, const char *source);

void parseGridTerrain(void) {
    parseGrid();
    parseTerrain(regnPlhPtr);
    terrainDirtyFlag = 0;
}

void parseTerrain(char *filename) {
    int16 tileIdx, level, tileOffset, entry;
    uint16 tileNum;
    replaceExtension(filename, ".3dT");
    if ((fileHandle = openFile(filename, 0)) == 0) {
        showMsgWaitKey("Open Error on *.3DT, assuming new file !");
    } else {
        fileRead(&terrainSignature, 2, 1, fileHandle);
        if (terrainSignature != TERRAIN_MAGIC) {
            showMsgWaitKey("Bad Tile file format.");
        } else {
            fileRead(terrainBuf1, 2, 5, fileHandle);
            for (level = 0; level < 5; level++) {
                if (terrainBuf1[level] > 32) {
                    showMsgWaitKey("Too many tiles.");
                    return;
                }
                fileRead(&terrainTileCounts[level], 2, terrainBuf1[level], fileHandle);
            }
            tileOffset = 0;
            for (level = 0; level < 5; level = level + 1) {
                for (entry = 0; terrainBuf1[level] > entry; entry++) {
                    terrainTilePtrs[level].entries[entry] = (struct TerrainTile *)((uint8 *)terrainTileBlock + tileOffset);
                    for (tileNum = 0; tileNum < terrainTileCounts[level].entries[entry]; tileNum++) {
                        if (tileOffset > 3500) {
                            showMsgWaitKey("Too much tile data");
                            return;
                        }
                        fileRead(&((struct TerrainTile *)((uint8 *)terrainTileBlock + tileOffset))->buf3, 2, 1, fileHandle);
                        fileRead(&((struct TerrainTile *)((uint8 *)terrainTileBlock + tileOffset))->buf4, 2, 1, fileHandle);
                        fileRead(&((struct TerrainTile *)((uint8 *)terrainTileBlock + tileOffset))->buf5, 2, 1, fileHandle);
                        fileRead(&tileIdx, 2, 1, fileHandle);
                        ((struct TerrainTile *)((uint8 *)terrainTileBlock + tileOffset))->idx = tileIdx;
                        tileOffset += sizeof(struct TerrainTile);
                    }
                }
            }
        }
        fileClose(fileHandle);
    }
}

/* ---- merged from stgrid.c ---- */
void parseGrid() {
    int16 idx;
    replaceExtension(regnPlhPtr, ".3dG");
    if ((fileHandle = openFile(regnPlhPtr, 0)) == 0) {
        showMsgWaitKey("Open Error on *.3DG, assuming new file !");
        idx = 0;
        do {
            gridBuf1[idx] = idx;
            idx++;
        } while (idx < 16);
        nearmemset(gridBuf2, 0, 0x100);
        nearmemset(gridBuf3, 0, 0x200);
        nearmemset(gridBuf4, 0, 0x200);
        nearmemset(gridBuf5, 0, 0x200);
        gridValidFlag = 0;
        return;
    }
    fileRead(&gridSignature, 2, 1, fileHandle);
    if (gridSignature != GRID_MAGIC) {
        showMsgWaitKey("Bad Grid file format.");
    } else {
        fileRead(gridBuf1, 1, 16, fileHandle);
        fileRead(gridBuf2, 1, 0x100, fileHandle);
        fileRead(gridBuf3, 1, 0x200, fileHandle);
        fileRead(gridBuf4, 1, 0x200, fileHandle);
        fileRead(gridBuf5, 1, 0x200, fileHandle);
    }
    fileClose(fileHandle);
}

int16 showMsgWaitKey(const char *msg) {
    doNothing2(msg, 0, 96, 0x0f);
    return misc_getKey();
}

void replaceExtension(char *path, const char *source) {
    int8 ch;
    for (; (ch = *path) != '.';) {
        if (ch == 0) break;
        path++;
    }
    mystrcpy(path, source);
}
