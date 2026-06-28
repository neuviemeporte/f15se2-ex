/* OpenGL 1.x 3D backend (see docs/render-3d-backend.md, Step 3) — the GPU path.
 *
 * Implements the R3DBackend 3D vtable against the *decoded* meshes (r3dmesh.c)
 * and owns the GL context + the interim 2D-overlay composite. The 3D main view
 * renders through GL at native window resolution with a real depth buffer and
 * double-sided fill (no back-face cull — cheap on a GPU, fixes over-optimized
 * models). The MFD/target sub-view (renderScene == 0) is delegated to the
 * software backend, which draws it into the page as before.
 *
 * Transform faithfulness: rather than re-derive the camera math in float (and
 * risk a matrix-convention bug), each object reuses the exact integer
 * orientation+origin the software path computes (r3d_objTransformFar), and the
 * decoded mesh vertices are projected with a GL matrix built to reproduce the
 * software perspective divide (the >>8 focal scale, 3/4 Y aspect, depth divide).
 * Pixels may drift from the integer rasterizer; behaviour does not (Q1).
 *
 * Compositing (interim): the software 2D page is drawn over the GL 3D as a
 * single flat textured quad with the show-through key (GFX_GL_SHOWTHROUGH_KEY)
 * made transparent, so the HUD/cockpit land on top of the GL viewport. A later
 * step submits 2D through the renderer too (docs/render-2d-overlay.md).
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "r3d.h"
#include "r3d_gl.h"
#include "r2d.h"
#include "r3dmesh.h"
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

void r3dgl_setGLAttributes(void) {
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

int r3dgl_initContext(SDL_Window *win) {
    s_win = win;
    s_ctx = SDL_GL_CreateContext(win);
    if (!s_ctx) {
        LogCritical(("GL context creation failed: %s", SDL_GetError()));
        return 0;
    }
    SDL_GL_MakeCurrent(win, s_ctx);
    SDL_GL_SetSwapInterval(1);
    {
        GLint depthBits = 0;
        glGetIntegerv(GL_DEPTH_BITS, &depthBits);
        LogInfo(("GL: %s / %s, depth bits=%d",
                 (const char *)glGetString(GL_RENDERER),
                 (const char *)glGetString(GL_VERSION), (int)depthBits));
    }
    s_active = 1;
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

/* ---- scene state -------------------------------------------------------- */

static int s_delegating;     /* current scene routed to the software backend */
static int s_sceneRendered;  /* a GL 3D main view was drawn this frame (live under the present) */
static int s_wide = -1;      /* widescreen 3D (Hor+): -1 = not yet read from F15_WIDESCREEN */
static float s_proj[16];     /* column-major GL projection for the active scene */
static float s_pixelScale;   /* letterbox scale: window pixels per 320-space pixel (point size) */
static float s_vpW, s_vpH;   /* active 3D viewport size in window pixels (screen-span tests) */

/* Deferred submission for painter's-order rendering. The game's surfaces are
 * paper-thin and frequently coplanar, which no z-buffer can resolve, so — like the
 * DOS original — we render with the depth test OFF and rely on draw order: objects
 * farthest-first by the LOD-normalized origin depth (insertSortedObject's key),
 * back-face culled, faces in display-list order. */
typedef struct {
    char *model;
    int16 combined[9];
    long camBase, camX, camY; /* camera-space origin axes (screen-X, screen-Y, depth) */
    int shade, colorBase, curLod;
    int dirX, dirY, dirZ; /* object facing, for the per-face back-face cull */
    int sortHi, sortLo;   /* normalized origin depth (sort key, farthest = largest) */
    int immediate;        /* flat ground/sea (posZ==0, no sort flag): drawn first/behind */
    int seq;              /* submission index; preserves walk order among immediate objects */
} GlSub;
#define GL_MAX_SUBS 4096
static GlSub s_subs[GL_MAX_SUBS];
static int s_nSub;
static int s_subOverflow;

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

static const char *gl_name(void) { return "opengl1"; }

static int gl_init(void) { return s_active; } /* claims iff the context came up */
static void gl_shutdown(void) {}

static R3DMesh gl_registerMesh(R3DMesh raw) { return raw; } /* decoded per submit */
static void gl_releaseMesh(R3DMesh mesh) { (void)mesh; }

/* Build the GL projection matrix that reproduces projectVertexToScreen for a
 * viewport of Wv x Wh (320-space) with principal point (cx, cy) and a far depth
 * gate. The submitted vertex is camera-space (camX, camY, depth/65536). */
static void buildProjection(int Wv, int Hv, int cx, int cy, float fGate) {
    float ax = 1.0f / (128.0f * Wv);
    float az = 2.0f * (float)cx / Wv - 1.0f;
    float by = 3.0f / (512.0f * Hv);
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
        rearY[ringIx] = -(-(((i + j) * 0.25f) - i) + j) + g_viewCenterY;
        foreY[ringIx] = (((i - j) * 0.25f) + g_viewCenterY) - i + j;
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
        rearY[ringIx] = -(-(((i + j) * 0.25f) - i) + j) + g_viewCenterY;
        foreY[ringIx] = (((i - j) * 0.25f) + g_viewCenterY) - i + j;
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

static void gl_beginScene(const R3DScene *s) {
    int win_w, win_h, vpTop, vpBot, vpLeft, vpRight, Wv, Hv, lbx, lby;
    float scale, fGate, sphOrtho[4];
    int16 skyIdx;
    SDL_Surface *page;

    /* The tiny MFD/target sub-view stays on the software rasterizer (it draws
     * into the page and composites as plain 2D); GL takes only the main view. */
    if (s->renderScene == 0) {
        s_delegating = 1;
        r3d_setObjCullWiden(1, 1, 1, 1); /* MFD/target sub-view stays 4:3 */
        r3d_softwareBackend.beginScene(s);
        return;
    }
    s_delegating = 0;

    if (s_wide < 0) {
        /* Widescreen 3D (Hor+): on by default; F15_WIDESCREEN=0 forces 4:3. The
         * stock UI stays a centred, pillarboxed 320x200 image (it never widens);
         * only the 3D fills the extra width (docs/render-2d-overlay.md). */
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
        r2d_computeMapping(LOGICAL_WIDTH, LOGICAL_HEIGHT, win_w, win_h, &m);
        scale = m.scale;
        lbx = m.offX;
        lby = m.offY;
    }
    s_pixelScale = scale;

    /* Clear the whole window (including the letterbox bars) to black; the 3D
     * viewport region is re-cleared to the sky colour once scissored below. */
    glViewport(0, 0, win_w, win_h);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* GL viewport origin is bottom-left; the 320-space rect is top-down. The
     * uniform letterbox scale keeps the same proportions as 640x400 at any size.
     * gx/gw/gy/gh is the centred 4:3 sub-rect the letterboxed UI quad occupies. */
    {
        int gx = lbx + (int)(vpLeft * scale);
        int gw = (int)(Wv * scale);
        int gh = (int)(Hv * scale);
        int gy = win_h - (lby + (int)(vpTop * scale)) - gh;
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
            sphOrtho[0] = -(float)gx / scale;                                  /* left  */
            sphOrtho[1] = sphOrtho[0] + (float)win_w / scale;                  /* right */
            sphOrtho[3] = -((float)lby + (float)vpTop * scale) / scale;        /* top   */
            sphOrtho[2] = ((float)win_h - ((float)lby + (float)vpTop * scale)) / scale; /* bottom */
        } else {
            glViewport(gx, gy, gw, gh);
            glScissor(gx, gy, gw, gh);
            s_vpW = (float)gw;
            s_vpH = (float)gh;
            r3d_setObjCullWiden(1, 1, 1, 1); /* 4:3: original cull */
            sphOrtho[0] = 0.0f; sphOrtho[1] = (float)Wv;
            sphOrtho[2] = (float)Hv; sphOrtho[3] = 0.0f;
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
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Sky/ground background sphere (detail >= 3) drawn first (farthest), then the
     * 3D projection is restored for the objects. Painter's order with the depth
     * test OFF — objects are sorted + drawn back-to-front in gl_endScene. */
    if ((char)g_detailLevel >= 3)
        glDrawSphere(sphOrtho[0], sphOrtho[1], sphOrtho[2], sphOrtho[3]);
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(s_proj);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    /* Make the page's viewport region show-through so the 2D composite reveals
     * the GL 3D under it; the HUD/cockpit then draw over it (non-key, opaque). */
    page = gfx_getCurPageSurface();
    if (page) {
        SDL_Rect rc = {vpLeft, vpTop, Wv, Hv};
        SDL_FillSurfaceRect(page, &rc, GFX_GL_SHOWTHROUGH_KEY);
    }
    s_nSub = 0;
    s_subOverflow = 0;
    s_sceneRendered = 1;
}

static void gl_submit(const R3DSubmit *o) {
    GlSub *r;
    int16 combined[9];
    long camBase, camTransX, camTransY, depth;
    int shade, dirX, dirY, dirZ, shift, i;

    if (s_delegating) {
        r3d_softwareBackend.submit(o);
        return;
    }
    if (s_nSub >= GL_MAX_SUBS) {
        s_subOverflow++;
        return;
    }

    if (r3d_objTransformFar((char far *)o->mesh, o->yaw, o->pitch, o->roll,
                            o->posX, o->posY, o->posZ,
                            combined, &camBase, &camTransX, &camTransY, &shade,
                            &dirX, &dirY, &dirZ))
        return; /* frustum-culled */

    r = &s_subs[s_nSub];
    r->model = (char *)o->mesh;
    for (i = 0; i < 9; i++) r->combined[i] = combined[i];
    r->camBase = camBase;
    r->camX = camTransX;
    r->camY = camTransY;
    r->shade = shade;
    r->colorBase = g_objColorBase;
    r->curLod = g_curLod;
    r->dirX = dirX;
    r->dirY = dirY;
    r->dirZ = dirZ;

    /* Immediate (drawn first/behind the sorted queue) iff flat at world Z=0 with no
     * sort flag, exactly as projectSceneObject classifies it. This keeps flat
     * ground/sea behind elevated objects instead of a near tile overpainting them. */
    r->immediate = (o->posZ == 0 &&
                    !peekSortFlag((const uint8 *)o->mesh,
                                  (const uint8 *)g_world3dData + WORLD3D_DATA_SIZE));
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

/* Distance-scaled single-pixel decoration. The original drew these as a fixed 1px
 * dot at any range; here the point shrinks with camera depth (and, with smooth-point
 * coverage, the far sub-pixel ones fade out) so the scattered ground decorations read
 * as receding into the distance — the depth cue they exist to give. Pixel drift from
 * the integer path is accepted (Q1). REF_DEPTH (a depthHi of 2500) maps to one logical
 * pixel and aligns with the edge-run colour-shade bands. */
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
    glPointSize(sz);
    glColorIndex(colorIdx);
    glBegin(GL_POINTS);
    glVertex3f(x, y, (float)depth32 / 65536.0f);
    glEnd();
}

/* Decode + transform + draw one object (painter's order; depth test already off).
 * Re-decodes the mesh (cheap; avoids caching across region reloads). */
static void drawSub(const GlSub *r) {
    MeshVtxPools pools;
    MeshLod *l;
    int i, shift;
    float cm[9], scaleDiv;
    static float vx_[R3DMESH_MAX_VERTS], vy_[R3DMESH_MAX_VERTS], vd_[R3DMESH_MAX_VERTS];
    static uint8 backFacing[R3DMESH_MAX_NORMALS];

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
        glEnable(GL_POINT_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawDepthPoint((float)r->camBase, (float)r->camX, r->camY,
                       colorLut[l->pointColor] + r->shade);
        glDisable(GL_BLEND);
        glDisable(GL_POINT_SMOOTH);
        return;
    }

    /* EDGERUN (sceneObjEdgeRun): a run of distance-shaded points, each a shared-pool
     * vertex transformed by the object matrix (the on-the-fly path's emitModelVertex).
     * No LOD scale (emitModelVertex applies none), so depth feeds glEdgeRunColor raw. */
    if (l->form == MESH_FORM_EDGERUN) {
        glEnable(GL_POINT_SMOOTH);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
        glDisable(GL_BLEND);
        glDisable(GL_POINT_SMOOTH);
        return;
    }

    if (l->form != MESH_FORM_MODEL) return;

    /* Back-face cull matching the original (rotatePoint3d): a face is hidden when
     * its gating normal faces away, dot(normal, objDir) < threshold. */
    for (i = 0; i < l->nNormals; i++) {
        MeshNormal *nrm = &l->normals[i];
        long dot = (long)nrm->nx * r->dirX + (long)nrm->ny * r->dirZ + (long)nrm->nz * r->dirY;
        backFacing[i] = (dot < (long)nrm->threshold) ? 1 : 0;
    }

    /* The LOD coordinate scale (8 - 2*curLod) cancels in the x/y projection ratio
     * (camX/depth); applied here only to keep depths consistent for the perspective
     * divide across LODs. */
    shift = 8 - 2 * r->curLod;
    if (shift < 0) shift = 0;
    scaleDiv = (float)(1 << shift);

    for (i = 0; i < 9; i++) cm[i] = (float)r->combined[i];

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
        if (nProj == l->nVerts && nProj > 0 &&
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
                glEnable(GL_POINT_SMOOTH);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                drawDepthPoint((float)r->camBase, (float)r->camX, r->camY,
                               colorLut[colorByte] + r->shade);
                glDisable(GL_BLEND);
                glDisable(GL_POINT_SMOOTH);
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
        /* Back-face cull (original's per-face normal sign test). */
        if (f->cullNormal < l->nNormals && backFacing[f->cullNormal]) continue;
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

        glColorIndex(colorLut[f->colorByte] + r->shade);
        glBegin(GL_POLYGON);
        for (k = 0; k < n; k++) glVertex3f(vx_[ring[k]], vy_[ring[k]], vd_[ring[k]]);
        glEnd();
    }

    /* Wire primitives. */
    if (l->nLines) {
        glBegin(GL_LINES);
        for (i = 0; i < l->nLines; i++) {
            MeshEdge *e = &l->edges[l->lines[i].edge];
            glColorIndex(colorLut[l->lines[i].colorByte] + r->shade);
            if (e->va < l->nVerts && e->vb < l->nVerts) {
                glVertex3f(vx_[e->va], vy_[e->va], vd_[e->va]);
                glVertex3f(vx_[e->vb], vy_[e->vb], vd_[e->vb]);
            }
        }
        glEnd();
    }
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

static void gl_endScene(void) {
    int i;

    if (s_delegating) {
        r3d_softwareBackend.endScene();
        s_delegating = 0;
        return;
    }

    if (s_subOverflow)
        LogWarn(("r3d_gl: %d submissions dropped (cap %d)", s_subOverflow, GL_MAX_SUBS));
    /* Draw back-to-front (no depth test) — the original's painter's algorithm, the
     * only thing that resolves the game's coplanar paper-thin surfaces. */
    qsort(s_subs, s_nSub, sizeof(GlSub), subCmp);
    for (i = 0; i < s_nSub; i++) drawSub(&s_subs[i]);
    s_nSub = 0;
    glDisable(GL_SCISSOR_TEST);
}

const R3DBackend r3d_glBackend = {
    gl_name,
    gl_init,
    gl_shutdown,
    gl_registerMesh,
    gl_releaseMesh,
    gl_beginScene,
    gl_submit,
    gl_endScene,
};

/* ---- 2D overlay composite + present ------------------------------------ */

static uint8 *s_rgba;       /* INDEX8 -> RGBA8888 scratch for the page texture */
static int s_rgbaCap;

void r3dgl_present(SDL_Surface *page, int shakeOffset) {
    int win_w, win_h, w, h, x, y, lbx, lby, qw, qh;
    SDL_Palette *pal;
    const uint8 *src;
    int pitch, need;
    GLuint tex;
    float shake, scale;

    if (!page || !s_active) return;
    w = page->w;
    h = page->h;
    pal = gfx_getPalette();
    if (!pal) return;

    need = w * h * 4;
    if (need > s_rgbaCap) {
        SDL_free(s_rgba);
        s_rgba = (uint8 *)SDL_malloc(need);
        s_rgbaCap = s_rgba ? need : 0;
    }
    if (!s_rgba) return;

    src = (const uint8 *)page->pixels;
    pitch = page->pitch;
    for (y = 0; y < h; y++) {
        const uint8 *row = src + y * pitch;
        uint8 *out = s_rgba + y * w * 4;
        for (x = 0; x < w; x++) {
            uint8 idx = row[x];
            SDL_Color c = pal->colors[idx];
            out[x * 4 + 0] = c.r;
            out[x * 4 + 1] = c.g;
            out[x * 4 + 2] = c.b;
            out[x * 4 + 3] = (idx == GFX_GL_SHOWTHROUGH_KEY) ? 0 : 255;
        }
    }

    SDL_GetWindowSizeInPixels(s_win, &win_w, &win_h);
    /* Letterbox the page into the window through the shared r2d mapping (deriving
     * the virtual size from the page itself, so the 320x200 overlay and the
     * 640x350 hi-res title both map correctly) — matches the 3D viewport's
     * letterbox so the overlay stays aligned and unstretched. */
    {
        R2DMapping m;
        r2d_computeMapping(w, h, win_w, win_h, &m);
        scale = m.scale;
        qw = (int)(w * scale);
        qh = (int)(h * scale);
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

    /* On flight frames the GL 3D is already in the framebuffer (with the bars
     * cleared black by gl_beginScene) and the overlay's show-through key reveals
     * it, so we must not clear here. On menu/briefing/debrief frames there is no
     * live 3D underneath, so clear the whole window to blacken the letterbox bars
     * before the opaque overlay quad lands. */
    if (!s_sceneRendered) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    s_sceneRendered = 0;
    glColor3ub(255, 255, 255);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, win_w, win_h, 0, -1, 1); /* y-down: texture row 0 at top */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shake = shakeOffset * scale;
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

    glDeleteTextures(1, &tex);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);

    SDL_GL_SwapWindow(s_win);
}
