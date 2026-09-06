/*
 * structured_asset_replacement.h - Optional JSON-to-legacy stream bridge.
 */
#ifndef F15_STRUCTURED_ASSET_REPLACEMENT_H
#define F15_STRUCTURED_ASSET_REPLACEMENT_H

struct SDL_IOStream;

/*
 * Rebuild an editable WLD/3DT/3DG/full-3D3 JSON replacement into the exact byte
 * stream expected by the existing loaders. Returns NULL when no usable
 * replacement exists, so callers can transparently open the legacy asset.
 */
struct SDL_IOStream *openStructuredAssetReplacement(const char *legacyFilename);

#endif
