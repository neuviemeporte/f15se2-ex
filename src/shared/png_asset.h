/*
 * png_asset.h - Optional PNG replacements for legacy PIC/SPR assets.
 */
#ifndef PNG_ASSET_H
#define PNG_ASSET_H

struct SDL_Surface;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Load legacyFilename with a .png extension into an existing indexed surface.
 * Indexed PNGs preserve their palette; RGB/RGBA PNGs use the active game
 * palette, matching the legacy renderer's in-memory representation.
 */
int loadReplacementPng(const char *legacyFilename, struct SDL_Surface *destination);

/* Hooks used by the existing PIC entry points. */
int loadReplacementPngToPage(const char *legacyFilename, int page);
int loadReplacementPngToSprite(const char *legacyFilename, int segment);
int loadReplacementPngToHiResTitle(const char *legacyFilename);

#ifdef __cplusplus
}
#endif

#endif /* PNG_ASSET_H */
