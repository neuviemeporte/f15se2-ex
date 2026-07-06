#include "hdsprite.h"
#include "r2d.h"
#include "log.h"
#include <SDL3/SDL.h>

/* The radar ownship's on-scope footprint (320-space), matching the original 7x7
 * gauge icon (blitGaugeSprite). Raise for a larger ownship marker. */
#define RADAR_OWNSHIP_FOOTPRINT 7

/* Load a PNG as an owned RGBA image, or NULL (missing/failed). GPU-gated by callers;
 * a missing asset is normal (the game ships with only the sprites drawn so far). */
static R2DImage *loadHdPng(const char *path) {
    SDL_Surface *raw = SDL_LoadPNG(path);
    SDL_Surface *rgba;
    if (!raw) {
        LogInfo(("hdsprite: %s not loaded (%s); using legacy art", path, SDL_GetError()));
        return NULL;
    }
    rgba = (raw->format == SDL_PIXELFORMAT_RGBA32)
               ? raw
               : SDL_ConvertSurface(raw, SDL_PIXELFORMAT_RGBA32);
    if (rgba != raw) SDL_DestroySurface(raw);
    if (!rgba) return NULL;
    LogInfo(("hdsprite: loaded %s (%dx%d)", path, rgba->w, rgba->h));
    return r2d_imageFromSurface(rgba);
}

/* Lazily loaded once; NULL means "no HD asset, use legacy". */
static R2DImage *radarOwnship(void) {
    static R2DImage *img;
    static int tried;
    if (tried) return img;
    tried = 1;
    if (!r2d_hasNativeOverlay()) return NULL; /* software: HD is GPU-only */
    img = loadHdPng("assets/flight/radar/ownship.png");
    return img;
}

int hdsprite_drawRadarOwnship(int destX, int destY) {
    R2DImage *hd = radarOwnship();
    SDL_Surface *s;
    int f = RADAR_OWNSHIP_FOOTPRINT;
    if (!hd || !r2d_vectorActive()) return 0;
    s = r2d_imageSurface(hd);
    if (!s) return 0;
    /* Centre the footprint on (destX,destY), matching the legacy icon's destX-3. */
    r2d_submitImageScaled(hd, 0, 0, s->w, s->h,
                          destX - f / 2, destY - f / 2, f, f, 0);
    return 1;
}
