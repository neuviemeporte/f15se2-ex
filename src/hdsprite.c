#include "hdsprite.h"
#include "r2d.h"
#include "log.h"
#include "inttype.h"
#include <SDL3/SDL.h>

/* The radar ownship's on-scope footprint (320-space), matching the original 7x7
 * gauge icon (blitGaugeSprite). Raise for a larger ownship marker. */
#define RADAR_OWNSHIP_FOOTPRINT 7

/* Resolve an HD asset path to an absolute location next to the executable.
 *
 * Callers pass paths relative to the bundled asset tree (e.g. "assets/flight/...").
 * Packaged builds (zip/tarball/AppImage/.app) are launched from an arbitrary working
 * directory, so a bare relative path would miss the assets that ship beside the
 * binary. SDL_GetBasePath() returns the directory the executable lives in (with a
 * trailing separator); we prefix it so the lookup is location-independent.
 *
 * Writes the resolved path into `out` and returns it, or returns the original `path`
 * unchanged when the base path is unavailable or the prefixed file does not exist
 * (preserving the historical working-directory-relative behavior, e.g. running the
 * freshly built binary straight out of the build dir). */
static const char *resolveAssetPath(const char *path, char *out, size_t outlen) {
    const char *base = SDL_GetBasePath();
    if (!base || !base[0]) return path;
    SDL_snprintf(out, outlen, "%s%s", base, path);
    /* Only prefer the executable-relative copy if it actually exists; otherwise fall
     * back to the bare path so a CWD-local assets/ dir still works. */
    if (!SDL_GetPathInfo(out, NULL)) return path;
    return out;
}

/* Load a PNG as an owned RGBA image, or NULL (missing/failed). GPU-gated by callers;
 * a missing asset is normal (the game ships with only the sprites drawn so far). */
static R2DImage *loadHdPng(const char *path) {
    char resolved[1024];
    const char *loadPath = resolveAssetPath(path, resolved, sizeof(resolved));
    SDL_Surface *raw = SDL_LoadPNG(loadPath);
    SDL_Surface *rgba;
    if (!raw) {
        LogInfo(("hdsprite: %s not loaded (%s); using legacy art", loadPath, SDL_GetError()));
        return NULL;
    }
    rgba = (raw->format == SDL_PIXELFORMAT_RGBA32)
               ? raw
               : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    if (rgba != raw) SDL_DestroySurface(raw);
    if (!rgba) return NULL;
    LogInfo(("hdsprite: loaded %s (%dx%d)", loadPath, rgba->w, rgba->h));
    return r2d_imageFromSurface(rgba);
}

/* Lazily loaded once; NULL means "no HD asset, use legacy". */
static R2DImage *radarOwnship(void) {
    static R2DImage *img;
    static int tried;
    if (tried) return img;
    tried = 1;
    if (!r2d_hasNativeOverlay()) return NULL; /* software: HD is GPU-only */
    img = loadHdPng("assets/flight/radar/self.png");
    return img;
}

/* Debrief theatre map: one HD PNG per theatre (parallel to endata's
 * theaterSprFiles). A missing file leaves the slot NULL → legacy SPR. */
static const char *const debriefMapPaths[8] = {
    "assets/end/map/lb.png", /* Libya */
    "assets/end/map/pg.png", /* Persian Gulf */
    "assets/end/map/vn.png", /* Vietnam */
    "assets/end/map/me.png", /* Middle East */
    "assets/end/map/nc.png", /* North Cape */
    "assets/end/map/ce.png", /* Central Europe */
    "assets/end/map/jp.png", /* Japan */
    "assets/end/map/na.png", /* North Africa */
};

int hdsprite_drawDebriefTheatreMap(int theatre) {
    static R2DImage *img[8];
    static int tried[8];
    R2DImage *hd;
    SDL_Surface *s;

    if (theatre < 0 || theatre >= 8 || !r2d_vectorActive()) return 0;
    if (!tried[theatre]) {
        tried[theatre] = 1;
        if (r2d_hasNativeOverlay()) img[theatre] = loadHdPng(debriefMapPaths[theatre]);
    }
    hd = img[theatre];
    if (!hd) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;

    /* Scale the whole HD image into the legacy map footprint (spriteMapAreaDef:
     * origin 8,10; 224×168 in 320-space) so it shares the overlay's 4:3-corrected
     * coordinate space — the flight-path lines and event markers are plotted in
     * that same space and must line up with the terrain. */
    r2d_submitImageScaled(hd, 0, 0, s->w, s->h, 8, 10, 224, 168, 0);
    return 1;
}

/* Pre-mission briefing (START mission-select) HD art: a window-filling widescreen
 * room/officer backdrop plus 7 pointer-arm poses. The arm poses are authored as
 * full-frame cels the same size as the wall (transparent except the forearm), so
 * they register over the wall with no per-frame placement — see hdsprite.h. */
#define BRIEFING_ARM_FRAMES 7

static R2DImage *briefingWall(void) {
    static R2DImage *img;
    static int tried;
    if (tried) return img;
    tried = 1;
    if (!r2d_hasNativeOverlay()) return NULL;
    img = loadHdPng("assets/start/menu/wall.png");
    return img;
}

int hdsprite_hasBriefingWall(void) { return briefingWall() != NULL; }

/* 320-space x for the LEFT edge of the arm cels within the 4:3 menu box. The legacy
 * arm sits at [60,174]; the HD cel, scaled to the room height, is wider (~173 units
 * at the reference window), so left-aligning to the box (0) lands the pointer near the
 * legacy 174 with the extra width tucked behind the officer. Raise toward 60 to match
 * the legacy left edge instead (pointer then overshoots to the right). */
#define BRIEFING_ARM_BOX_LEFT_X 0.0f

void hdsprite_drawBriefingWall(void) {
    R2DImage *w = briefingWall();
    if (w && r2d_vectorActive()) r2d_submitImageWindow(w); /* room centred */
}

void hdsprite_drawBriefingArm(int frame) {
    static R2DImage *img[BRIEFING_ARM_FRAMES];
    static int tried[BRIEFING_ARM_FRAMES];
    R2DImage *a;
    if (frame < 0 || frame >= BRIEFING_ARM_FRAMES) return;
    if (!tried[frame]) {
        tried[frame] = 1;
        if (r2d_hasNativeOverlay()) {
            char path[48];
            SDL_snprintf(path, sizeof(path), "assets/start/menu/arm/%d.png", frame);
            img[frame] = loadHdPng(path);
        }
    }
    a = img[frame];
    /* Arm cels are the officer's forearm; draw at the room's height scale with the
     * cel left-aligned in the 4:3 menu box, so the pointer reaches across the box to
     * the menu rows (see BRIEFING_ARM_BOX_LEFT_X). */
    if (a && r2d_vectorActive()) r2d_submitImageWindowBoxX(a, BRIEFING_ARM_BOX_LEFT_X);
}

int hdsprite_drawRadarOwnship(float destX, float destY) {
    R2DImage *hd = radarOwnship();
    SDL_Surface *s;
    int f = RADAR_OWNSHIP_FOOTPRINT;
    if (!hd || !r2d_vectorActive()) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    /* Centre the footprint on the sub-pixel (destX,destY) so the ownship marker
     * glides with the scope grid, matching the legacy icon's destX-3 centring. */
    return r2d_submitImageF(hd, 0, 0, s->w, s->h,
                            destX - f / 2.0f, destY - f / 2.0f, f, f, 0);
}

/* Radar scope contacts (middle-MFD gauge bank), drawn into the 7x7 footprint centred
 * on the sub-pixel blip (cx,cy). Each returns 1 if it submitted the HD art (caller
 * skips the atlas), 0 to fall back. GPU-only; always 0 on the software backend. */

/* Enemy aircraft, spun to the contact's relative heading (authored once nose-up).
 * altBand: 0 = co-altitude, 1 = below you, 2 = above you (egui.c). */
int hdsprite_drawRadarContact(int altBand, float cx, float cy, int angle16) {
    static R2DImage *img[3];
    static int tried[3];
    static const char *const paths[3] = {
        "assets/flight/radar/plane-level.png", /* co-altitude */
        "assets/flight/radar/plane-low.png",   /* below you */
        "assets/flight/radar/plane-high.png",  /* above you */
    };
    R2DImage *hd;
    SDL_Surface *s;
    float rad;
    if (altBand < 0 || altBand > 2 || !r2d_vectorActive()) return 0;
    if (!tried[altBand]) {
        tried[altBand] = 1;
        if (r2d_hasNativeOverlay()) img[altBand] = loadHdPng(paths[altBand]);
    }
    hd = img[altBand];
    if (!hd) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    rad = (float)(int16)angle16 * (float)(6.283185307179586 / 65536.0);
    return r2d_submitImageRot(hd, 0, 0, s->w, s->h, cx, cy,
                              (float)RADAR_OWNSHIP_FOOTPRINT, (float)RADAR_OWNSHIP_FOOTPRINT, rad, 0);
}

/* Fixed-orientation status blips selected by the scope's contact `code`: 1 = active
 * ("sam"), 5 = default ground contact ("bullseye"), 7 = special ("boat"). Other codes
 * (target, runway, …) return 0 so they keep the legacy atlas art. */
int hdsprite_drawRadarBlip(int code, float cx, float cy) {
    static R2DImage *img[3];
    static int tried[3];
    static const char *const paths[3] = {
        "assets/flight/radar/sam.png",      /* code 1 */
        "assets/flight/radar/bullseye.png", /* code 5 */
        "assets/flight/radar/boat.png",     /* code 7 */
    };
    int idx, f = RADAR_OWNSHIP_FOOTPRINT;
    R2DImage *hd;
    SDL_Surface *s;
    idx = code == 1 ? 0 : code == 5 ? 1 : code == 7 ? 2 : -1;
    if (idx < 0 || !r2d_vectorActive()) return 0;
    if (!tried[idx]) {
        tried[idx] = 1;
        if (r2d_hasNativeOverlay()) img[idx] = loadHdPng(paths[idx]);
    }
    hd = img[idx];
    if (!hd) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    return r2d_submitImageF(hd, 0, 0, s->w, s->h,
                            cx - f / 2.0f, cy - f / 2.0f, (float)f, (float)f, 0);
}

/* HUD reticles: lazily-loaded HD PNGs drawn into the legacy sprite footprint. The
 * footprint (320-space) equals the original blitSprite width/height so placement is
 * identical; the 1.2 present aspect then stretches it back toward square on screen. */
#define HUD_GUNRETICLE_W 11 /* legacy blitSprite(154, y, 0x94, 21, 11, 7, ...) */
#define HUD_GUNRETICLE_H 7
#define HUD_AAMSEEKER_W 13 /* legacy blitSprite(x, y, 0x91, 4, 13, 11, ...) */
#define HUD_AAMSEEKER_H 11

static R2DImage *loadHudReticle(const char *path) {
    if (!r2d_hasNativeOverlay()) return NULL; /* software: HD is GPU-only */
    return loadHdPng(path);
}

/* Fractional 320-space destination (destX,destY is the footprint's top-left) so the
 * reticle glides sub-pixel with the interpolated player state instead of snapping to
 * the 320x200 grid — the same native-res path the radar ownship and target boxes use. */
static int drawHudReticle(R2DImage *hd, float destX, float destY, int w, int h) {
    SDL_Surface *s;
    if (!hd || !r2d_vectorActive()) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    return r2d_submitImageF(hd, 0, 0, s->w, s->h, destX, destY, (float)w, (float)h, 0);
}

int hdsprite_drawHudGunReticle(float destX, float destY) {
    static R2DImage *img;
    static int tried;
    if (!tried) { tried = 1; img = loadHudReticle("assets/flight/hud/gun-reticle.png"); }
    return drawHudReticle(img, destX, destY, HUD_GUNRETICLE_W, HUD_GUNRETICLE_H);
}

int hdsprite_drawHudAamSeeker(float destX, float destY) {
    static R2DImage *img;
    static int tried;
    if (!tried) { tried = 1; img = loadHudReticle("assets/flight/hud/aam-seeker.png"); }
    return drawHudReticle(img, destX, destY, HUD_AAMSEEKER_W, HUD_AAMSEEKER_H);
}
