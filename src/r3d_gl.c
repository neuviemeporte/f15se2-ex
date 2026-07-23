/* OpenGL 1.x 3D backend — the GPU path.
 *
 * Implements the R3DBackend 3D vtable against the *decoded* meshes (r3dmesh.c)
 * and owns the GL context + the 2D-overlay composite. The 3D main view
 * renders through GL at native window resolution with a real depth buffer and
 * double-sided fill (no back-face cull — cheap on a GPU, fixes over-optimized
 * models). The target-model MFD sub-view (renderScene == 0) also renders through
 * GL, into a scissored sub-viewport over a backdrop snapshotted from the page (the
 * game fills that MFD region with a two-tone horizon before submitting the model).
 *
 * Transform faithfulness: rather than re-derive the camera math in float (and
 * risk a matrix-convention bug), each object reuses the exact integer
 * orientation+origin the software path computes (r3d_objTransformFar), and the
 * decoded mesh vertices are projected with a GL matrix built to reproduce the
 * software perspective divide (the >>8 focal scale, 3/4 Y aspect, depth divide).
 * Pixels may drift from the integer rasterizer; behaviour does not (Q1).
 *
 * Compositing: the 2D overlay draws IMMEDIATELY over the GL 3D at native window
 * resolution, in true call order (r3dgl_draw*, driven from r2d_submit*). The
 * retained software page still carries the cockpit/panel + on-change gauges; it is
 * composited once per flight frame as a textured quad at the main-scene anchor
 * (gl_endScene, before the frame's HUD), with the 3D viewport rect(s) made
 * transparent so the GL 3D shows beneath. Pure-2D screens (menus/briefing/debrief)
 * have no 3D pass and composite the page at present instead.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "r3d.h"
#include "r3d_gl.h"
#include "r2d.h"
#include "r3dmesh.h"
#include "r3d_replacement.h"
#include "gfx_impl.h"
#include "eg3dmap.h"
#include "egcode.h"
#include "egdata.h"
#include "egtypes.h"
#include "log.h"

#include <stdlib.h> /* qsort */

/* ---- GL context / selection ------------------------------------------- */

static SDL_Window *s_win;
static SDL_GLContext s_ctx;
static int s_active;

/* glFogCoordf (core GL 1.4) lets us drive fog from an explicit per-vertex distance
 * instead of GL's eye-space distance. We must: the "eye" coords we submit are
 * projection numerators (camX/camY are ~65536x the scale of the depth z we pass), so
 * GL's built-in radial eye-distance fog is dominated by those bogus lateral values
 * and comes out wrong (and heading-dependent). Loaded at context init. */
#ifndef GL_FOG_COORD_SRC
#define GL_FOG_COORD_SRC 0x8450
#endif
#ifndef GL_FOG_COORD
#define GL_FOG_COORD 0x8451
#endif
static void (*s_glFogCoordf)(GLfloat);

int r3dgl_wantGL(void) {
    /* Bring a GL window/context up unless software is explicitly forced — auto,
     * "gl", and unknown names all prefer GL (an unknown name falls back to the
     * preference order, which is GL-first). The probe in r3d_init then claims iff
     * the context actually came up (s_active); if it didn't, gfx_videoInit falls
     * back to an SDL_Renderer and the software backend claims. Shares
     * r3d_requestedBackend so the window decision and the probe agree. */
    const char *want = r3d_requestedBackend();
    return !want || SDL_strcasecmp(want, "software") != 0;
}

/* MSAA multisampling for the GL framebuffer. 4x is effectively free on any desktop
 * GPU and noticeably cleans up the flat-shaded polygon edges and the native-res
 * vector HUD. gfx_impl.c retries with 0 if the context won't come up with it. */
static const int GL_MSAA_SAMPLES = 4;

int r3dgl_msaaSamples(void) { return GL_MSAA_SAMPLES; }

void r3dgl_setGLAttributes(int msaaSamples) {
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    /* 8-bit stencil (D24S8, universal on GL 1.1) — the shadow pass masks each covered
     * pixel so a self-overlapping silhouette blends exactly once. */
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaaSamples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaaSamples > 0 ? msaaSamples : 0);
}

static void gl_imageDestroyed(R2DImage *img); /* drop an image's cached texture on release */

int r3dgl_initContext(SDL_Window *win) {
    s_win = win;
    s_ctx = SDL_GL_CreateContext(win);
    if (!s_ctx) {
        LogCritical(("GL context creation failed: %s", SDL_GetError()));
        return 0;
    }
    SDL_GL_MakeCurrent(win, s_ctx);
    SDL_GL_SetSwapInterval(1);
    s_glFogCoordf = (void (*)(GLfloat))SDL_GL_GetProcAddress("glFogCoordf");
    if (GL_MSAA_SAMPLES > 0) glEnable(GL_MULTISAMPLE); /* no-op if the format has 0 samples */
    {
        GLint depthBits = 0, stencilBits = 0, samples = 0;
        glGetIntegerv(GL_DEPTH_BITS, &depthBits);
        glGetIntegerv(GL_STENCIL_BITS, &stencilBits);
        glGetIntegerv(GL_SAMPLES, &samples);
        LogInfo(("GL: %s / %s, depth bits=%d, stencil bits=%d, MSAA samples=%d",
                 (const char *)glGetString(GL_RENDERER),
                 (const char *)glGetString(GL_VERSION), (int)depthBits, (int)stencilBits, (int)samples));
    }
    s_active = 1;
    r2d_registerImageDestroy(gl_imageDestroyed);
    return 1;
}

int r3dgl_active(void) { return s_active; }

/* ---- mesh decode -------------------------------------------------------- */

/* Decoded once per submit into this scratch (the parse is cheap and a fresh
 * decode is always correct across region reloads, so no per-pointer cache). */
static Mesh s_mesh;

/* Per-face edge-adjacency scratch for ordering a face's boundary into a loop. */
static uint8 s_nbr[R3DMESH_MAX_VERTS][2];
static uint8 s_nbrN[R3DMESH_MAX_VERTS];

static void fillPools(MeshVtxPools *pools) {
    pools->idxX = buf3d3_1;
    pools->idxY = buf3d3_2;
    pools->idxZ = buf3d3_3;
    pools->coordX = g_replayLog.vertexX;
    pools->coordY = (const int16 *)g_modelVertY;
    pools->coordZ = (const int16 *)g_modelVertZ;
    pools->nRefs = (int)size3d3_3;
    pools->nX = (int)size3d3_4;
    pools->nY = (int)size3d3_5;
    pools->nZ = (int)size3d3_6;
}

#ifdef DEBUG
int r3dgl_testLegacyShapeStats(const unsigned char *legacyModel,
                               size_t legacyModelSize,
                               R3DLegacyShapeStats *stats) {
    MeshVtxPools pools{};
    Mesh decoded{};
    MeshLod *lod{};

    if (!legacyModel || !stats || legacyModelSize == 0 ||
        legacyModelSize > WORLD3D_DATA_SIZE) {
        return 0;
    }
    memset(stats, 0, sizeof(*stats));
    memset(g_world3dData, 0, WORLD3D_DATA_SIZE);
    memcpy(g_world3dData, legacyModel, legacyModelSize);
    fillPools(&pools);
    if (r3dmesh_decode((const uint8 *)g_world3dData,
                       (const uint8 *)g_world3dData + legacyModelSize,
                       &pools, colorLut, &decoded) < 0) {
        return 0;
    }
    lod = &decoded.lods[0];
    stats->form = lod->form;
    if (lod->form == MESH_FORM_POINT) {
        stats->renderable = 1;
        stats->points = 1;
        stats->pointColors[lod->pointColor] = 1;
        return 1;
    }
    if (lod->form == MESH_FORM_EDGERUN) {
        stats->renderable = lod->nRunRefs > 0;
        stats->points = lod->nRunRefs;
        stats->pointColors[lod->pointColor] = lod->nRunRefs;
        return 1;
    }
    if (lod->form != MESH_FORM_MODEL) return 1;

    for (int i = 0; i < lod->nFaces; ++i) {
        MeshFace *face = &lod->faces[i];
        if (face->nEdges < 3 || face->colorByte == 0xff) continue;
        const int triangleCount = (int)face->nEdges - 2;
        stats->renderable = 1;
        stats->triangles += triangleCount;
        stats->faceColors[face->colorByte] += triangleCount;
        for (int edgeIndex = 0; edgeIndex < face->nEdges; ++edgeIndex) {
            MeshEdge *edge = &lod->edges[face->edge[edgeIndex]];
            if (edge->va == edge->vb) {
                ++stats->points;
                ++stats->pointColors[face->colorByte];
            } else {
                ++stats->maximumLines;
                ++stats->maximumLineColors[face->colorByte];
            }
        }
    }
    for (int i = 0; i < lod->nLines; ++i) {
        MeshLine *line = &lod->lines[i];
        MeshEdge *edge = &lod->edges[line->edge];
        stats->renderable = 1;
        if (edge->va == edge->vb) {
            ++stats->points;
            ++stats->pointColors[line->colorByte];
        } else {
            ++stats->minimumLines;
            ++stats->maximumLines;
            ++stats->minimumLineColors[line->colorByte];
            ++stats->maximumLineColors[line->colorByte];
        }
    }
    return 1;
}
#endif

/* ---- scene state -------------------------------------------------------- */

static int s_sceneRendered;  /* a GL 3D view was drawn this frame (live under the present) */
static int s_mainScene;      /* the current scene is the main out-the-window 3D view (not an
                              * MFD target sub-scene) — gates the anchor page composite */
static int s_glFlightLive;   /* the last present carried a live 3D flight frame (so a bare
                              * gfx_repaint must not blank it by re-presenting only the page) */
static int s_pageComposited; /* the page backdrop was already laid down this frame (pure-2D
                              * screens composite it at r3dgl_beginOverlay, under the immediate
                              * overlay) so the present must not composite it again on top */
static int s_wide = -1;      /* widescreen 3D (Hor+): -1 = not yet read from F15_WIDESCREEN */

/* The 3D-viewport rect(s) the anchor page composite makes transparent so the GL 3D
 * shows beneath the retained cockpit/panel — registered by the main out-the-window
 * view (gl_beginScene). Keyed on the rect, not on any page pixel value, so no page
 * fill is needed: nothing 2D is baked into the viewport region on GL (the HUD there
 * is drawn immediately over the 3D). The MFD target sub-view needs none — its
 * two-tone horizon is drawn immediately as its backdrop. Consumed by the composite. */
#define MAX_SHOW_RECTS 4
static SDL_Rect s_showRects[MAX_SHOW_RECTS];
static int s_nShowRects;

static void addShowRect(int x, int y, int w, int h) {
    if (s_nShowRects >= MAX_SHOW_RECTS) return;
    s_showRects[s_nShowRects].x = x;
    s_showRects[s_nShowRects].y = y;
    s_showRects[s_nShowRects].w = w;
    s_showRects[s_nShowRects].h = h;
    s_nShowRects++;
}

/* Is page-space pixel (x,y) inside any show-through rect? */
static int inShowRect(int x, int y) {
    int i;
    for (i = 0; i < s_nShowRects; i++) {
        const SDL_Rect *r = &s_showRects[i];
        if (x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h)
            return 1;
    }
    return 0;
}
static float s_proj[16];     /* column-major GL projection for the active scene */
static int s_sceneVp[4];     /* window-px viewport/scissor of the active 3D scene, for
                              * re-establishing the scene state in gl_endScene */
static float s_pixelScale;   /* letterbox scale: window pixels per 320-space pixel (point size) */
static float s_vpW, s_vpH;   /* active 3D viewport size in window pixels (screen-span tests) */

/* Deferred submission for the hybrid depth ordering (see gl_endScene). A real
 * z-buffer (GL_LEQUAL) resolves genuine occlusion painter's order alone got wrong
 * (a plane behind a building does not show through); the objects are still
 * collected and drawn in the original's painter's order — farthest-first by the
 * LOD-normalized origin depth (insertSortedObject's key), faces in display-list
 * order — so that surfaces a z-buffer cannot separate (paper-thin
 * coplanar faces: jet fire on the engine, surf on the sea, deck markings) keep the
 * original look, the later draw winning at equal depth. */
typedef struct {
    char *model;
    int shapeId;
    const char *containerLegacyName;
    int16 combined[9];
    long camBase, camX, camY; /* camera-space origin axes (screen-X, screen-Y, depth) */
    int shade, colorBase, curLod;
    int posZ;             /* object world altitude (0 = ground/sea), for wire ground test */
    int sortHi, sortLo;   /* normalized origin depth (sort key, farthest = largest) */
    int immediate;        /* flat ground/sea (posZ==0, no sort flag): drawn first/behind */
    int shadow;           /* aircraft ground shadow: flattened onto the ground, drawn translucent */
    int seq;              /* submission index; preserves walk order among immediate objects */
} GlSub;
#define GL_MAX_SUBS 4096
static GlSub s_subs[GL_MAX_SUBS];
static int s_nSub;
static int s_subOverflow;

/* World-space 3D line segments (cannon tracers, explosion sparks) submitted this
 * frame; drawn as camera-facing ribbons (z-tested + fogged) at the end of the
 * scene. Endpoints are the scene camera-space (screen-X axis, screen-Y axis,
 * depth) triples; color is a final palette index. */
typedef struct {
    long baseXA, camXA, camYA;
    long baseXB, camXB, camYB;
    int color;
} GlLine;
#define GL_MAX_LINES 256
static GlLine s_lines[GL_MAX_LINES];
static int s_nLine;
static int s_lineOverflow;

/* INDEX8 -> RGBA8888 conversion scratch, shared by the page composite, the sprite
 * textures and the sub-view backdrop snapshot. */
static uint8 *s_rgba;
static int s_rgbaCap;
static uint8 *ensureRgbaScratch(int need) {
    if (need > s_rgbaCap) {
        SDL_free(s_rgba);
        s_rgba = (uint8 *)SDL_malloc(need);
        s_rgbaCap = s_rgba ? need : 0;
    }
    return s_rgba;
}

/* Map a final palette index to GL colour. */
static void glColorIndex(int idx) {
    uint8 r, g, b;
    gfx_paletteRGB(idx & 0xff, &r, &g, &b);
    glColor3ub(r, g, b);
}

/* Distance-shaded point colour for the edge-run path (edgeRunColor, eg3drast.c). */
static int glEdgeRunColor(int depthHi) {
    int bx = depthHi > 5000 ? 8 : depthHi > 2500 ? 7 : 0xf;
    return colorLut[bx];
}

/* 3D wire ribbon half-width as a function of the wire's world length (`len`, which
 * is proportional to true world length with one global constant — the rotation
 * matrices are Q15 and LOD scale is divided out before len, so it is the same for
 * a banking plane and flat terrain). Width must grow SUBLINEARLY with length: a
 * 40cm antenna has to be relatively fat to be visible, while a km-long road must
 * stay thin (width-per-length spans ~60x across antenna->road). A constant or
 * affine law can't bend that way; a power law can. Anchored on the two measured
 * features — antenna (len~2k -> ~250) and road (len~47M -> ~100k) — POW lands ~0.6,
 * SCALE ~2.5, and the same curve also hits masts/beams (~7k) and ridges (~24k).
 * POW < 1 is the sublinear knee; SCALE sets overall thickness. */
static const float WIRE_HW_SCALE = 2.5f;
static const float WIRE_HW_POW   = 0.6f;

/* A wire whose two endpoints sit at (nearly) the same WORLD altitude is a ground
 * feature — a road, a runway marking, an outline on a ship deck. Those must lie
 * FLAT on the ground (extruded within the horizontal plane) instead of facing the
 * camera, or they billboard upright like a row of fences as the camera banks.
 * A steeper wire (antenna, mast, building edge) keeps the camera-facing ribbon so
 * it reads as a line from any angle. WIRE_FLAT_VERT is the cutoff on |dot(lineDir,
 * worldUp)|: 0 = perfectly horizontal, 1 = vertical; below the cutoff we lay flat. */
static const float WIRE_FLAT_VERT = 0.00030f;  /* lay flat when within ~17deg of horizontal */

/* Peek a shape's finest-LOD sort flag (the 0x40 bit on the body's first opcode).
 * projectSceneObject draws an object immediately (behind the sorted queue) only
 * when this bit is clear and the object sits at world Z=0 (flat ground/sea); the
 * GL backend reproduces that order so flat terrain never paints over elevated
 * objects. Descends to the finest LOD to match what the GL path actually renders. */
static int peekSortFlag(const uint8 *model, const uint8 *limit) {
    const uint8 *p = model;
    if (p >= limit) return 0;
    p++; /* render-mode byte */
    while (p < limit && (p[0] & 0x80)) p += 3; /* descend to the finest LOD body */
    if (p >= limit) return 0;
    if ((p[0] & 0x60) == 0x60) p++; /* storeObjTransform byte precedes the opcode */
    if (p >= limit) return 0;
    return (p[0] & 0x40) ? 1 : 0;
}

/* A model is "flat" (a ground decal: sea, surf, road, runway/deck marking) when its
 * geometry has no real vertical relief — it is planar, so it sits coplanar with the
 * ground and the z-buffer cannot order it, but draw order can. A model with relief
 * (a cargo ship and its bridge, a building) is NOT flat: it must be z-buffered so its
 * own near faces occlude its far ones.
 *
 * Test: a ground decal is genuinely PLANAR — one model-space axis collapses to ~0
 * (the world data models the sea, terrain tiles, roads and deck markings with a span
 * of exactly 0 on their vertical axis). A solid has real extent on all three: the
 * cargo ship measures 384x36x24, so its thinnest axis (24) sits well above any planar
 * shape. Flat iff the thinnest axis span is at/near zero — a small absolute slack
 * tolerates minor modelling noise while staying far below the ship's 24. (A ratio
 * test fails here: the ship is ~16x longer than tall, so any ratio loose enough to
 * pass real sheets also catches it.) */
static const int GL_FLAT_SPAN = 8;

/* Relief is a static property of the model, but a frame has many instances of a few
 * models (terrain tiles), so cache the verdict per model pointer; reset each frame
 * (the world data can reload between regions). */
static const char *s_flatPtr[256];
static uint8 s_flatVal[256];
static int s_flatN;

static int modelIsFlat(const char *model) {
    int i, minx, maxx, miny, maxy, minz, maxz, sx, sy, sz, smin, flat;
    MeshVtxPools pools;
    MeshLod *l;
    for (i = 0; i < s_flatN; i++)
        if (s_flatPtr[i] == model) return s_flatVal[i];

    fillPools(&pools);
    flat = 0; /* undecodable / non-model -> treat as solid (z-buffer it; the safe side) */
    if (r3dmesh_decode((const uint8 *)model,
                       (const uint8 *)g_world3dData + WORLD3D_DATA_SIZE,
                       &pools, colorLut, &s_mesh) >= 0) {
        l = &s_mesh.lods[0];
        if (l->form == MESH_FORM_MODEL && l->nVerts > 0) {
            minx = maxx = l->verts[0].x;
            miny = maxy = l->verts[0].y;
            minz = maxz = l->verts[0].z;
            for (i = 1; i < l->nVerts; i++) {
                int X = l->verts[i].x, Y = l->verts[i].y, Z = l->verts[i].z;
                if (X < minx) minx = X;
                if (X > maxx) maxx = X;
                if (Y < miny) miny = Y;
                if (Y > maxy) maxy = Y;
                if (Z < minz) minz = Z;
                if (Z > maxz) maxz = Z;
            }
            sx = maxx - minx; sy = maxy - miny; sz = maxz - minz;
            smin = sx; if (sy < smin) smin = sy; if (sz < smin) smin = sz;
            flat = (smin <= GL_FLAT_SPAN);
        }
    }
    if (s_flatN < 256) { s_flatPtr[s_flatN] = model; s_flatVal[s_flatN++] = (uint8)flat; }
    return flat;
}

/* Atmospheric haze. The original colours each object by distance through 8 discrete
 * palette bands (g_objShade = (v<<4)+0x80, added to colorLut[colorByte]): the near
 * band 0x80 is the saturated true colour, and with distance every hue brightens and
 * desaturates toward a light grey — but in coarse, visible steps. GL_FOG reproduces
 * the same effect per-pixel and smooth: draw with the near/saturated base colour
 * (colorLut[c] + GL_NEAR_SHADE) and let fog blend it toward the live horizon colour
 * with distance, sampled from the palette each frame (so night/region repalettes follow
 * automatically). We fog toward the GROUND side of the horizon (ground ramp base, 0x70),
 * not the sky side (0x60): most fogged geometry is terrain and must melt into the earth
 * band of the horizon sphere. By day the two meet at the same haze grey, but at dusk/
 * night the sky side carries the sunset glow while the earth is dark — fogging to 0x60
 * would wrongly brighten distant ground.
 *
 * Fog distance is supplied explicitly per vertex (fogVertex below) as the forward
 * depth z we already emit — the LOD-normalized eye depth (vd_), which the z-buffer
 * also uses and which equals true distance across LODs (so it is heading-independent).
 * We can NOT let GL derive the distance from the submitted "eye" position: camX/camY
 * are projection numerators ~65536x the scale of that z, so GL's radial eye-distance
 * fog is swamped by them and comes out wrong (and heading-dependent). Range is in
 * those vd_ forward-depth units (1..fGate, the projection's near..far). */
static const int GL_NEAR_SHADE = 0x80;  /* the v=0 saturated band: the un-hazed colour */
static const int GL_HORIZON_IDX = 0x70; /* ground-at-horizon (earth side); see above */
/* Falloff curve. GL_LINEAR ramps straight from FOG_NEAR_DIST (clear) to FOG_FAR_DIST
 * (full haze). GL_EXP / GL_EXP2 ignore near/far and use FOG_DENSITY instead, giving an
 * exponential curve — clearer up close, thickening with distance (EXP2 is the steeper,
 * more "atmospheric" of the two). Switch FOG_MODE to taste; tune the matching knob. */
static const GLint FOG_MODE = GL_EXP;
static const float FOG_NEAR_DIST = 0.0f;    /* GL_LINEAR: haze onset, vd_ forward-depth units */
static const float FOG_FAR_DIST = 6000.0f;  /* GL_LINEAR: fully hazed (horizon) at this depth */
static const float FOG_DENSITY = 0.0003f;   /* GL_EXP/EXP2: higher = haze closes in sooner */
//static const float FOG_DENSITY = 0.0003f;   /* GL_EXP/EXP2: higher = haze closes in sooner */

/* Emit one vertex with its fog distance = its forward depth z (planar), bypassing GL's
 * eye-distance derivation. Falls back to a plain vertex if glFogCoordf is unavailable. */
static void fogVertex(float x, float y, float z) {
    if (s_glFogCoordf) s_glFogCoordf(z);
    glVertex3f(x, y, z);
}

static const char *gl_name(void) { return "opengl1"; }

static int gl_init(void) { return s_active; } /* claims iff the context came up */
static void gl_shutdown(void) { r3dReplacementShutdown(); }

static R3DMesh gl_registerMesh(R3DMesh raw) { return raw; } /* decoded per submit */
static void gl_releaseMesh(R3DMesh mesh) { (void)mesh; }

/* Build the GL projection matrix that reproduces projectVertexToScreen for a
 * viewport of Wv x Wh (320-space) with principal point (cx, cy) and a far depth
 * gate. The submitted vertex is camera-space (camX, camY, depth/65536). */
static void buildProjection(int Wv, int Hv, int cx, int cy, float fGate) {
    float ax = 1.0f / (128.0f * Wv);
    float az = 2.0f * (float)cx / Wv - 1.0f;
    /* Y focal boosted from the original 3/(512 Hv) to 5/(768 Hv): the original baked
     * a 3/4 grid Y-aspect (a DOS cost-saving) that left 3D ~10% wide even after the
     * 1.2 aspect-corrected present. 5/6 grid x 1.2 present = geometrically round. */
    float by = 5.0f / (768.0f * Hv);
    float bz = 1.0f - 2.0f * (float)cy / Hv;
    /* Depth: NDC_z = -1 at dNear, +1 at fGate (w = depth, ~= world forward
     * distance). Keep dNear as large as the closest geometry allows: with a tiny
     * dNear nearly the whole depth range is wasted on the empty 0..dNear gap and
     * distant terrain/aircraft collapse to the same depth (objects "sink into the
     * ground" at range). 1 world unit is far closer than anything the near cull
     * lets through, so it is safe and recovers far precision. */
    float dNear = 1.0f;
    float zb = -2.0f / (1.0f / dNear - 1.0f / fGate);
    float za = -1.0f - zb / dNear;
    float *m = s_proj;
    m[0] = ax; m[4] = 0;  m[8]  = az; m[12] = 0;
    m[1] = 0;  m[5] = by; m[9]  = bz; m[13] = 0;
    m[2] = 0;  m[6] = 0;  m[10] = za; m[14] = zb;
    m[3] = 0;  m[7] = 0;  m[11] = 1;  m[15] = 0;
}

/* Draw one sphere ring quad (4 screen-space points, viewport-local 320-space)
 * as a vertical gradient: the near edge (pts 0/1 = the ring's own rear+fore)
 * gets palNear, the far edge (pts 2/3 = the next ring's fore+rear) gets palFar.
 * GL_SMOOTH then interpolates between the two adjacent ramp colours so the
 * horizon reads as a continuous gradient instead of solid stepped bands. */
static void sphereQuadGrad(const float *pts, int palNear, int palFar) {
    glBegin(GL_QUADS);
    glColorIndex(palNear); glVertex2f(pts[0], pts[1]);
    glColorIndex(palNear); glVertex2f(pts[2], pts[3]);
    glColorIndex(palFar);  glVertex2f(pts[4], pts[5]);
    glColorIndex(palFar);  glVertex2f(pts[6], pts[7]);
    glEnd();
}

/* Float counterpart of fixedMulQ14 (round((a*b)>>15)): the sphere edge vertices
 * are computed in float and never snapped to the 320-space integer grid, so the
 * horizon stays sub-pixel-stable when this viewport is upscaled to the window.
 * (The software rasterizer must round to whole pixels; GL interpolates, so it
 * doesn't — leaving the horizon to jitter in ~scale-pixel steps as you manoeuvre.
 * Only the background sphere diverges to float; the 3D objects keep the integer
 * transform that the depth sort depends on.) */
static float fmulQ15(float a, float b) { return a * b * (1.0f / 32768.0f); }

/* GL horizon/sky background — a port of drawProjectionSphere (egsphere.c). The
 * software path stacks screen-space ring quads whose between-ring edges step
 * through the sky ramp (palette 0x60+) and ground ramp (0x70+); here each ring is
 * a flat GL quad filled from that ramp, drawn in an ortho viewport with depth off
 * so the 3D objects always composite in front (matching the original's draw-first,
 * no-Z background). Runs only at detail >= 3; below that a flat clear stands in. */
static void glDrawSphere(float oLeft, float oRight, float oBottom, float oTop) {
    float rearX[17], rearY[17], foreX[17], foreY[17], facePts[8];
    int ringIx;
    float ringRad, radiusScale, i, j;
    /* Ring Y is g_viewCenterY - (1 - vAspectK)*(pitch/roll term): the vertical
     * grid-aspect must match the 3D terrain so the gradient horizon doesn't drift
     * from the terrain horizon. buildProjection projects the GL world with a 5/6
     * vertical aspect, so vAspectK = 1/6 here (1 - 1/6 = 5/6); the software sphere
     * (egsphere.c) uses >>2 = 1/4 → 3/4, matching the software raster's 3/4 world. */
    const float vAspectK = 1.0f / 6.0f;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* y-down, in 320-space virtual coords. In widescreen the extents run past
     * 0..Wv / 0..Hv so the horizon spans the full window with the same central
     * mapping as the 3D objects; in 4:3 they are exactly the viewport-local box. */
    glOrtho(oLeft, oRight, oBottom, oTop, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glShadeModel(GL_SMOOTH); /* gradient across each ring; restored to flat below */

    for (ringIx = 0; ringIx < 16; ringIx++)
        g_sphereRingRadii[ringIx] = g_sphereRingTable[ringIx];
    g_sphereTiltZ = -g_spherePitch;
    /* Kept in float (the software path truncates to int): radiusScale ~= 0 when
     * the horizon sits at screen centre, so its integer step there is the whole
     * horizon displacement — a one-pixel pop as the view crosses the horizon. */
    radiusScale = ((float)g_sphereRadius * 256.0f) /
                  (float)(g_sphereDistZ < 0x200 ? 0x200 : g_sphereDistZ);
    if (g_extraScaleShift != 0) radiusScale *= (float)(1 << g_extraScaleShift);
    if (g_halfScaleRender != 0) radiusScale *= 0.5f;

    for (ringIx = 0; ringIx < 17; ringIx++) {
        ringRad = (ringIx < 16) ? (float)g_sphereRingRadii[ringIx] + radiusScale : (float)0x5848;
        i = fmulQ15(-0x5848, g_sphereRoll);
        j = fmulQ15(ringRad, g_sphereTiltZ);
        rearX[ringIx] = (g_viewCenterX + i) - j;
        foreX[ringIx] = -i + g_viewCenterX - j;
        i = fmulQ15(ringRad, g_sphereRoll);
        j = fmulQ15(-0x5848, g_sphereTiltZ);
        rearY[ringIx] = -(-(((i + j) * vAspectK) - i) + j) + g_viewCenterY;
        foreY[ringIx] = (((i - j) * vAspectK) + g_viewCenterY) - i + j;
    }
    for (ringIx = 0; ringIx < 16; ringIx++) {
        facePts[0] = rearX[ringIx];     facePts[1] = rearY[ringIx];
        facePts[2] = foreX[ringIx];     facePts[3] = foreY[ringIx];
        facePts[4] = foreX[ringIx + 1]; facePts[5] = foreY[ringIx + 1];
        facePts[6] = rearX[ringIx + 1]; facePts[7] = rearY[ringIx + 1];
        /* sky ramp: blend this band's colour toward the next band's */
        sphereQuadGrad(facePts, 0x60 + ringIx, 0x60 + (ringIx < 15 ? ringIx + 1 : 15));
    }

    g_sphereRingRadii[0] = g_viewPosZ / 0x200;
    for (ringIx = 1; ringIx < 16; ringIx++)
        g_sphereRingRadii[ringIx] = g_viewPosZ / ((16 - ringIx) * 0x20) - g_sphereRingRadii[0];
    g_sphereRingRadii[0] = 0;
    for (ringIx = 0; ringIx < 17; ringIx++) {
        ringRad = (ringIx < 16) ? radiusScale - (float)g_sphereRingRadii[ringIx] : (float)-0x5848;
        i = fmulQ15(-0x5848, g_sphereRoll);
        j = fmulQ15(ringRad, g_sphereTiltZ);
        rearX[ringIx] = (g_viewCenterX + i) - j;
        foreX[ringIx] = -i + g_viewCenterX - j;
        i = fmulQ15(ringRad, g_sphereRoll);
        j = fmulQ15(-0x5848, g_sphereTiltZ);
        rearY[ringIx] = -(-(((i + j) * vAspectK) - i) + j) + g_viewCenterY;
        foreY[ringIx] = (((i - j) * vAspectK) + g_viewCenterY) - i + j;
    }
    for (ringIx = 0; ringIx < 16; ringIx++) {
        facePts[0] = rearX[ringIx];     facePts[1] = rearY[ringIx];
        facePts[2] = foreX[ringIx];     facePts[3] = foreY[ringIx];
        facePts[4] = foreX[ringIx + 1]; facePts[5] = foreY[ringIx + 1];
        facePts[6] = rearX[ringIx + 1]; facePts[7] = rearY[ringIx + 1];
        /* ground ramp: blend this band's colour toward the next band's */
        sphereQuadGrad(facePts, 0x70 + ringIx, 0x70 + (ringIx < 15 ? ringIx + 1 : 15));
    }
    glShadeModel(GL_FLAT);
}

/* Common per-model GL state for a 3D scene (main view and target sub-view): the
 * perspective projection, double-sided flat fill, and the hybrid depth setup
 * (z-buffer + GL_LEQUAL + polygon offset — see gl_endScene). */
static void beginModelPass(void) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(s_proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_CULL_FACE); /* double-sided per docs */
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glShadeModel(GL_FLAT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glEnable(GL_POLYGON_OFFSET_POINT);
}

/* Target-model MFD sub-view (renderScene == 0). The game's two-tone horizon backdrop
 * is drawn immediately (fillSpanRectImmediate, egmath.c) into the MFD region just
 * before this, over the anchor page composite — so the framebuffer already holds the
 * MFD backdrop. We just render the model over it in a scissored sub-viewport, with
 * only this region's depth cleared so the main 3D already in the framebuffer (and the
 * immediate horizon) are preserved; the model's blips/labels then draw over it. */
static void gl_beginSubScene(const R3DScene *s) {
    int win_w, win_h, vpTop, vpBot, vpLeft, vpRight, Wv, Hv, lbx, lby, gx, gy, gw, gh;
    float scaleX, scaleY, fGate;

    r3d_setObjCullWiden(1, 1, 1, 1); /* the MFD is inside the pillarboxed 4:3 UI box */
    setup3DTransform(s->viewport, s->angleX, s->angleY, s->angleZ,
                     s->posX, s->posY, s->posZ, 0);

    vpTop = s->viewport[7];
    vpBot = s->viewport[8];
    vpLeft = s->viewport[9];
    vpRight = s->viewport[10];
    Wv = vpRight - vpLeft + 1;
    Hv = vpBot - vpTop + 1;
    if (Wv < 1) Wv = 1;
    if (Hv < 1) Hv = 1;

    fGate = (float)(*(int16 *)(colorLut + 0x20));
    if (fGate < 2.0f) fGate = 8192.0f;
    buildProjection(Wv, Hv, g_viewCenterX, g_viewCenterY, fGate);
    /* The target view magnifies the model by dividing the perspective depth by
     * 2^g_extraScaleShift (projectVertexToScreen); reproduce that as a matching
     * boost to the projection's x/y focal terms. g_halfScaleRender halves it. */
    {
        float sc = 1.0f;
        if (g_extraScaleShift) sc *= (float)(1 << g_extraScaleShift);
        if (g_halfScaleRender) sc *= 0.5f;
        s_proj[0] *= sc;
        s_proj[5] *= sc;
    }

    SDL_GetWindowSizeInPixels(s_win, &win_w, &win_h);
    {
        R2DMapping m;
        r2d_computeMapping(LOGICAL_WIDTH, LOGICAL_HEIGHT, win_w, win_h, 0, &m);
        scaleX = m.scaleX;
        scaleY = m.scaleY;
        lbx = m.offX;
        lby = m.offY;
    }
    s_pixelScale = scaleX;
    gx = lbx + (int)(vpLeft * scaleX);
    gw = (int)(Wv * scaleX);
    gh = (int)(Hv * scaleY);
    gy = win_h - (lby + (int)(vpTop * scaleY)) - gh;

    glViewport(gx, gy, gw, gh);
    glScissor(gx, gy, gw, gh);
    glEnable(GL_SCISSOR_TEST);
    s_sceneVp[0] = gx;
    s_sceneVp[1] = gy;
    s_sceneVp[2] = gw;
    s_sceneVp[3] = gh;
    s_vpW = (float)gw;
    s_vpH = (float)gh;

    /* Clear only this region's depth (color preserved: the immediate horizon backdrop
     * already drawn here stays) so the model z-buffers against a clean slate over the
     * main-view depth. */
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);

    beginModelPass();
    s_nSub = 0;
    s_subOverflow = 0;
    s_nLine = 0;
    s_lineOverflow = 0;
    s_flatN = 0;
    s_mainScene = 0; /* this is the MFD target sub-scene, not the main view */
    s_sceneRendered = 1; /* live 3D is now in the framebuffer; the present must not clear it */
}

static void gl_beginScene(const R3DScene *s) {
    int win_w, win_h, vpTop, vpBot, vpLeft, vpRight, Wv, Hv, lbx, lby;
    float scaleX, scaleY, fGate, sphOrtho[4];
    int16 skyIdx;

    /* The target-model MFD sub-view has its own scissored GL path (model over the
     * immediate horizon backdrop), separate from the main view's full sky/fog setup. */
    if (s->renderScene == 0) {
        gl_beginSubScene(s);
        return;
    }
    /* This is a flight 3D frame: its HUD/MFD line & point submissions draw
     * immediately at native resolution. The page backdrop is composited mid-frame
     * at the gl_endScene anchor (after the 3D, before the HUD), so don't compose it
     * here. */
    r2d_vectorBeginFrame(R2D_COMPOSE_NONE);

    if (s_wide < 0) {
        /* Widescreen 3D (Hor+): on by default; F15_WIDESCREEN=0 forces 4:3. The
         * stock UI stays a centred, pillarboxed 320x200 image (it never widens);
         * only the 3D fills the extra width. */
        const char *e = SDL_getenv("F15_WIDESCREEN");
        s_wide = (e && (e[0] == '0' || e[0] == 'n' || e[0] == 'N')) ? 0 : 1;
        LogInfo(("r3d_gl: widescreen 3D %s", s_wide ? "on" : "off"));
    }

    /* Reuse the software scene setup minus the sphere / shared-vertex precompute
     * (renderScene = 0 here): view matrix + position + viewport + spin advance +
     * sort reset. The GL submit reads g_viewRotMatrix / g_viewPos* indirectly via
     * r3d_objTransformFar. */
    setup3DTransform(s->viewport, s->angleX, s->angleY, s->angleZ,
                     s->posX, s->posY, s->posZ, 0);

    vpTop = s->viewport[7];
    vpBot = s->viewport[8];
    vpLeft = s->viewport[9];
    vpRight = s->viewport[10];
    Wv = vpRight - vpLeft + 1;
    Hv = vpBot - vpTop + 1;
    if (Wv < 1) Wv = 1;
    if (Hv < 1) Hv = 1;

    fGate = (float)(*(int16 *)(colorLut + 0x20));
    if (fGate < 2.0f) fGate = 8192.0f;
    buildProjection(Wv, Hv, g_viewCenterX, g_viewCenterY, fGate);

    SDL_GetWindowSizeInPixels(s_win, &win_w, &win_h);
    /* The 320x200 UI box maps to the window through the shared r2d letterbox so
     * the 3D viewport stays aligned with the pillarboxed HUD. */
    {
        R2DMapping m;
        r2d_computeMapping(LOGICAL_WIDTH, LOGICAL_HEIGHT, win_w, win_h, 0, &m);
        scaleX = m.scaleX;
        scaleY = m.scaleY;
        lbx = m.offX;
        lby = m.offY;
    }
    s_pixelScale = scaleX;

    /* Clear the whole window (including the letterbox bars) to black; the 3D
     * viewport region is re-cleared to the sky colour once scissored below. */
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    /* GL viewport origin is bottom-left; the 320-space rect is top-down. The
     * uniform letterbox scale keeps the same proportions as 640x400 at any size.
     * gx/gw/gy/gh is the centred 4:3 sub-rect the letterboxed UI quad occupies. */
    {
        int gx = lbx + (int)(vpLeft * scaleX);
        int gw = (int)(Wv * scaleX);
        int gh = (int)(Hv * scaleY);
        int gy = win_h - (lby + (int)(vpTop * scaleY)) - gh;
        if (s_wide) {
            /* Render the 3D across the WHOLE window and remap the projection so the
             * central 320-space region still lands on exactly the gx/gw/gy/gh pixels
             * (px = C0 + C1*camX/depth preserved, extended linearly outside). The
             * keyed windscreen of the centred UI quad therefore stays aligned with
             * the 3D behind it, while the side gaps — uncovered by the UI quad in
             * present — reveal more world. At 4:3 (gw==win_w, gh==win_h) the remap
             * is the identity, so nothing changes. */
            float ax = s_proj[0], az = s_proj[8], by = s_proj[5], bz = s_proj[9];
            float C0 = (float)gx + 0.5f * gw * (1.0f + az);
            float D0 = (float)gy + 0.5f * gh * (1.0f + bz);
            s_proj[0] = ax * gw / win_w;
            s_proj[8] = 2.0f * C0 / win_w - 1.0f;
            s_proj[5] = by * gh / win_h;
            s_proj[9] = 2.0f * D0 / win_h - 1.0f;
            glViewport(0, 0, win_w, win_h);
            glScissor(0, 0, win_w, win_h);
            s_vpW = (float)win_w;
            s_vpH = (float)win_h;
            /* Widen the object frustum cull to the same view cone the projection
             * now covers, so peripheral buildings/terrain models in the side gaps
             * are fetched instead of culled at the 4:3 boundary. */
            r3d_setObjCullWiden(win_w, gw, win_h, gh);
            /* Sphere ortho spanning the full window with the same central mapping:
             * window x=gx -> virtual 0, x=gx+gw -> virtual Wv (scale px/virtual). */
            sphOrtho[0] = -(float)gx / scaleX;                                  /* left  */
            sphOrtho[1] = sphOrtho[0] + (float)win_w / scaleX;                  /* right */
            sphOrtho[3] = -((float)lby + (float)vpTop * scaleY) / scaleY;       /* top   */
            sphOrtho[2] = ((float)win_h - ((float)lby + (float)vpTop * scaleY)) / scaleY; /* bottom */
        } else {
            glViewport(gx, gy, gw, gh);
            glScissor(gx, gy, gw, gh);
            s_vpW = (float)gw;
            s_vpH = (float)gh;
            r3d_setObjCullWiden(1, 1, 1, 1); /* 4:3: original cull */
            sphOrtho[0] = 0.0f; sphOrtho[1] = (float)Wv;
            sphOrtho[2] = (float)Hv; sphOrtho[3] = 0.0f;
        }
        if (s_wide) {
            s_sceneVp[0] = 0; s_sceneVp[1] = 0;
            s_sceneVp[2] = win_w; s_sceneVp[3] = win_h;
        } else {
            s_sceneVp[0] = gx; s_sceneVp[1] = gy;
            s_sceneVp[2] = gw; s_sceneVp[3] = gh;
        }
    }
    glEnable(GL_SCISSOR_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(s_proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_CULL_FACE); /* double-sided per docs */
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glShadeModel(GL_FLAT);

    /* Clear to the flat sky colour first (render3DView resolved the sky palette
     * index into viewport[2]); the gradient sphere then overdraws the viewport. */
    skyIdx = s->viewport[2];
    {
        uint8 r, g, b;
        gfx_paletteRGB((int)(uint8)skyIdx, &r, &g, &b);
        glClearColor(r / 255.0f, g / 255.0f, b / 255.0f, 1.0f);
    }
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    /* Sky/ground background sphere (detail >= 3) drawn first (farthest) with the
     * depth test off so it stays behind everything; the 3D projection is then
     * restored for the objects. */
    if ((char)g_detailLevel >= 3)
        glDrawSphere(sphOrtho[0], sphOrtho[1], sphOrtho[2], sphOrtho[3]);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(s_proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Hybrid depth ordering (see gl_endScene): a real z-buffer resolves genuine
     * occlusion (a plane behind a building does not show through), while objects
     * are still drawn in the original's painter's order so coplanar surfaces a
     * z-buffer can't separate keep the original look. GL_LEQUAL lets the later-drawn
     * coplanar surface win at equal depth; a per-draw polygon offset (gl_endScene)
     * turns draw order into a depth tiebreak so they never z-fight. The depth buffer
     * was cleared above; the sphere left it untouched (depth writes off). */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glEnable(GL_POLYGON_OFFSET_POINT);

    /* Distance haze (the GL equivalent of the software path's stepped g_objShade
     * bands). Enabled after the sky sphere so the background gradient itself isn't
     * fogged; disabled in gl_endScene.
     * Fog colour = live horizon colour so geometry fades into the horizon line. The
     * distance is the per-vertex forward depth supplied by fogVertex (GL_FOG_COORD),
     * not GL's eye-distance — see fogVertex. */
    {
        uint8 fr, fg, fb;
        GLfloat fogColor[4];
        gfx_paletteRGB(GL_HORIZON_IDX, &fr, &fg, &fb);
        fogColor[0] = fr / 255.0f;
        fogColor[1] = fg / 255.0f;
        fogColor[2] = fb / 255.0f;
        fogColor[3] = 1.0f;
        glFogi(GL_FOG_MODE, FOG_MODE);
        glFogfv(GL_FOG_COLOR, fogColor);
        if (FOG_MODE == GL_LINEAR) {
            glFogf(GL_FOG_START, FOG_NEAR_DIST);
            glFogf(GL_FOG_END, FOG_FAR_DIST);
        } else {
            glFogf(GL_FOG_DENSITY, FOG_DENSITY);
        }
        if (s_glFogCoordf) glFogi(GL_FOG_COORD_SRC, GL_FOG_COORD); /* use fogVertex's z */
        glEnable(GL_FOG);
    }

    /* Register the 3D viewport rect as show-through: the anchor page composite makes
     * exactly this rect transparent so the GL 3D shows, and the immediate HUD then
     * draws over it. No page fill needed — nothing 2D is baked into this region. */
    addShowRect(vpLeft, vpTop, Wv, Hv);
    s_nSub = 0;
    s_subOverflow = 0;
    s_nLine = 0;
    s_lineOverflow = 0;
    s_flatN = 0; /* model-flatness cache is per-frame (world data may reload) */
    s_mainScene = 1; /* the out-the-window view: gl_endScene composites the page backdrop after it */
    s_sceneRendered = 1;
}

static void gl_submit(const R3DSubmit *o) {
    GlSub *r;
    int16 combined[9];
    long camBase, camTransX, camTransY, depth;
    int shade, shift, i;

    if (s_nSub >= GL_MAX_SUBS) {
        s_subOverflow++;
        return;
    }

    if (r3d_objTransformFar((char far *)o->mesh, o->yaw, o->pitch, o->roll,
                            o->posX, o->posY, o->posZ,
                            combined, &camBase, &camTransX, &camTransY, &shade))
        return; /* frustum-culled */

    r = &s_subs[s_nSub];
    r->model = (char *)o->mesh;
    r->shapeId = o->shapeId;
    r->containerLegacyName = o->containerLegacyName;
    for (i = 0; i < 9; i++) r->combined[i] = combined[i];
    r->camBase = camBase;
    r->camX = camTransX;
    r->camY = camTransY;
    r->shade = shade;
    r->colorBase = g_objColorBase;
    r->curLod = g_curLod;
    r->posZ = o->posZ;
    r->shadow = o->shadow;

    /* Immediate = the no-z-buffer ground class (drawn first, painter's): a flat,
     * Z=0, unsorted shape. Unlike projectSceneObject (which keys only on Z=0 + sort
     * flag), we also require the model to be geometrically FLAT, so a 3D object that
     * merely SITS at sea level — a cargo ship with a tall bridge — is NOT lumped in
     * with the sea and instead gets z-buffered (pass 2), fixing its self-occlusion.
     * The lowest-LOD ground tile (colorBase 0x400) is always flat (its non-ground
     * faces are junk that would otherwise read as relief). */
    r->immediate = (!o->shadow && o->posZ == 0 &&
                    !peekSortFlag((const uint8 *)o->mesh,
                                  (const uint8 *)g_world3dData + WORLD3D_DATA_SIZE) &&
                    (g_objColorBase == 0x400 || modelIsFlat((const char *)o->mesh)));
    r->seq = s_nSub;
    s_nSub++;

    /* Painter's sort key: the LOD-normalized origin depth, exactly as
     * insertSortedObject computes it (so terrain across LODs and dynamic objects
     * order on a common scale, with the LOD-2/render-mode-5 farther bias). */
    depth = camTransY;
    shift = 8 - 2 * g_curLod;
    if (shift > 0) depth >>= shift;
    r->sortLo = (int)(uint16)(int16)depth;
    r->sortHi = (int)(int16)(depth >> 16);
    if (g_curLod == 2 && g_objRenderMode == 5) r->sortHi += 0x20;
}

static void gl_submitLine(const R3DLine *o) {
    GlLine *l;
    if (s_nLine >= GL_MAX_LINES) {
        s_lineOverflow++;
        return;
    }
    l = &s_lines[s_nLine++];
    l->baseXA = o->baseXA;
    l->camXA = o->camXA;
    l->camYA = o->camYA;
    l->baseXB = o->baseXB;
    l->camXB = o->camXB;
    l->camYB = o->camYB;
    l->color = o->color;
}

/* Distance-scaled single-pixel decoration. The original drew these as a fixed 1px
 * dot at any range; here the point shrinks with camera depth (and, with smooth-point
 * coverage, the far sub-pixel ones fade out) so the scattered ground decorations read
 * as receding into the distance — the depth cue they exist to give. Pixel drift from
 * the integer path is accepted (Q1). REF_DEPTH (a depthHi of 2500) maps to one logical
 * pixel and aligns with the edge-run colour-shade bands. */
/* Per-PRIMITIVE forward depth bias (glPolygonOffset units), applied in draw order
 * via a frame-global running counter (s_paintSeq, bumped by paintBias before every
 * face/line/point). Each successive primitive is nudged one step toward the camera,
 * so any two surfaces the z-buffer sees as coplanar — whether in different objects
 * (overlapping terrain tiles, sea vs deck) OR within one model (markings, fire on
 * the engine) — are ordered by draw order: the later one wins under GL_LEQUAL
 * instead of z-fighting. The step must exceed the depth-buffer jitter between
 * coplanar faces (which is several LSB out at distance, where far depth resolution
 * is poor) yet stay far below the gap between genuinely separated objects, so the
 * z-test still drives real occlusion. Raise if coplanar faces still shimmer; lower
 * if a clearly-nearer surface is punched through by a later, farther draw. */
static const float GL_PAINT_BIAS = 20.0f;
static int s_paintSeq; /* primitives drawn so far this frame (reset in gl_endScene) */

/* Bias the next primitive one draw-step toward the camera. Call OUTSIDE glBegin/End,
 * before each primitive, so draw order breaks coplanar depth ties. */
static void paintBias(void) {
    glPolygonOffset(0.0f, -(float)(s_paintSeq++) * GL_PAINT_BIAS);
}

/* Round anti-aliased points come from GL_POINT_SMOOTH's coverage-blended alpha. The
 * GL spec routes point AA through the multisample resolve instead when MULTISAMPLE is
 * on, which rasterizes the point as a hard SQUARE and drops the soft rim fade — so we
 * suspend multisampling for the point draw and restore it after (polygons/lines keep
 * their MSAA edges). Restore mirrors the init gate (GL_MSAA_SAMPLES > 0). */
static void beginSmoothPoints(void) {
    glDisable(GL_MULTISAMPLE);
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
static void endSmoothPoints(void) {
    glDisable(GL_BLEND);
    glDisable(GL_POINT_SMOOTH);
    if (GL_MSAA_SAMPLES > 0) glEnable(GL_MULTISAMPLE);
}

#define GL_POINT_REF_DEPTH 5000.0f /* depthHi at which the point is one logical pixel */
#define GL_POINT_NEAR_CAP 3.0f     /* max size, in logical pixels, for the nearest points */
#define GL_POINT_MIN_SIZE 0.5f     /* smallest size; smooth coverage fades it below 1px */
static void drawDepthPoint(float x, float y, long depth32, int colorIdx) {
    int depthHi = (int)(depth32 >> 16);
    float sz;
    if (depthHi < 1) return; /* behind the near plane */
    sz = s_pixelScale * (GL_POINT_REF_DEPTH / (float)depthHi);
    if (sz > GL_POINT_NEAR_CAP * s_pixelScale) sz = GL_POINT_NEAR_CAP * s_pixelScale;
    if (sz < GL_POINT_MIN_SIZE) sz = GL_POINT_MIN_SIZE;
    paintBias();
    glPointSize(sz);
    glColorIndex(colorIdx);
    glBegin(GL_POINTS);
    fogVertex(x, y, (float)depth32 / 65536.0f);
    glEnd();
}

/* Replacement and legacy shadows use the same opacity and ground-plane lift. */
static const float GL_SHADOW_ALPHA = 0.4f;
static const float GL_SHADOW_RAISE_FRAC = 0.25f;

/* Submit one replacement mesh with the same transform and primitive order as the legacy shape. */
static void drawReplacementSub(const GlSub *submission,
                               const R3DReplacementMesh *mesh) {
    const int lod_shift = 8 - 2 * submission->curLod;
    const int shift = lod_shift > 0 ? lod_shift : 0;
    const float scale = (float)(1 << shift);
    float matrix[9]{};
    float shadow_nx = 0.0f, shadow_ny = 0.0f, shadow_nz = 0.0f;
    float shadow_ox = 0.0f, shadow_oy = 0.0f, shadow_oz = 0.0f;
    float shadow_lift_x = 0.0f, shadow_lift_y = 0.0f;
    float shadow_lift_z = 0.0f;
    for (int index = 0; index < 9; ++index) {
        matrix[index] = (float)submission->combined[index];
    }

    if (submission->shadow) {
        shadow_nx = (float)g_viewRotMatrix[3];
        shadow_ny = (float)g_viewRotMatrix[4];
        shadow_nz = (float)g_viewRotMatrix[5];
        const float normal_length = SDL_sqrtf(
            shadow_nx * shadow_nx + shadow_ny * shadow_ny
            + shadow_nz * shadow_nz);
        if (normal_length > 1e-3f) {
            shadow_nx /= normal_length;
            shadow_ny /= normal_length;
            shadow_nz /= normal_length;
        }
        shadow_ox = (float)submission->camBase / scale;
        shadow_oy = (float)submission->camX / scale;
        shadow_oz = (float)submission->camY / scale;
        float maximum_height = 0.0f;
        for (int primitive_index = 0;
             primitive_index < mesh->nPrims; ++primitive_index) {
            const R3DReplacementPrim *primitive =
                &mesh->prims[primitive_index];
            for (int vertex = 0; vertex < primitive->nVerts; ++vertex) {
                const float x = primitive->xyz[vertex * 3];
                const float y = primitive->xyz[vertex * 3 + 1];
                const float z = primitive->xyz[vertex * 3 + 2];
                const float camera_x =
                    (2.0f * (matrix[0] * x + matrix[3] * z
                             + matrix[6] * y)
                     + (float)submission->camBase) / scale;
                const float camera_y =
                    (2.0f * (matrix[1] * x + matrix[4] * z
                             + matrix[7] * y)
                     + (float)submission->camX) / scale;
                const float depth =
                    (2.0f * (matrix[2] * x + matrix[5] * z
                             + matrix[8] * y)
                     + (float)submission->camY) / scale;
                float height =
                    (camera_x - shadow_ox) * shadow_nx
                    + (camera_y - shadow_oy) * shadow_ny
                    + (depth - shadow_oz) * shadow_nz;
                if (height < 0.0f) height = -height;
                if (height > maximum_height) maximum_height = height;
            }
        }
        const float raise =
            (shadow_nz > 0.0f ? -GL_SHADOW_RAISE_FRAC
                              : GL_SHADOW_RAISE_FRAC)
            * maximum_height;
        shadow_lift_x = raise * shadow_nx;
        shadow_lift_y = raise * shadow_ny;
        shadow_lift_z = raise * shadow_nz;
    }

    for (int primitive_index = 0;
         primitive_index < mesh->nPrims; ++primitive_index) {
        const R3DReplacementPrim *primitive =
            &mesh->prims[primitive_index];
        if (submission->shadow && primitive->mode != 4) continue;
        const GLenum mode = primitive->mode == 4 ? GL_TRIANGLES
                          : primitive->mode == 1 ? GL_LINES : GL_POINTS;
        if (submission->shadow) {
            glColor4f(0.0f, 0.0f, 0.0f, GL_SHADOW_ALPHA);
        } else {
            paintBias();
            glColor4fv(primitive->rgba);
        }
        if (mode == GL_POINTS) {
            glPointSize(s_pixelScale > 1.0f ? s_pixelScale : 1.0f);
        }
        glBegin(mode);
        for (int vertex = 0; vertex < primitive->nVerts; ++vertex) {
            const float x = primitive->xyz[vertex * 3];
            const float y = primitive->xyz[vertex * 3 + 1];
            const float z = primitive->xyz[vertex * 3 + 2];
            float camera_x =
                (2.0f * (matrix[0] * x + matrix[3] * z + matrix[6] * y)
                 + (float)submission->camBase) / scale;
            float camera_y =
                (2.0f * (matrix[1] * x + matrix[4] * z + matrix[7] * y)
                 + (float)submission->camX) / scale;
            float depth =
                (2.0f * (matrix[2] * x + matrix[5] * z + matrix[8] * y)
                 + (float)submission->camY) / scale;
            if (submission->shadow) {
                const float distance =
                    (camera_x - shadow_ox) * shadow_nx
                    + (camera_y - shadow_oy) * shadow_ny
                    + (depth - shadow_oz) * shadow_nz;
                camera_x += shadow_lift_x - distance * shadow_nx;
                camera_y += shadow_lift_y - distance * shadow_ny;
                depth += shadow_lift_z - distance * shadow_nz;
            }
            fogVertex(camera_x, camera_y, depth / 65536.0f);
        }
        glEnd();
    }
}

/* Decode + transform + draw one object (painter's order; z-buffer on, GL_LEQUAL,
 * per-draw polygon offset set by the caller). Re-decodes the mesh (cheap; avoids
 * caching across region reloads). */
static void drawSub(const GlSub *r) {
    MeshVtxPools pools;
    MeshLod *l;
    int i, shift;
    float cm[9], scaleDiv;
    /* Ground-plane projection basis for a shadow (camera space): unit world-up
     * normal `sn` and the object origin `so` (a point on the plane). Each vertex is
     * projected straight down onto that plane, collapsing the model's vertical
     * relief into a flat outline on the ground, then lifted by `srv` (a uniform
     * translation toward the camera, so the faces stay coplanar for the GL_LESS pass). */
    float snx = 0, sny = 0, snz = 0, sox = 0, soy = 0, soz = 0;
    float srvx = 0, srvy = 0, srvz = 0;
    static float vx_[R3DMESH_MAX_VERTS], vy_[R3DMESH_MAX_VERTS], vd_[R3DMESH_MAX_VERTS];
    R3DReplacementMesh *replacement =
        r3dReplacementMesh(r->containerLegacyName, r->shapeId);

    if (replacement) {
        drawReplacementSub(r, replacement);
        return{};
    }

    fillPools(&pools);
    if (r3dmesh_decode((const uint8 *)r->model,
                       (const uint8 *)g_world3dData + WORLD3D_DATA_SIZE,
                       &pools, colorLut, &s_mesh) < 0)
        return;
    l = &s_mesh.lods[0];

    /* POINT (sceneObjPoint): the object origin as one shaded pixel. The origin
     * axes (camBase = screen-X, camX = screen-Y, camY = depth) project exactly as a
     * model vertex at (0,0,0) does — the LOD scale cancels in the perspective ratio,
     * so no scaleDiv is needed. Sized by distance (drawDepthPoint). */
    if (l->form == MESH_FORM_POINT) {
        if ((int)(r->camY >> 16) < 1) return; /* g_camTransYHi < 1 cull */
        beginSmoothPoints();
        drawDepthPoint((float)r->camBase, (float)r->camX, r->camY,
                       colorLut[l->pointColor] + GL_NEAR_SHADE);
        endSmoothPoints();
        return;
    }

    /* EDGERUN (sceneObjEdgeRun): a run of distance-shaded points, each a shared-pool
     * vertex transformed by the object matrix (the on-the-fly path's emitModelVertex).
     * No LOD scale (emitModelVertex applies none), so depth feeds glEdgeRunColor raw. */
    if (l->form == MESH_FORM_EDGERUN) {
        beginSmoothPoints();
        for (i = 0; i < 9; i++) cm[i] = (float)r->combined[i];
        for (i = 0; i < l->nRunRefs; i++) {
            int ref = l->runRefs[i];
            int ix, iy, iz, X, Y, Z;
            float camX, camY, depth;
            long depthRaw;
            if (ref >= pools.nRefs) continue;
            ix = pools.idxX[ref] & 0xff;
            iy = pools.idxY[ref] & 0xff;
            iz = pools.idxZ[ref] & 0xff;
            if (ix >= pools.nX || iy >= pools.nY || iz >= pools.nZ) continue;
            X = pools.coordX[ix];
            Y = pools.coordY[iy];
            Z = pools.coordZ[iz];
            camX = 2.0f * (cm[0] * X + cm[3] * Z + cm[6] * Y) + (float)r->camBase;
            camY = 2.0f * (cm[1] * X + cm[4] * Z + cm[7] * Y) + (float)r->camX;
            depth = 2.0f * (cm[2] * X + cm[5] * Z + cm[8] * Y) + (float)r->camY;
            depthRaw = (long)depth;
            if ((int)(depthRaw >> 16) < 1) continue; /* dHi >= 1 */
            drawDepthPoint(camX, camY, depthRaw, glEdgeRunColor((int)(depthRaw >> 16)));
        }
        endSmoothPoints();
        return;
    }

    if (l->form != MESH_FORM_MODEL) return;

    /* The LOD coordinate scale (8 - 2*curLod) cancels in the x/y projection ratio
     * (camX/depth); applied here only to keep depths consistent for the perspective
     * divide across LODs. */
    shift = 8 - 2 * r->curLod;
    if (shift < 0) shift = 0;
    scaleDiv = (float)(1 << shift);

    for (i = 0; i < 9; i++) cm[i] = (float)r->combined[i];

    /* Shadow: build the ground-plane projection basis. World-up in this camera
     * basis is g_viewRotMatrix's Z column (same as the wire ground test); the plane
     * passes through the object origin, which sits at ground level (the shadow is
     * submitted at the terrain altitude, level). Vertices are projected onto it
     * below, flattening the model's vertical relief into a flat outline. */
    if (r->shadow) {
        float ul;
        snx = (float)g_viewRotMatrix[3];
        sny = (float)g_viewRotMatrix[4];
        snz = (float)g_viewRotMatrix[5];
        ul = SDL_sqrtf(snx * snx + sny * sny + snz * snz);
        if (ul > 1e-3f) { snx /= ul; sny /= ul; snz /= ul; }
        sox = (float)r->camBase / scaleDiv;
        soy = (float)r->camX / scaleDiv;
        soz = (float)r->camY / scaleDiv;
        {
            /* Model's camera-space height above the ground plane, and the lift vector
             * along the plane normal in whichever direction reduces depth (toward the
             * camera) so the raised shadow wins the depth test over the ground. */
            float maxDd = 0.0f, rs;
            for (i = 0; i < l->nVerts; i++) {
                int X = l->verts[i].x, Y = l->verts[i].y, Z = l->verts[i].z;
                float cX = (2.0f * (cm[0] * X + cm[3] * Z + cm[6] * Y) + (float)r->camBase) / scaleDiv;
                float cY = (2.0f * (cm[1] * X + cm[4] * Z + cm[7] * Y) + (float)r->camX) / scaleDiv;
                float dp = (2.0f * (cm[2] * X + cm[5] * Z + cm[8] * Y) + (float)r->camY) / scaleDiv;
                float dd = (cX - sox) * snx + (cY - soy) * sny + (dp - soz) * snz;
                if (dd < 0.0f) dd = -dd;
                if (dd > maxDd) maxDd = dd;
            }
            rs = (snz > 0.0f ? -GL_SHADOW_RAISE_FRAC : GL_SHADOW_RAISE_FRAC) * maxDd;
            srvx = rs * snx; srvy = rs * sny; srvz = rs * snz;
        }
    }

    /* Screen-space bounding box (window pixels) of the projected vertices. The GL
     * path always renders the finest LOD; at long range that geometry shrinks below
     * a pixel and rasterizes to nothing, whereas the original switched to a coarse
     * point LOD and drew an explicit dot. Track the span and, if the whole model is
     * sub-pixel, fall back to a single distance-scaled point at the origin — so a
     * far object reads as a decorative pixel and grows into geometry as you approach
     * (the LOD fade the data intends), without reintroducing a near-LOD point. */
    {
        float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
        int nProj = 0;
        for (i = 0; i < l->nVerts; i++) {
            int X = l->verts[i].x, Y = l->verts[i].y, Z = l->verts[i].z;
            float camX = (2.0f * (cm[0] * X + cm[3] * Z + cm[6] * Y) + (float)r->camBase) / scaleDiv;
            float camY = (2.0f * (cm[1] * X + cm[4] * Z + cm[7] * Y) + (float)r->camX) / scaleDiv;
            float depth = (2.0f * (cm[2] * X + cm[5] * Z + cm[8] * Y) + (float)r->camY) / scaleDiv;
            if (r->shadow) {
                /* Drop the vertex onto the ground plane along world-up, then lift the
                 * whole plane toward the camera so it clears the ground (no z-fight). */
                float dd = (camX - sox) * snx + (camY - soy) * sny + (depth - soz) * snz;
                camX -= dd * snx; camY -= dd * sny; depth -= dd * snz;
                camX += srvx; camY += srvy; depth += srvz;
            }
            vx_[i] = camX;
            vy_[i] = camY;
            vd_[i] = depth / 65536.0f;
            if (vd_[i] > 1e-3f) {
                /* px = (NDC*0.5+0.5)*viewport; the principal-point offsets cancel in
                 * the span, so only the s_proj scale terms matter here. */
                float sx = (s_proj[0] * vx_[i] / vd_[i]) * 0.5f * s_vpW;
                float sy = (s_proj[5] * vy_[i] / vd_[i]) * 0.5f * s_vpH;
                if (sx < minX) minX = sx;
                if (sx > maxX) maxX = sx;
                if (sy < minY) minY = sy;
                if (sy > maxY) maxY = sy;
                nProj++;
            }
        }
        /* Only collapse to a point when the WHOLE model is in front of the near
         * plane and sub-pixel — a genuinely distant, tiny object. If any vertex is
         * behind the near plane (nProj < nVerts) the object straddles it (near/
         * foreground geometry, especially flat sea tiles seen edge-on or while
         * banked): the surviving in-front verts give a bogus sub-pixel span, which
         * would drop the tile to a single dot. Draw the real near-plane-clipped
         * polygon instead, matching the software rasterizer's screen-space clip. */
        if (!r->shadow && nProj == l->nVerts && nProj > 0 &&
            (maxX - minX) < 2.0f && (maxY - minY) < 2.0f) {
            int colorByte = -1;
            for (i = 0; i < l->nFaces; i++) {
                int cb = l->faces[i].colorByte;
                if (cb == 0xff) continue;
                if (r->colorBase == 0x400 && cb != 1) continue;
                colorByte = cb;
                break;
            }
            if (colorByte < 0 && l->nLines) colorByte = l->lines[0].colorByte;
            if (colorByte >= 0) {
                beginSmoothPoints();
                drawDepthPoint((float)r->camBase, (float)r->camX, r->camY,
                               colorLut[colorByte] + GL_NEAR_SHADE);
                endSmoothPoints();
            }
            return;
        }
    }

    /* Filled faces. The stream lists a face as an unordered set of boundary edges
     * (the software span-fill needs no order); GL_POLYGON needs the vertices in
     * loop order, so walk the edge adjacency around the cycle. */
    for (i = 0; i < l->nFaces; i++) {
        MeshFace *f = &l->faces[i];
        int ring[R3DMESH_MAX_FACE_EDGES + 1];
        int n, k, cur, prev, deg;
        if (f->nEdges < 3) continue;
        /* Colour filters from renderPrimitiveCommand: 0xff is transparent, and the
         * lowest-LOD flat ground tile (colorBase == 0x400) draws only its
         * colorByte==1 ground face — the rest of that tile's faces are junk. */
        if (f->colorByte == 0xff) continue;
        if (r->colorBase == 0x400 && f->colorByte != 1) continue;

        for (k = 0; k < f->nEdges; k++) {
            MeshEdge *e = &l->edges[f->edge[k]];
            if (e->va < l->nVerts) s_nbrN[e->va] = 0;
            if (e->vb < l->nVerts) s_nbrN[e->vb] = 0;
        }
        for (k = 0; k < f->nEdges; k++) {
            MeshEdge *e = &l->edges[f->edge[k]];
            if (e->va >= l->nVerts || e->vb >= l->nVerts) continue;
            if (s_nbrN[e->va] < 2) s_nbr[e->va][s_nbrN[e->va]++] = e->vb;
            if (s_nbrN[e->vb] < 2) s_nbr[e->vb][s_nbrN[e->vb]++] = e->va;
        }

        cur = l->edges[f->edge[0]].va;
        if (cur >= l->nVerts) continue;
        n = 0;
        prev = -1;
        ring[n++] = cur;
        for (k = 0; k < f->nEdges && n <= R3DMESH_MAX_FACE_EDGES; k++) {
            deg = s_nbrN[cur];
            int next = (deg >= 1 && s_nbr[cur][0] != prev) ? s_nbr[cur][0]
                     : (deg >= 2)                          ? s_nbr[cur][1]
                                                           : -1;
            if (next < 0 || next == ring[0]) break;
            ring[n++] = next;
            prev = cur;
            cur = next;
        }
        if (n < 3) continue;

        if (r->shadow) {
            /* No paintBias — the shadow pass (gl_endScene) uses the stencil to blend
             * each covered pixel once, so overlapping flattened faces need no depth
             * ordering; a per-face offset would only reintroduce depth jitter. */
            glColor4f(0.0f, 0.0f, 0.0f, GL_SHADOW_ALPHA);
        } else {
            paintBias();
            glColorIndex(colorLut[f->colorByte] + GL_NEAR_SHADE);
        }
        glBegin(GL_POLYGON);
        for (k = 0; k < n; k++) fogVertex(vx_[ring[k]], vy_[ring[k]], vd_[ring[k]]);
        glEnd();
    }

    /* Wire primitives (roads, beams, mountain highlights, antennas) as world-space
     * ribbons. Each line becomes a thin quad offset by +/- a world-space half-width;
     * being real geometry it tapers with perspective, clips on the near plane (no
     * explosion when an endpoint goes behind the eye) and shows no screen-aligned
     * cap as it rotates. Work in true camera space (z = vd_ * 65536, same scale as
     * x/y) so lengths and perpendiculars are Euclidean; divide z out only on emit.
     *
     * Two extrusion modes (see WIRE_FLAT_VERT): a near-horizontal wire is a GROUND
     * feature and is extruded within the horizontal plane (perpendicular to world-
     * up) so it stays pinned flat to the ground; a steeper wire is extruded camera-
     * facing (perpendicular to the line and the view ray) so it always reads as a
     * line. World-up in camera space is the matrix column the model Z axis maps
     * through, i.e. (g_viewRotMatrix[3..5]) in this (camX,camY,depth) basis. */
    if (l->nLines && !r->shadow) {
        float ux = (float)g_viewRotMatrix[3];
        float uy = (float)g_viewRotMatrix[4];
        float uz = (float)g_viewRotMatrix[5];
        float ul = SDL_sqrtf(ux * ux + uy * uy + uz * uz);
        if (ul > 1e-3f) { ux /= ul; uy /= ul; uz /= ul; }
        for (i = 0; i < l->nLines; i++) {
            MeshEdge *e = &l->edges[l->lines[i].edge];
            float ax, ay, az, bx, by, bz, lx, ly, lz, len;
            if (e->va >= l->nVerts || e->vb >= l->nVerts) continue;
            ax = vx_[e->va]; ay = vy_[e->va]; az = vd_[e->va] * 65536.0f;
            bx = vx_[e->vb]; by = vy_[e->vb]; bz = vd_[e->vb] * 65536.0f;
            lx = bx - ax; ly = by - ay; lz = bz - az;
            len = SDL_sqrtf(lx * lx + ly * ly + lz * lz);
            if (len < 1.0f) continue;
            float dx, dy, dz, vert;
            dx = lx / len; dy = ly / len; dz = lz / len;
            vert = ux * dx + uy * dy + uz * dz;
            if (vert < 0.0f) vert = -vert; /* 0 = horizontal, 1 = vertical */

            /* One width law for every wire (extrusion below is what differs). */
            float hw = WIRE_HW_SCALE * SDL_powf(len, WIRE_HW_POW);

            glColorIndex(colorLut[l->lines[i].colorByte] + GL_NEAR_SHADE);
            /* Lay flat only for a near-horizontal wire on a ground-level object
             * (|posZ| small). The object's world altitude is the clean signal — a
             * road tile sits at ~0, a flying plane carries its altitude — so a level
             * antenna up in the air keeps the camera-facing ribbon. */
            if (vert < WIRE_FLAT_VERT && r->posZ < 1.0f) {
                /* GROUND: one horizontal offset for both ends = normalize(cross(up,
                 * lineDir)) * hw — a flat parallelogram lying on the ground. */
                float gx = uy * dz - uz * dy;
                float gy = uz * dx - ux * dz;
                float gz = ux * dy - uy * dx;
                float gl = SDL_sqrtf(gx * gx + gy * gy + gz * gz);
                if (gl < 1e-3f) continue;
                gx = gx / gl * hw; gy = gy / gl * hw; gz = gz / gl * hw;
                /* Per-line bias so two crossing wires on the same model (both coplanar
                 * with the ground/deck) are ordered by draw order, not left to z-fight. */
                paintBias();
                glBegin(GL_QUADS);
                fogVertex(ax + gx, ay + gy, (az + gz) / 65536.0f);
                fogVertex(bx + gx, by + gy, (bz + gz) / 65536.0f);
                fogVertex(bx - gx, by - gy, (bz - gz) / 65536.0f);
                fogVertex(ax - gx, ay - gy, (az - gz) / 65536.0f);
                glEnd();
            } else {
                /* CAMERA-FACING: per-end normalize(cross(lineDir, ray)) * hw. */
                float pax, pay, paz, pbx, pby, pbz, pl;
                pax = ly * az - lz * ay; pay = lz * ax - lx * az; paz = lx * ay - ly * ax;
                pl = SDL_sqrtf(pax * pax + pay * pay + paz * paz);
                if (pl < 1e-3f) continue;
                pax = pax / pl * hw; pay = pay / pl * hw; paz = paz / pl * hw;
                pbx = ly * bz - lz * by; pby = lz * bx - lx * bz; pbz = lx * by - ly * bx;
                pl = SDL_sqrtf(pbx * pbx + pby * pby + pbz * pbz);
                if (pl < 1e-3f) continue;
                pbx = pbx / pl * hw; pby = pby / pl * hw; pbz = pbz / pl * hw;
                /* keep the two ends' offsets consistent so the quad doesn't twist */
                if (pax * pbx + pay * pby + paz * pbz < 0.0f) { pbx = -pbx; pby = -pby; pbz = -pbz; }
                paintBias(); /* per-line bias: overlapping wires order by draw order */
                glBegin(GL_QUADS);
                fogVertex(ax + pax, ay + pay, (az + paz) / 65536.0f);
                fogVertex(bx + pbx, by + pby, (bz + pbz) / 65536.0f);
                fogVertex(bx - pbx, by - pby, (bz - pbz) / 65536.0f);
                fogVertex(ax - pax, ay - pay, (az - paz) / 65536.0f);
                glEnd();
            }
        }
    }
}

/* Effect-line ribbon half-width as a fraction of camera depth, so a tracer /
 * explosion spark keeps a roughly constant thin screen width (the offset is added
 * in camera space and divided by depth on projection). Tune to taste. */
static const float GL_EFFECT_HW_FRAC = 0.0016f;

/* The LOD coordinate scale drawWorldObject/gl_submit apply to these effects
 * (g_curLod == 1 -> 1 << (8 - 2*1)). drawSub divides every model vertex by the
 * same scaleDiv; it cancels in the x/y projection ratio but sets the absolute
 * magnitude the fog distance (vd_) is read in, so a loose line must divide too or
 * it hazes ~64x too hard. */
static const float GL_EFFECT_SCALEDIV = 64.0f;

/* Draw one world-space 3D line (tracer / explosion spark) as a camera-facing
 * ribbon quad, the same construction as a model wire (drawSub): work in true
 * camera space (z = depth, same scale as x/y) so the perpendicular is Euclidean
 * and the near plane clips it, divide z out on emit. z-tested + fogged by the
 * caller's GL state, so it occludes and hazes like scene geometry. */
static void drawGlLine(const GlLine *ln) {
    const float sd = GL_EFFECT_SCALEDIV;
    float ax = (float)ln->baseXA / sd, ay = (float)ln->camXA / sd, az = (float)ln->camYA / sd;
    float bx = (float)ln->baseXB / sd, by = (float)ln->camXB / sd, bz = (float)ln->camYB / sd;
    float lx = bx - ax, ly = by - ay, lz = bz - az;
    float len = SDL_sqrtf(lx * lx + ly * ly + lz * lz);
    float avgDepth, hw, pax, pay, paz, pbx, pby, pbz, pl;
    if (len < 1.0f) return;
    avgDepth = 0.5f * (az + bz);
    if (avgDepth < 1.0f) avgDepth = 1.0f;
    hw = avgDepth * GL_EFFECT_HW_FRAC;

    /* per-end normalize(cross(lineDir, ray)) * hw (ray = endpoint position) */
    pax = ly * az - lz * ay;
    pay = lz * ax - lx * az;
    paz = lx * ay - ly * ax;
    pl = SDL_sqrtf(pax * pax + pay * pay + paz * paz);
    if (pl < 1e-3f) return;
    pax = pax / pl * hw;
    pay = pay / pl * hw;
    paz = paz / pl * hw;
    pbx = ly * bz - lz * by;
    pby = lz * bx - lx * bz;
    pbz = lx * by - ly * bx;
    pl = SDL_sqrtf(pbx * pbx + pby * pby + pbz * pbz);
    if (pl < 1e-3f) return;
    pbx = pbx / pl * hw;
    pby = pby / pl * hw;
    pbz = pbz / pl * hw;
    if (pax * pbx + pay * pby + paz * pbz < 0.0f) {
        pbx = -pbx;
        pby = -pby;
        pbz = -pbz;
    }
    glColorIndex(ln->color);
    glBegin(GL_QUADS);
    fogVertex(ax + pax, ay + pay, (az + paz) / 65536.0f);
    fogVertex(bx + pbx, by + pby, (bz + pbz) / 65536.0f);
    fogVertex(bx - pbx, by - pby, (bz - pbz) / 65536.0f);
    fogVertex(ax - pax, ay - pay, (az - paz) / 65536.0f);
    glEnd();
}

/* Painter's order, reproducing projectSceneObject + renderSortedListFar: the
 * immediate (flat ground/sea) objects first in walk order, then the sorted queue
 * farthest first (descending sortHi, then unsigned sortLo). */
static int subCmp(const void *a, const void *b) {
    const GlSub *x = (const GlSub *)a, *y = (const GlSub *)b;
    if (x->immediate != y->immediate) return y->immediate - x->immediate; /* immediate first */
    if (x->immediate) return x->seq - y->seq;                             /* walk order */
    if (x->sortHi != y->sortHi) return (x->sortHi < y->sortHi) - (x->sortHi > y->sortHi);
    return ((unsigned)x->sortLo < (unsigned)y->sortLo) - ((unsigned)x->sortLo > (unsigned)y->sortLo);
}

static void composePageBackdrop(SDL_Surface *page, int shakeOffset); /* defined below */

static void gl_endScene(void) {
    int i;

    /* Re-establish the scene's GL state before replaying the deferred subs: an
     * immediate 2D primitive drawn mid-scene (e.g. the far-contact dot from
     * updateTargetLock) leaves the overlay's full-window ortho/viewport behind,
     * which would project the whole scene as garbage. */
    glViewport(s_sceneVp[0], s_sceneVp[1], s_sceneVp[2], s_sceneVp[3]);
    glScissor(s_sceneVp[0], s_sceneVp[1], s_sceneVp[2], s_sceneVp[3]);
    glEnable(GL_SCISSOR_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(s_proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glShadeModel(GL_FLAT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glEnable(GL_POLYGON_OFFSET_POINT);
    if (s_mainScene) glEnable(GL_FOG);

    if (s_subOverflow)
        LogWarn(("r3d_gl: %d submissions dropped (cap %d)", s_subOverflow, GL_MAX_SUBS));
    qsort(s_subs, s_nSub, sizeof(GlSub), subCmp);

    /* Pass 1 — the ground plane class (flat ground/sea/roads/surf at world Z=0, the
     * "immediate" set): pure painter's, depth test OFF and depth writes OFF, drawn
     * first as the background in walk order. These are huge, distant, and densely
     * coplanar (surf on sea, markings on a road, outlines on a deck) — the case no
     * z-buffer + offset could resolve without a bias so large it punched through
     * small elevated models. With the test off, draw order alone orders them, exactly
     * like the original. They write no depth, so they never wrongly occlude pass 2. */
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    for (i = 0; i < s_nSub; i++)
        if (s_subs[i].immediate) drawSub(&s_subs[i]);

    /* Pass 2 — elevated objects (planes, missiles, buildings, terrain with relief):
     * z-buffered (GL_LEQUAL, set in gl_beginScene) so genuine occlusion is correct —
     * a plane behind a building does not show through. The per-primitive paint bias
     * lives only here, breaking these objects' own coplanar ties by draw order; the
     * counter resets so pass 1's primitives don't inflate it into a punch-through. */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    s_paintSeq = 0;

    for (i = 0; i < s_nSub; i++)
        if (!s_subs[i].immediate && !s_subs[i].shadow) drawSub(&s_subs[i]);

    /* Shadow pass — aircraft ground shadows, composited over the opaque world: they
     * lie on the ground (depth-tested, so geometry in front occludes them) but never
     * occlude anything (no depth writes). Translucent black, un-fogged so a distant
     * shadow stays dark. The stencil marks each covered pixel after its first fragment
     * (GL_EQUAL 0 to draw, GL_INCR on pass), so a silhouette whose flattened faces
     * overlap — tail over wing — blends exactly once instead of stacking to a darker
     * core. The small camera-ward lift (drawSub) keeps it off the coplanar ground. */
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_FOG);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xff);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    for (i = 0; i < s_nSub; i++)
        if (s_subs[i].shadow) drawSub(&s_subs[i]);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (s_mainScene) glEnable(GL_FOG);

    /* 3D effect lines (tracers / explosion sparks): z-tested against the scene
     * just drawn and fogged like it, so they occlude and haze. Drawn after the
     * objects with no polygon offset (they are thin free-standing ribbons, not
     * coplanar decals). */
    if (s_lineOverflow)
        LogWarn(("r3d_gl: %d effect lines dropped (cap %d)", s_lineOverflow, GL_MAX_LINES));
    glPolygonOffset(0.0f, 0.0f);
    for (i = 0; i < s_nLine; i++) drawGlLine(&s_lines[i]);
    s_nLine = 0;

    glDisable(GL_FOG);
    s_nSub = 0;
    glDisable(GL_SCISSOR_TEST);

    /* Ordering anchor: composite the retained page (cockpit/panel + on-change
     * gauges, with the viewport show-through hole) onto the window NOW — right after
     * the main out-the-window 3D view, before the frame's immediate HUD/MFD draws —
     * so the 2D lands over it in true call order. The MFD target sub-scene has its
     * own path (s_mainScene 0) and draws its model directly over this backdrop. */
    if (s_mainScene) {
        composePageBackdrop(gfx_getCurPageSurface(), gfx_getShakeOffset());
        s_mainScene = 0;
    }
}

const R3DBackend r3d_glBackend = {
    gl_name,
    gl_init,
    gl_shutdown,
    gl_registerMesh,
    gl_releaseMesh,
    gl_beginScene,
    gl_submit,
    gl_submitLine,
    gl_endScene,
};

/* ---- 2D overlay composite + present ------------------------------------ */

/* Set the nearest-filter + clamp parameters shared by the page and sprite
 * textures (indexed art must not bleed or filter). */
static void setOverlayTexParams(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void gl_imageDestroyed(R2DImage *img) {
    GLuint tex = (GLuint)r2d_imageCacheTex(img);
    if (tex) {
        glDeleteTextures(1, &tex);
        r2d_imageSetCache(img, 0, -1);
    }
}

/* The cached RGBA texture for a submitted sprite's whole backing sheet, rebuilt
 * only when the palette generation moved since it was uploaded. Index 0 becomes
 * the transparent texel (alpha 0) — the sprite transparency key is always 0; an
 * opaque draw (key<0) disables blend at draw time so alpha is ignored. */
/* HD (RGBA) sprites downscale from a large source into a small footprint, so they
 * want smooth filtering rather than the pixel-art NEAREST of the paletted sprites. */
static void setHdTexParams(void) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint imageTexture(R2DImage *img, SDL_Palette *pal, int palGen) {
    GLuint tex = (GLuint)r2d_imageCacheTex(img);
    SDL_Surface *surf = r2d_imageSurface(img);
    const uint8 *src;
    uint8 *rgba;
    int sw, sh, pitch, x, y;

    if (!surf) return 0;
    sw = surf->w;
    sh = surf->h;
    if (surf->format != SDL_PIXELFORMAT_INDEX8) {
        /* HD RGBA sprite: palette-independent, so upload once and reuse (the cache
         * gen is irrelevant — a nonzero tex means it is already built). Copy rows to
         * a tight RGBA scratch in case the surface pitch is padded. */
        if (tex) return tex;
        rgba = ensureRgbaScratch(sw * sh * 4);
        if (!rgba) return 0;
        for (y = 0; y < sh; y++)
            SDL_memcpy(rgba + (size_t)y * sw * 4,
                       (const uint8 *)surf->pixels + (size_t)y * surf->pitch,
                       (size_t)sw * 4);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        setHdTexParams();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        r2d_imageSetCache(img, (unsigned int)tex, palGen);
        return tex;
    }
    if (tex && r2d_imageCacheGen(img) == palGen) return tex;
    rgba = ensureRgbaScratch(sw * sh * 4);
    if (!rgba) return 0;
    src = (const uint8 *)surf->pixels;
    pitch = surf->pitch;
    for (y = 0; y < sh; y++) {
        const uint8 *row = src + y * pitch;
        uint8 *out = rgba + y * sw * 4;
        for (x = 0; x < sw; x++) {
            uint8 idx = row[x];
            SDL_Color c = pal->colors[idx];
            out[x * 4 + 0] = c.r;
            out[x * 4 + 1] = c.g;
            out[x * 4 + 2] = c.b;
            out[x * 4 + 3] = (idx == 0) ? 0 : 255;
        }
    }
    if (!tex) glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    setOverlayTexParams();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sw, sh, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    r2d_imageSetCache(img, (unsigned int)tex, palGen);
    return tex;
}

/* ---- Immediate 2D overlay primitives ------------------------------------ */

/* 2D overlay draw context: the virtual->window letterbox (from the shared r2d
 * mapping) plus the screen-shake, shared by every immediate 2D primitive. Set once
 * before the frame's first 2D draw (overlaySetContext); each r3dgl_draw* below reads
 * it. Drawing a cached texture or a few vector verts is cheap enough to issue every
 * frame, so the only thing cached is the *decode* (imageTexture), never the draw. */
static struct {
    int lbx, lby;         /* top-left of the centred virtual box, window px */
    float scaleX, scaleY; /* window px per virtual (320-space) px, per axis */
    float shake;          /* explosion screen-shake, already scaled to window px */
    int winW, winH;
    float lineW;          /* native HUD line width */
} s_ov;

static void overlaySetContext(int virtW, int virtH, int shakeOffset) {
    R2DMapping m;
    SDL_GetWindowSizeInPixels(s_win, &s_ov.winW, &s_ov.winH);
    r2d_computeMapping(virtW, virtH, s_ov.winW, s_ov.winH, 0, &m);
    s_ov.scaleX = m.scaleX;
    s_ov.scaleY = m.scaleY;
    s_ov.lbx = m.offX;
    s_ov.lby = m.offY;
    s_ov.shake = (float)shakeOffset * m.scaleX;
    s_ov.lineW = m.scaleY < 1.0f ? 1.0f : m.scaleY;
}

void r3dgl_beginOverlay(int composePage) {
    /* The flight overlay is authored in the 320x200 virtual box; shake is the live
     * explosion offset (read once per frame here, applied by every r3dgl_draw*). */
    overlaySetContext(LOGICAL_WIDTH, LOGICAL_HEIGHT, gfx_getShakeOffset());
    /* Flight frames render the 3D and composite the page backdrop mid-frame (the
     * gl_endScene anchor) before their immediate HUD draws. Pure-2D screens have no
     * 3D pass, so lay the page down NOW — under the immediate overlay (map/lines/
     * markers/popup) drawn next — instead of at present, where it would land on top
     * and blank the overlay. The present then skips its own composite. */
    if (composePage == 1) {
        composePageBackdrop(gfx_getCurPageSurface(), gfx_getShakeOffset());
        s_pageComposited = 1;
    } else if (composePage == 2) {
        /* Self-backdrop (HD briefing): the caller draws its own window-filling
         * backdrop as the first overlay draw, so the legacy 320x200 page is never
         * composited. Clear the window to black (uncovered margins) and mark the page
         * handled so the present skips its own composite (which would land the page
         * ON TOP of the overlay). */
        glViewport(0, 0, s_ov.winW, s_ov.winH);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        s_pageComposited = 1;
    }
}

/* Map a 320-space coordinate to window pixels through the shared letterbox+shake.
 * Callers add the +0.5 pixel-centre bias for vector endpoints; corners pass integers. */
static float ovMapX(float x) { return (float)s_ov.lbx + x * s_ov.scaleX - s_ov.shake; }
static float ovMapY(float y) { return (float)s_ov.lby + y * s_ov.scaleY; }

/* Scissor to a half-open 320-space clip rect (used by the MFD poly/scope draws). */
static void ovClipScissor(int cx0, int cy0, int cx1, int cy1) {
    float sx0 = ovMapX((float)cx0), sx1 = ovMapX((float)cx1);
    float sy1w = ovMapY((float)cy0), sy2w = ovMapY((float)cy1);
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)sx0, (int)((float)s_ov.winH - sy2w),
              (int)(sx1 - sx0), (int)(sy2w - sy1w));
}

/* Establish the 2D ortho GL state for one immediate overlay primitive: full-window
 * viewport, y-down ortho in window pixels, depth/fog/lighting off. Cheap and fully
 * self-contained so a primitive drawn mid-frame — even right after a 3D sub-scene
 * that left depth/scissor/perspective set — always draws correctly, no shared
 * "am I in 2D mode" flag. */
static void overlay2DState(void) {
    glViewport(0, 0, s_ov.winW, s_ov.winH);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_TEXTURE_2D);
    glShadeModel(GL_FLAT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, s_ov.winW, s_ov.winH, 0, -1, 1); /* y-down: 320-space row 0 at top */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

/* Fill a wide line's endpoints with a centred square of the line's own width.
 * GL wide lines have butt caps, so two segments of a shape (target box, hexagon,
 * HUD frame, A2A circle) that share an endpoint leave a triangular notch at the
 * join; the coincident corner squares of the abutting segments fill it. Axis-
 * aligned in device space, so an axis-aligned box keeps sharp, crisp corners (a
 * round cap would round them) — and, being bounded, it never spikes at an acute
 * angle the way a projecting cap would. Device-space (post-ovMap) coords; the
 * caller owns the colour / 2D state. Only worth it once lines are thick enough to
 * gap — a 1px line's butt caps already touch. */
static void capLineEnds(float ax, float ay, float bx, float by, float w) {
    float hw = w * 0.5f;
    glBegin(GL_QUADS);
    glVertex2f(ax - hw, ay - hw); glVertex2f(ax + hw, ay - hw);
    glVertex2f(ax + hw, ay + hw); glVertex2f(ax - hw, ay + hw);
    glVertex2f(bx - hw, by - hw); glVertex2f(bx + hw, by - hw);
    glVertex2f(bx + hw, by + hw); glVertex2f(bx - hw, by + hw);
    glEnd();
}

void r3dgl_drawLine(int x1, int y1, int x2, int y2, int color) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Color c;
    float ax, ay, bx, by;
    if (!pal) return;
    c = pal->colors[color & 0xff];
    overlay2DState();
    glColor3ub(c.r, c.g, c.b);
    ax = ovMapX((float)x1 + 0.5f); ay = ovMapY((float)y1 + 0.5f);
    bx = ovMapX((float)x2 + 0.5f); by = ovMapY((float)y2 + 0.5f);
    glLineWidth(s_ov.lineW);
    glBegin(GL_LINES);
    glVertex2f(ax, ay);
    glVertex2f(bx, by);
    glEnd();
    /* Cap only a genuine segment. A zero-length "line" is a point — notably the
     * distant-aircraft single dot (drawViewportLine(x,y,x,y)); GL already draws that
     * as one fragment, and corner caps would balloon it into a fat white block over
     * the 3D scene. Leave it as the plain dot the game has always drawn. */
    if (s_ov.lineW > 1.5f && !(x1 == x2 && y1 == y2)) {
        capLineEnds(ax, ay, bx, by, s_ov.lineW);
    }
}

/* One virtual-pixel cell (scaleX x scaleY) so points keep their footprint at
 * native, non-square size (pitch-ladder marks, submitted text pixels). */
void r3dgl_drawPoint(int x, int y, int color) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Color c;
    float x0, y0, x1q, y1q;
    if (!pal) return;
    c = pal->colors[color & 0xff];
    overlay2DState();
    x0 = ovMapX((float)x);
    y0 = ovMapY((float)y);
    x1q = x0 + s_ov.scaleX;
    y1q = y0 + s_ov.scaleY;
    glColor3ub(c.r, c.g, c.b);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1q, y0);
    glVertex2f(x1q, y1q);
    glVertex2f(x0, y1q);
    glEnd();
}

/* Filled axis-aligned rect with INCLUSIVE 320-space bounds, matching fillSpanRect's
 * pixel extent (x0..x1, y0..y1). Used for the per-frame MFD backdrops (target-view
 * horizon, radar-scope background) and the damage flash — the fills that must land
 * in true call order under the immediate MFD/scope content instead of baking. */
void r3dgl_drawRect(int x0, int y0, int x1, int y1, int color) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Color c;
    float qx0, qy0, qx1, qy1;
    if (!pal || x1 < x0 || y1 < y0) return;
    c = pal->colors[color & 0xff];
    overlay2DState();
    qx0 = ovMapX((float)x0);
    qy0 = ovMapY((float)y0);
    qx1 = ovMapX((float)(x1 + 1));
    qy1 = ovMapY((float)(y1 + 1));
    glColor3ub(c.r, c.g, c.b);
    glBegin(GL_QUADS);
    glVertex2f(qx0, qy0);
    glVertex2f(qx1, qy0);
    glVertex2f(qx1, qy1);
    glVertex2f(qx0, qy1);
    glEnd();
}

/* Filled convex tile face (left-MFD terrain map). Vertices are 320-space x,y pairs
 * submitted UNCLIPPED and scissored to the MFD rect (clipX0,clipY0)-(clipX1,clipY1),
 * half-open, so a face crossing the border fills correctly with a crisp edge. */
void r3dgl_drawPoly(const short *xy, int n, int color,
                    int clipX0, int clipY0, int clipX1, int clipY1) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Color c;
    int k;
    if (!pal || n < 3) return;
    c = pal->colors[color & 0xff];
    overlay2DState();
    ovClipScissor(clipX0, clipY0, clipX1, clipY1);
    glColor3ub(c.r, c.g, c.b);
    glBegin(GL_POLYGON);
    for (k = 0; k < n; k++)
        glVertex2f(ovMapX((float)xy[k * 2] + 0.5f), ovMapY((float)xy[k * 2 + 1] + 0.5f));
    glEnd();
    glDisable(GL_SCISSOR_TEST);
}

/* Fill one float-cornered quad (a rotated, sub-grid font texel of a HUD label),
 * scissored to the half-open clip rect. Cell-corner convention (no +0.5), matching
 * r3dgl_drawPoint's virtual-pixel footprint so rotated labels sit on their lines. */
void r3dgl_drawQuadF(const float *xy, int color, int cx0, int cy0, int cx1, int cy1) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Color c;
    int k;
    if (!pal) return;
    c = pal->colors[color & 0xff];
    overlay2DState();
    ovClipScissor(cx0, cy0, cx1, cy1);
    glColor3ub(c.r, c.g, c.b);
    glBegin(GL_QUADS);
    for (k = 0; k < 4; k++)
        glVertex2f(ovMapX(xy[k * 2]), ovMapY(xy[k * 2 + 1]));
    glEnd();
    glDisable(GL_SCISSOR_TEST);
}

/* Radar/MFD line with fractional endpoints (no whole-pixel wobble); its ends are
 * cut by a GL scissor at the true MFD edge (cx0,cy0)-(cx1,cy1) rather than a
 * geometry clip — a crisp screen-edge cut, not a slanted butt-cap short of it.
 * widthScale multiplies the base per-virtual-pixel line width: the radar/MFD uses
 * 0.5 (thin scope look), the HUD target box 1.0 (full weight, matching r3dgl_drawLine). */
void r3dgl_drawScopeLine(float x1, float y1, float x2, float y2, int color,
                         int cx0, int cy0, int cx1, int cy1, float widthScale) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Color c;
    float w;
    if (!pal) return;
    c = pal->colors[color & 0xff];
    overlay2DState();
    ovClipScissor(cx0, cy0, cx1, cy1);
    w = s_ov.lineW * widthScale;
    glLineWidth(w < 1.0f ? 1.0f : w);
    glColor3ub(c.r, c.g, c.b);
    {
        float ax = ovMapX(x1 + 0.5f), ay = ovMapY(y1 + 0.5f);
        float bx = ovMapX(x2 + 0.5f), by = ovMapY(y2 + 0.5f);
        glBegin(GL_LINES);
        glVertex2f(ax, ay);
        glVertex2f(bx, by);
        glEnd();
        /* Full-weight (widthScale >= 1) scope lines are the HUD target box/hexagon,
         * closed shapes whose corners gap under GL butt caps; cap their ends so the
         * joins close (still under the scissor, so a cap near a viewport edge is cut
         * with the line). The thin 0.5 radar grid stays uncapped — its lines cross
         * rather than share corners, and caps would blob the intersections. */
        if (widthScale >= 1.0f && w > 1.5f)
            capLineEnds(ax, ay, bx, by, w);
    }
    glDisable(GL_SCISSOR_TEST);
}

/* Shrink a device-space rect to the largest centred sub-rect with the source's
 * srcW:srcH aspect (square pixels). HD art is authored square, so it fills its
 * footprint region without the 320-space 4:3 stretch that paletted sprites want
 * (the same rigid-square principle r3dgl_drawImageRot already uses for rotated
 * contacts). Placement still comes from the per-axis footprint mapping; only the
 * art's own grid is left unstretched. */
static void fitContainSquare(float *x0, float *y0, float *x1, float *y1, int srcW, int srcH) {
    float rw = *x1 - *x0, rh = *y1 - *y0, sc, dw, dh;
    if (srcW <= 0 || srcH <= 0 || rw <= 0.0f || rh <= 0.0f) return;
    sc = (rw / (float)srcW < rh / (float)srcH) ? rw / (float)srcW : rh / (float)srcH;
    dw = (float)srcW * sc;
    dh = (float)srcH * sc;
    *x0 += (rw - dw) * 0.5f;
    *y0 += (rh - dh) * 0.5f;
    *x1 = *x0 + dw;
    *y1 = *y0 + dh;
}

/* One sprite/HD image as a textured quad from the per-image decode cache. Source
 * sub-rect (srcX,srcY,imgW,imgH); destination footprint (dstW,dstH) in 320-space
 * may differ from the source size (a scaled HD sprite). key<0 opaque (blend off,
 * index 0 drawn as its colour); key>=0 transparent on index 0 (baked alpha). */
void r3dgl_drawImage(R2DImage *img, int srcX, int srcY, int imgW, int imgH,
                     int dstX, int dstY, int dstW, int dstH, int key) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Surface *surf = r2d_imageSurface(img);
    GLuint itex;
    float u0, v0, u1, v1, x0, y0, x1q, y1q;
    if (!surf || !pal) return;
    itex = imageTexture(img, pal, gfx_paletteGeneration());
    if (!itex) return;
    overlay2DState();
    glEnable(GL_TEXTURE_2D);
    glColor3ub(255, 255, 255); /* MODULATE: white so the texture passes through */
    if (key < 0) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, itex);
    /* Full-frame texcoords into the sheet; pixel-snapping the quad keeps every
     * fragment centre strictly inside the frame so NEAREST never bleeds across a
     * sprite-sheet boundary. */
    u0 = (float)srcX / (float)surf->w;
    v0 = (float)srcY / (float)surf->h;
    u1 = (float)(srcX + imgW) / (float)surf->w;
    v1 = (float)(srcY + imgH) / (float)surf->h;
    if (surf->format != SDL_PIXELFORMAT_INDEX8) {
        /* HD (RGBA) art: place in the footprint region, don't stretch the art. */
        x0 = ovMapX((float)dstX);
        y0 = ovMapY((float)dstY);
        x1q = x0 + (float)dstW * s_ov.scaleX;
        y1q = y0 + (float)dstH * s_ov.scaleY;
        fitContainSquare(&x0, &y0, &x1q, &y1q, imgW, imgH);
        x0 = SDL_floorf(x0 + 0.5f);
        y0 = SDL_floorf(y0 + 0.5f);
        x1q = SDL_floorf(x1q + 0.5f);
        y1q = SDL_floorf(y1q + 0.5f);
    } else {
        x0 = SDL_floorf(ovMapX((float)dstX) + 0.5f);
        y0 = SDL_floorf(ovMapY((float)dstY) + 0.5f);
        x1q = x0 + SDL_floorf((float)dstW * s_ov.scaleX + 0.5f);
        y1q = y0 + SDL_floorf((float)dstH * s_ov.scaleY + 0.5f);
    }
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1q, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1q, y1q);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1q);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

/* Like r3dgl_drawImage but with a fractional 320-space destination: the quad is
 * NOT snapped to whole device pixels, so a sprite whose logical position moves
 * sub-pixel per frame (a radar blip tracking the gliding scope grid) slides
 * smoothly instead of stepping. Texcoords are inset half a source texel so NEAREST
 * still can't bleed across a sprite-sheet cell boundary without the quad snapping. */
void r3dgl_drawImageF(R2DImage *img, int srcX, int srcY, int imgW, int imgH,
                      float dstX, float dstY, float dstW, float dstH, int key) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Surface *surf = r2d_imageSurface(img);
    GLuint itex;
    float u0, v0, u1, v1, x0, y0, x1q, y1q, hu, hv;
    if (!surf || !pal) return;
    itex = imageTexture(img, pal, gfx_paletteGeneration());
    if (!itex) return;
    overlay2DState();
    glEnable(GL_TEXTURE_2D);
    glColor3ub(255, 255, 255);
    if (key < 0) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, itex);
    hu = 0.5f / (float)surf->w;
    hv = 0.5f / (float)surf->h;
    u0 = (float)srcX / (float)surf->w + hu;
    v0 = (float)srcY / (float)surf->h + hv;
    u1 = (float)(srcX + imgW) / (float)surf->w - hu;
    v1 = (float)(srcY + imgH) / (float)surf->h - hv;
    x0 = ovMapX(dstX);
    y0 = ovMapY(dstY);
    x1q = x0 + dstW * s_ov.scaleX;
    y1q = y0 + dstH * s_ov.scaleY;
    /* HD art keeps square pixels; the fractional (unsnapped) quad glides smoothly. */
    if (surf->format != SDL_PIXELFORMAT_INDEX8)
        fitContainSquare(&x0, &y0, &x1q, &y1q, imgW, imgH);
    glBegin(GL_QUADS);
    glTexCoord2f(u0, v0); glVertex2f(x0, y0);
    glTexCoord2f(u1, v0); glVertex2f(x1q, y0);
    glTexCoord2f(u1, v1); glVertex2f(x1q, y1q);
    glTexCoord2f(u0, v1); glVertex2f(x0, y1q);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

/* Like r3dgl_drawImageF but the quad is rotated by angleRad (clockwise on screen)
 * about the 320-space centre (cx,cy). Used by the HD radar path so a single base
 * sprite spins smoothly to a contact's heading instead of snapping to one of the
 * atlas's hand-drawn rotation frames. The device-space half-extents use scaleX on
 * both axes so the icon stays rigid (square) as it turns rather than shearing with
 * the non-square 320x200 pixel aspect. */
void r3dgl_drawImageRot(R2DImage *img, int srcX, int srcY, int imgW, int imgH,
                        float cx, float cy, float dstW, float dstH,
                        float angleRad, int key) {
    SDL_Palette *pal = gfx_getPalette();
    SDL_Surface *surf = r2d_imageSurface(img);
    GLuint itex;
    float u0, v0, u1, v1, hu, hv;
    float dcx, dcy, hw, hh, cs, sn;
    /* corner offsets (device px, y-down): TL, TR, BR, BL — matching the texcoords */
    static const float cxoff[4] = { -1.0f,  1.0f,  1.0f, -1.0f };
    static const float cyoff[4] = { -1.0f, -1.0f,  1.0f,  1.0f };
    float u[4], vtc[4];
    int k;
    if (!surf || !pal) return;
    itex = imageTexture(img, pal, gfx_paletteGeneration());
    if (!itex) return;
    overlay2DState();
    glEnable(GL_TEXTURE_2D);
    glColor3ub(255, 255, 255);
    if (key < 0) {
        glDisable(GL_BLEND);
    } else {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, itex);
    hu = 0.5f / (float)surf->w;
    hv = 0.5f / (float)surf->h;
    u0 = (float)srcX / (float)surf->w + hu;
    v0 = (float)srcY / (float)surf->h + hv;
    u1 = (float)(srcX + imgW) / (float)surf->w - hu;
    v1 = (float)(srcY + imgH) / (float)surf->h - hv;
    u[0] = u0; vtc[0] = v0;
    u[1] = u1; vtc[1] = v0;
    u[2] = u1; vtc[2] = v1;
    u[3] = u0; vtc[3] = v1;
    dcx = ovMapX(cx);
    dcy = ovMapY(cy);
    hw = dstW * s_ov.scaleX * 0.5f;
    hh = dstH * s_ov.scaleX * 0.5f;
    cs = SDL_cosf(angleRad);
    sn = SDL_sinf(angleRad);
    glBegin(GL_QUADS);
    for (k = 0; k < 4; k++) {
        float ox = cxoff[k] * hw, oy = cyoff[k] * hh;
        glTexCoord2f(u[k], vtc[k]);
        glVertex2f(dcx + ox * cs - oy * sn, dcy + ox * sn + oy * cs);
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

/* Draw a whole HD image scaled to the window HEIGHT (aspect preserved), full window
 * height, its left edge at window-x `leftWinX` — the shared body of the two window-
 * fill entry points below. Always alpha-blended. */
static void drawImageWindowFitH(R2DImage *img, float leftWinX) {
    SDL_Surface *surf = r2d_imageSurface(img);
    GLuint itex;
    float sc, dw, x0, x1q, y1q;
    if (!surf) return;
    itex = imageTexture(img, gfx_getPalette(), gfx_paletteGeneration());
    if (!itex) return;
    overlay2DState();
    glEnable(GL_TEXTURE_2D);
    glColor3ub(255, 255, 255);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, itex);
    sc = (float)s_ov.winH / (float)surf->h;
    dw = (float)surf->w * sc;
    x0 = leftWinX;
    x1q = x0 + dw;
    y1q = (float)s_ov.winH;
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2f(x0, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex2f(x1q, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex2f(x1q, y1q);
    glTexCoord2f(0.0f, 1.0f); glVertex2f(x0, y1q);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

/* Window-filling widescreen 2D art that lives OUTSIDE the 320-space overlay box,
 * scaled to the window height and CENTRED horizontally (sides cropped to fit) — the
 * briefing room backdrop. */
void r3dgl_drawImageWindow(R2DImage *img) {
    SDL_Surface *surf = r2d_imageSurface(img);
    float dw;
    if (!surf) return;
    dw = (float)surf->w * ((float)s_ov.winH / (float)surf->h);
    drawImageWindowFitH(img, ((float)s_ov.winW - dw) * 0.5f - s_ov.shake);
}

/* Same window-height scale as the centred room (so the cel matches the room art's
 * scale), but with the image's LEFT edge at 320-space `boxLeftX` mapped through the
 * 4:3 overlay letterbox — the briefing arm cels are authored at the room's height
 * (the forearm meets the officer's body) yet must sit in the 4:3 menu box they point
 * into. The cel is wider than the legacy arm, so left-aligning tucks the extra width
 * behind the officer while the pointer lands on the menu. */
void r3dgl_drawImageWindowBoxX(R2DImage *img, float boxLeftX) {
    drawImageWindowFitH(img, ovMapX(boxLeftX));
}

/* The retained page as a persistent GL texture + the dirty key it was built for.
 * Re-uploaded ONLY when its visible output could have changed (below), so the page
 * is NOT re-converted/re-uploaded every frame — the flight fire-palette cycle bumps
 * the global palette generation every frame but touches only its 9 fire entries, so
 * a cockpit that doesn't use them stays cached. */
static GLuint s_pageTex;
static unsigned s_pageKey;
static int s_pageTexW, s_pageTexH;

/* FNV-1a fold of the page's exact visible output: the pixel indices, the palette
 * RGB of ONLY the indices actually present (so fire-cycle changes to unused entries
 * don't force a re-upload), the show-through rects (they set the alpha), and the
 * page size. A change in any of these — and nothing else — re-uploads the texture. */
static unsigned pageOutputKey(SDL_Surface *page, SDL_Palette *pal) {
    unsigned k = 2166136261u;
    unsigned used[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const uint8 *src = (const uint8 *)page->pixels;
    int pitch = page->pitch, x, y, i;
    for (y = 0; y < page->h; y++) {
        const uint8 *row = src + (size_t)y * pitch;
        for (x = 0; x < page->w; x++) {
            uint8 idx = row[x];
            k = (k ^ idx) * 16777619u;
            used[idx >> 5] |= 1u << (idx & 31);
        }
    }
    for (i = 0; i < 256 && i < pal->ncolors; i++) {
        if (!(used[i >> 5] & (1u << (i & 31)))) continue;
        k = (k ^ (unsigned)pal->colors[i].r) * 16777619u;
        k = (k ^ (unsigned)pal->colors[i].g) * 16777619u;
        k = (k ^ (unsigned)pal->colors[i].b) * 16777619u;
    }
    for (i = 0; i < s_nShowRects; i++) {
        const SDL_Rect *r = &s_showRects[i];
        k = (k ^ (unsigned)r->x) * 16777619u;
        k = (k ^ (unsigned)r->y) * 16777619u;
        k = (k ^ (unsigned)r->w) * 16777619u;
        k = (k ^ (unsigned)r->h) * 16777619u;
    }
    k = (k ^ (unsigned)page->w) * 16777619u;
    k = (k ^ (unsigned)page->h) * 16777619u;
    return k;
}

/* Composite the retained 320x200 page (cockpit, gauges, menus, 2D screens) onto the
 * window as one letterboxed textured quad, transparent inside the 3D-viewport rect(s)
 * so the GL 3D beneath shows. On flight frames this runs at the MAIN-SCENE anchor
 * (gl_endScene), BEFORE the frame's immediate HUD/MFD draws, so the 2D composes over
 * it in true call order; on pure-2D screens (no 3D pass) it runs at present. Does not
 * clear when a 3D scene is live. The RGBA convert + texture upload happen only when
 * the page's visible output changed (pageOutputKey); the quad itself draws every
 * frame (cheap, and the screen-shake offset applies here without a re-upload). */
static void composePageBackdrop(SDL_Surface *page, int shakeOffset) {
    int win_w, win_h, w, h, lbx, lby, qw, qh;
    SDL_Palette *pal;
    float shake, scaleX, scaleY;
    unsigned key;

    if (!page) return;
    w = page->w;
    h = page->h;
    pal = gfx_getPalette();
    if (!pal) return;

    key = pageOutputKey(page, pal);
    if (!s_pageTex || key != s_pageKey || w != s_pageTexW || h != s_pageTexH) {
        int x, y, pitch;
        const uint8 *src;
        if (!ensureRgbaScratch(w * h * 4)) { s_nShowRects = 0; return; }
        src = (const uint8 *)page->pixels;
        pitch = page->pitch;
        for (y = 0; y < h; y++) {
            const uint8 *row = src + (size_t)y * pitch;
            uint8 *out = s_rgba + (size_t)y * w * 4;
            for (x = 0; x < w; x++) {
                uint8 idx = row[x];
                SDL_Color c = pal->colors[idx];
                out[x * 4 + 0] = c.r;
                out[x * 4 + 1] = c.g;
                out[x * 4 + 2] = c.b;
                /* Transparent inside the 3D viewport rect(s) so the GL 3D beneath
                 * shows; opaque everywhere else (cockpit/panel). */
                out[x * 4 + 3] = inShowRect(x, y) ? 0 : 255;
            }
        }
        if (!s_pageTex) glGenTextures(1, &s_pageTex);
        glBindTexture(GL_TEXTURE_2D, s_pageTex);
        setOverlayTexParams();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);
        s_pageKey = key;
        s_pageTexW = w;
        s_pageTexH = h;
    }
    s_nShowRects = 0; /* consumed for this frame */

    SDL_GetWindowSizeInPixels(s_win, &win_w, &win_h);
    /* Letterbox the page into the window through the shared r2d mapping (deriving
     * the virtual size from the page itself, so the 320x200 overlay and the
     * 640x350 hi-res title both map correctly) — matches the 3D viewport's
     * letterbox so the overlay stays aligned and unstretched. */
    {
        R2DMapping m;
        r2d_computeMapping(w, h, win_w, win_h, 0, &m);
        scaleX = m.scaleX;
        scaleY = m.scaleY;
        qw = (int)(w * scaleX);
        qh = (int)(h * scaleY);
        lbx = m.offX;
        lby = m.offY;
    }
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glShadeModel(GL_FLAT);

    /* On flight frames the GL 3D is already in the framebuffer (bars cleared black
     * by gl_beginScene) and the viewport rect reveals it, so we must not clear. On
     * menu/briefing/debrief frames there is no live 3D underneath, so clear the whole
     * window to blacken the letterbox bars before the opaque quad lands. */
    if (!s_sceneRendered) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glColor3ub(255, 255, 255);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1); /* y-down: texture row 0 at top */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s_pageTex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shake = shakeOffset * scaleX;
    {
        float x0 = lbx - shake, y0 = (float)lby;
        float x1 = lbx + qw - shake, y1 = (float)(lby + qh);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f(x0, y0);
        glTexCoord2f(1, 0); glVertex2f(x1, y0);
        glTexCoord2f(1, 1); glVertex2f(x1, y1);
        glTexCoord2f(0, 1); glVertex2f(x0, y1);
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

void r3dgl_present(SDL_Surface *page, int shakeOffset) {
    if (!page || !s_active) return;
    /* Flight frames composed the page backdrop at the main-scene anchor (before the
     * frame's immediate HUD/MFD draws); only pure-2D screens (no 3D pass) compose it
     * here, on top of the black-cleared window. */
    if (!s_sceneRendered && !s_pageComposited) composePageBackdrop(page, shakeOffset);
    /* Remember whether THIS present carried a live 3D view. gfx_repaint (window
     * expose/resize) re-presents only the page — which would blank the GL 3D (it
     * lives in the framebuffer, not the page) — so on a flight frame it must instead
     * leave the last composited frame alone (r3dgl_flightLive). */
    s_glFlightLive = s_sceneRendered;
    s_sceneRendered = 0;
    s_pageComposited = 0;
    r2d_vectorMarkPresented();
    SDL_GL_SwapWindow(s_win);
}

int r3dgl_flightLive(void) { return s_glFlightLive; }
