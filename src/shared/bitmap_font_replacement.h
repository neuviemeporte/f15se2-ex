/*
 * bitmap_font_replacement.h - Optional editable bitmap-font media.
 */
#ifndef BITMAP_FONT_REPLACEMENT_H
#define BITMAP_FONT_REPLACEMENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BitmapFontReplacement {
    const uint8_t *bitmaps;
    const uint8_t *widths;
} BitmapFontReplacement;

/*
 * Load fonts/font_<id>.bdf, falling back to fonts/font_<id>.png.
 * Replacement data retains the legacy 96-glyph, one-byte-per-row layout.
 */
int bitmapFontReplacementGet(unsigned font_id, unsigned cell_width,
                             unsigned height, const uint8_t *original_widths,
                             BitmapFontReplacement *result);
void bitmapFontReplacementShutdown(void);

#ifdef __cplusplus
}
#endif

#endif
