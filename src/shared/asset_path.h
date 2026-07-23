/*
 * asset_path.h - Locate optional modern assets without changing legacy I/O.
 */
#ifndef ASSET_PATH_H
#define ASSET_PATH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve relativePath below F15_REPLACEMENT_ROOT.
 *
 * Components are matched case-insensitively because converted DOS assets
 * commonly retain uppercase names. Absolute paths and parent traversal are
 * rejected so a custom pack cannot escape its declared root.
 *
 * Returns non-zero and writes a NUL-terminated path on success. On failure,
 * returns zero and leaves outPath as an empty string when space permits.
 */
int findAssetReplacement(const char *relativePath, char *outPath, size_t outPathSize);

#ifdef __cplusplus
}
#endif

#endif /* ASSET_PATH_H */
