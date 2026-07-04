/* Display-list -> Mesh decoder.
 *
 * Parses the raw DOS display-list byte stream (the format the software
 * rasterizer walks every frame in eg3drast.c) into the structured geometry in
 * r3dmesh.h. The byte layout, distilled from the rasterizer:
 *
 *   model := renderModeByte , lodChain
 *   lodChain := { lodHeader , lodBody }* , terminalBody     ; headers have 0x80
 *   lodHeader := (0x80|bits) , skipOffsetLo , skipOffsetHi  ; next header = p+skip
 *   lodBody := [storeXformByte(0x60)] , opcodeByte , ...
 *
 *   opcodeByte & 0x3f == 0x3f  -> POINT   : color (2 bytes total from opcode)
 *   opcodeByte & 0x3f == 0x3e  -> EDGERUN : 2 skip, count, count*ref
 *   otherwise                  -> MODEL   : low 5 bits = face-normal count
 *
 *   MODEL := normalCount(low5 of opcode) , normalCount*{nx,ny,nz,thr int16}
 *            , vertexSection , edgeSection , primitiveSection
 *   vertexSection := al ; if al&0x80 -> (al&0x7f) shared verts [mask, refByte]
 *                         elif al&0x7f -> that many explicit verts [mask, x,y,z]
 *   edgeSection := count , count*[mask, va, vb]
 *   primitiveSection := n ; n==0 none ; n!=0xff -> n commands
 *                          ; n==0xff -> reordered grouped commands (RLE path)
 *   command := opc ; (opc&3)==1 -> face [countByte, count*edgeIdx, color]
 *                    else        -> line [mask, edgeIdx, color]
 *
 * `mask` is 2 bytes, or 4 when the face-normal count exceeds 16 (the original's
 * g_modelWideVtxFlag). Command byte-lengths are fully determined by the opcode +
 * wide flag, independent of visibility.
 *
 * Note: the LOD skip offset is SIGNED and points to a sibling header (often far
 * forward, or backward to a shared coarse LOD) -- it is NOT the body length, so
 * a body can be followed by an unread padding byte before the next header. The
 * decoder extracts the finest LOD (descend-only) and the self-test anchors the
 * body end on the next header / shape boundary rather than the skip offset.
 */
#include "r3dmesh.h"
#include "egdata.h"
#include "egtypes.h"
#include "log.h"

#include <string.h>

/* Decode one primitive command (filled face or line), advancing *pp. Extracts
 * geometry into *out (clamped to capacity) and always consumes the exact byte
 * length the rasterizer would. Returns 0, or -1 on a bounds error. */
static int decodePrimitiveCommand(const uint8 **pp, const uint8 *limit,
                                  int wide, MeshLod *out) {
    const uint8 *p = *pp;
    if (p >= limit) return -1;
    int opc = *p++;
    if ((opc & 3) == 1) { /* filled face */
        if (p >= limit) return -1;
        int countByte = *p++;
        if (p + countByte + 1 > limit) return -1;
        if (out->nFaces < R3DMESH_MAX_FACES) {
            MeshFace *f = &out->faces[out->nFaces++];
            int store = countByte < R3DMESH_MAX_FACE_EDGES ? countByte
                                                           : R3DMESH_MAX_FACE_EDGES;
            f->nEdges = (uint8)store;
            for (int i = 0; i < store; i++) f->edge[i] = p[i];
            f->colorByte = p[countByte];
            f->cullNormal = (uint8)(((opc >> 3) & 0xf) | ((opc & 0x80) ? 16 : 0));
        }
        p += countByte + 1;
    } else { /* line */
        int maskBytes = wide ? 4 : 2;
        if (p + maskBytes + 2 > limit) return -1;
        uint32 mask = wide ? (uint32)rdU16(p) | ((uint32)rdU16(p + 2) << 16)
                           : (uint32)rdU16(p);
        p += maskBytes;
        if (out->nLines < R3DMESH_MAX_LINES) {
            MeshLine *l = &out->lines[out->nLines++];
            l->edge = p[0];
            l->colorByte = p[1];
            l->visMask = mask;
        }
        p += 2;
    }
    *pp = p;
    return 0;
}

int r3dmesh_decodeLod(const uint8 *body, const uint8 *limit,
                      const MeshVtxPools *pools, MeshLod *out) {
    const uint8 *p = body;
    memset(out, 0, sizeof *out);
    out->gateDistance = -1;

    /* storeObjTransform opcode (both 0x40 and 0x20 set) consumes one byte before
     * the real opcode; its only effect is a runtime spin-angle write — no
     * geometry, but the byte must be skipped. */
    if (p < limit && (p[0] & 0x60) == 0x60) p++;

    if (p >= limit) return -1;
    int opByte = p[0];
    int form = opByte & 0x3f;
    out->sortFlag = (opByte & 0x40) ? 1 : 0;

    if (form == 0x3f) { /* POINT */
        out->form = MESH_FORM_POINT;
        if (p + 2 > limit) return -1;
        out->pointColor = p[1];
        p += 2;
        out->consumed = (int)(p - body);
        return out->consumed;
    }
    if (form == 0x3e) { /* EDGERUN */
        out->form = MESH_FORM_EDGERUN;
        if (p + 3 > limit) return -1;
        int count = p[2];
        const uint8 *refs = p + 3;
        if (refs + count > limit) return -1;
        int store = count < R3DMESH_MAX_RUNREFS ? count : R3DMESH_MAX_RUNREFS;
        for (int i = 0; i < store; i++) out->runRefs[i] = refs[i];
        out->nRunRefs = store;
        p = refs + count;
        out->consumed = (int)(p - body);
        return out->consumed;
    }

    /* MODEL */
    out->form = MESH_FORM_MODEL;
    int nNormals = (*p++) & 0x1f;
    int wide = nNormals > 16;
    int maskBytes = wide ? 4 : 2;
    for (int i = 0; i < nNormals; i++) {
        if (p + 8 > limit) return -1;
        if (i < R3DMESH_MAX_NORMALS) {
            out->normals[i].nx = rdI16(p);
            out->normals[i].ny = rdI16(p + 2);
            out->normals[i].nz = rdI16(p + 4);
            out->normals[i].threshold = rdI16(p + 6);
        }
        p += 8;
    }
    out->nNormals = nNormals < R3DMESH_MAX_NORMALS ? nNormals : R3DMESH_MAX_NORMALS;

    /* vertices */
    if (p >= limit) return -1;
    int al = *p++;
    if (al & 0x80) { /* shared / indexed pool */
        int count = al & 0x7f;
        out->explicitVerts = 0;
        for (int i = 0; i < count; i++) {
            if (p + maskBytes + 1 > limit) return -1;
            p += maskBytes; /* visibility mask (geometry-irrelevant) */
            int ref = *p++;
            if (i < R3DMESH_MAX_VERTS) {
                if (pools && pools->idxX && ref < pools->nRefs) {
                    int ix = pools->idxX[ref], iy = pools->idxY[ref], iz = pools->idxZ[ref];
                    out->verts[i].x = (ix < pools->nX) ? pools->coordX[ix] : 0;
                    out->verts[i].y = (iy < pools->nY) ? pools->coordY[iy] : 0;
                    out->verts[i].z = (iz < pools->nZ) ? pools->coordZ[iz] : 0;
                } else {
                    out->verts[i].x = out->verts[i].y = out->verts[i].z = 0;
                }
            }
        }
        out->nVerts = count < R3DMESH_MAX_VERTS ? count : R3DMESH_MAX_VERTS;
    } else if (al & 0x7f) { /* explicit inline coords */
        int count = al & 0x7f;
        out->explicitVerts = 1;
        for (int i = 0; i < count; i++) {
            if (p + maskBytes + 6 > limit) return -1;
            p += maskBytes;
            if (i < R3DMESH_MAX_VERTS) {
                out->verts[i].x = rdI16(p);
                out->verts[i].y = rdI16(p + 2);
                out->verts[i].z = rdI16(p + 4);
            }
            p += 6;
        }
        out->nVerts = count < R3DMESH_MAX_VERTS ? count : R3DMESH_MAX_VERTS;
    }

    /* edges */
    if (p >= limit) return -1;
    int nEdges = *p++;
    for (int i = 0; i < nEdges; i++) {
        if (p + maskBytes + 2 > limit) return -1;
        uint32 mask = wide ? (uint32)rdU16(p) | ((uint32)rdU16(p + 2) << 16)
                           : (uint32)rdU16(p);
        p += maskBytes;
        if (i < R3DMESH_MAX_EDGES) {
            out->edges[i].va = p[0];
            out->edges[i].vb = p[1];
            out->edges[i].visMask = mask;
        }
        p += 2;
    }
    out->nEdges = nEdges < R3DMESH_MAX_EDGES ? nEdges : R3DMESH_MAX_EDGES;

    /* primitives */
    if (p >= limit) return -1;
    int cnt = *p++;
    if (cnt == 0) {
        out->consumed = (int)(p - body);
        return out->consumed;
    }
    if (cnt != 0xff) {
        for (int c = 0; c < cnt; c++) {
            if (decodePrimitiveCommand(&p, limit, wide, out) < 0) return -1;
        }
        out->consumed = (int)(p - body);
        return out->consumed;
    }

    /* 0xff: reordered grouped commands. Groups can share command runs, so this
     * may over-collect faces; harmless for a z-buffered GPU draw. The exact body
     * length is the farthest command end across all groups. */
    out->usedRle = 1;
    {
        const uint8 *q = p;    /* just past the 0xff byte */
        int groups = nNormals; /* g_modelEdgeCount in the original */
        const uint8 *coord = q + groups * 2 + 1;
        const uint8 *cnts = coord + groups * 2;
        const uint8 *dataBase = cnts + groups;
        const uint8 *maxEnd = dataBase;
        if (dataBase > limit) return -1;
        for (int g = 0; g < groups; g++) {
            int runCount = cnts[g];
            const uint8 *cp = dataBase + (uint16)rdU16(coord + g * 2);
            for (int r = 0; r < runCount; r++) {
                if (decodePrimitiveCommand(&cp, limit, wide, out) < 0) return -1;
            }
            if (cp > maxEnd) maxEnd = cp;
        }
        p = maxEnd;
    }
    out->consumed = (int)(p - body);
    return out->consumed;
}

int r3dmesh_decode(const uint8 *model, const uint8 *limit,
                   const MeshVtxPools *pools, const uint8 *colorLut,
                   Mesh *out) {
    const uint8 *p = model;
    out->nLods = 0;
    if (p >= limit) return -1;
    p++; /* render-mode byte */

    /* Descend the LOD chain to the finest level. Each header offers a near
     * branch (p+=3, descend = more detail) and a far branch (p+=signed skip,
     * to a sibling/shared header, possibly forward or backward). The finest LOD
     * is the near branch at every header, so we never follow a skip here -- which
     * sidesteps the signed/shared-skip graph entirely. The GPU backend uses the
     * finest LOD always. */
    int bits = -1;
    while (p < limit && (p[0] & 0x80)) {
        bits = p[0] & 7; /* innermost header's distance gate */
        p += 3;
    }
    if (p >= limit) return -1;

    MeshLod *lod = &out->lods[0];
    int consumed = r3dmesh_decodeLod(p, limit, pools, lod);
    if (consumed < 0) return -1;
    lod->gateDistance = (colorLut && bits >= 0) ? rdI16(colorLut + 0x10 + bits * 2) : -1;
    out->nLods = 1;
    return consumed;
}

/* ---- self-test ---------------------------------------------------------- */

static Mesh s_meshScratch; /* large; keep off the stack */

int r3dmesh_selfTest(void) {
    MeshVtxPools pools;
    pools.idxX = buf3d3_1;
    pools.idxY = buf3d3_2;
    pools.idxZ = buf3d3_3;
    pools.coordX = g_replayLog.vertexX;
    pools.coordY = (const int16 *)g_modelVertY;
    pools.coordZ = (const int16 *)g_modelVertZ;
    pools.nRefs = (int)size3d3_3;
    pools.nX = (int)size3d3_4;
    pools.nY = (int)size3d3_5;
    pools.nZ = (int)size3d3_6;

    const uint8 *base = (const uint8 *)g_world3dData;
    const uint8 *limit = base + AIRCRAFT_MODELS_OFFSET; /* main + appended photo models */
    int shapes = (int)size3d3;

    int failed = 0, badIndex = 0, badAnchor = 0;
    int pointShapes = 0, edgerunShapes = 0, modelShapes = 0, rleShapes = 0;
    int totalVerts = 0, totalFaces = 0, totalLines = 0;

    for (int s = 0; s < shapes; s++) {
        const uint8 *model = base + buf3d3[s];
        if (model < base || model >= limit) {
            LogWarn(("r3dmesh: shape %d offset %u out of region (size %u)",
                     s, (unsigned)buf3d3[s], (unsigned)size3d3_2));
            failed++;
            continue;
        }
        if (r3dmesh_decode(model, limit, &pools, colorLut, &s_meshScratch) < 0) {
            LogWarn(("r3dmesh: shape %d (off %u) decode error", s, (unsigned)buf3d3[s]));
            failed++;
            continue;
        }
        MeshLod *l = &s_meshScratch.lods[0]; /* finest LOD */
        if (l->form == MESH_FORM_POINT)
            pointShapes++;
        else if (l->form == MESH_FORM_EDGERUN)
            edgerunShapes++;
        else
            modelShapes++;
        if (l->usedRle) rleShapes++;
        totalVerts += l->nVerts;
        totalFaces += l->nFaces;
        totalLines += l->nLines;

        /* Correctness: every topology reference must be in range. A misaligned
         * parse produces garbage indices, so this is a strong content check. */
        int idxOk = 1;
        for (int e = 0; e < l->nEdges; e++)
            if (l->edges[e].va >= l->nVerts || l->edges[e].vb >= l->nVerts) idxOk = 0;
        for (int f = 0; f < l->nFaces; f++)
            for (int k = 0; k < l->faces[f].nEdges; k++)
                if (l->faces[f].edge[k] >= l->nEdges) idxOk = 0;
        for (int ln = 0; ln < l->nLines; ln++)
            if (l->lines[ln].edge >= l->nEdges) idxOk = 0;
        if (!idxOk) {
            badIndex++;
            LogWarn(("r3dmesh: shape %d out-of-range index", s));
        }

        /* Structural anchor: the finest body must end where the renderer would
         * stop -- on the next LOD header (0x80) or the next shape's data. Shapes
         * are packed in one ascending offset sequence (main .3D3 models then the
         * appended photo/target models), so the next shape's offset is the body
         * boundary; allow up to 1 unread padding byte the renderer never touches.
         * The last shape has no following offset, so it's bounds-checked only. */
        const uint8 *bp = model + 1;
        while (bp < limit && (bp[0] & 0x80)) bp += 3; /* descend to body start */
        const uint8 *end = bp + l->consumed;
        int anchorOk;
        if (s + 1 < shapes && buf3d3[s + 1] > buf3d3[s]) {
            const uint8 *nextShape = base + buf3d3[s + 1];
            anchorOk = (end < limit && (end[0] & 0x80))             /* a LOD header */
                       || end == nextShape || end + 1 == nextShape; /* shape boundary (+pad) */
            if (!anchorOk)
                LogWarn(("r3dmesh: shape %d body end +%d off-anchor (next +%d)",
                         s, l->consumed, (int)(nextShape - bp)));
        } else {
            anchorOk = (end <= limit); /* last/shared shape: in-bounds */
        }
        if (!anchorOk) badAnchor++;
    }

    LogInfo(("r3dmesh self-test: %d shapes (model %d, point %d, edgerun %d, rle %d)",
             shapes, modelShapes, pointShapes, edgerunShapes, rleShapes));
    LogInfo(("  failures: decode %d, out-of-range %d, off-anchor %d",
             failed, badIndex, badAnchor));
    LogInfo(("  geometry totals: verts=%d faces=%d lines=%d",
             totalVerts, totalFaces, totalLines));
    return failed + badIndex + badAnchor;
}
