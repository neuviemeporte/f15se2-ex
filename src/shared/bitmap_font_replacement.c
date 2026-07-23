/*
 * bitmap_font_replacement.c - BDF/PNG font replacement loader.
 */

#include "bitmap_font_replacement.h"

#include "asset_path.h"
#include "../log.h"

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FONT_SLOT_COUNT 8
#define FONT_FIRST_CODEPOINT 0x20
#define FONT_GLYPH_COUNT 96

typedef struct CachedFont {
    uint8_t *bitmaps;
    uint8_t *widths;
    int tried;
} CachedFont;

static CachedFont g_fonts[FONT_SLOT_COUNT];

static int replacementPath(unsigned font_id, const char *extension,
                           char *path, size_t path_size) {
    char relative[64];
    snprintf(relative, sizeof(relative), "fonts/font_%u.%s",
             font_id, extension);
    return findAssetReplacement(relative, path, path_size);
}

static void discardFont(CachedFont *font) {
    SDL_free(font->bitmaps);
    SDL_free(font->widths);
    font->bitmaps = NULL;
    font->widths = NULL;
}

static int parseBdf(const char *path, unsigned height,
                    uint8_t **bitmaps_out, uint8_t **widths_out) {
    FILE *file = fopen(path, "rb");
    uint8_t *bitmaps = NULL;
    uint8_t *widths = NULL;
    unsigned char complete[FONT_GLYPH_COUNT] = {0};
    int encoding = -1;
    int advance = -1;
    unsigned row = 0;
    int in_bitmap = 0;
    char line[512];

    if (!file) return 0;
    bitmaps = (uint8_t *)SDL_calloc(FONT_GLYPH_COUNT, height);
    widths = (uint8_t *)SDL_calloc(FONT_GLYPH_COUNT, 1);
    if (!bitmaps || !widths) goto fail;

    while (fgets(line, sizeof(line), file)) {
        char *text = line;
        while (isspace((unsigned char)*text)) ++text;
        if (strncmp(text, "STARTCHAR ", 10) == 0) {
            encoding = -1;
            advance = -1;
            row = 0;
            in_bitmap = 0;
        } else if (sscanf(text, "ENCODING %d", &encoding) == 1) {
            continue;
        } else if (sscanf(text, "DWIDTH %d", &advance) == 1) {
            continue;
        } else if (strncmp(text, "BITMAP", 6) == 0) {
            in_bitmap = 1;
            row = 0;
        } else if (strncmp(text, "ENDCHAR", 7) == 0) {
            if (encoding >= FONT_FIRST_CODEPOINT
                && encoding < FONT_FIRST_CODEPOINT + FONT_GLYPH_COUNT
                && advance > 0 && advance <= 255 && row == height) {
                const unsigned index =
                    (unsigned)(encoding - FONT_FIRST_CODEPOINT);
                widths[index] = (uint8_t)advance;
                complete[index] = 1;
            }
            in_bitmap = 0;
        } else if (in_bitmap) {
            char *end = NULL;
            if (row >= height) goto fail;
            const unsigned long value = strtoul(text, &end, 16);
            if (end == text || value > 255) goto fail;
            while (isspace((unsigned char)*end)) ++end;
            if (*end != '\0') goto fail;
            if (encoding >= FONT_FIRST_CODEPOINT
                && encoding < FONT_FIRST_CODEPOINT + FONT_GLYPH_COUNT) {
                const unsigned index =
                    (unsigned)(encoding - FONT_FIRST_CODEPOINT);
                bitmaps[index * height + row] = (uint8_t)value;
            }
            ++row;
        }
    }
    fclose(file);

    for (unsigned index = 0; index < FONT_GLYPH_COUNT; ++index) {
        if (!complete[index]) goto invalid;
    }
    *bitmaps_out = bitmaps;
    *widths_out = widths;
    return 1;

invalid:
    SDL_free(bitmaps);
    SDL_free(widths);
    return 0;
fail:
    fclose(file);
    SDL_free(bitmaps);
    SDL_free(widths);
    return 0;
}

static int pixelIsLit(SDL_Surface *surface, int x, int y) {
    Uint8 r, g, b, a;
    if (!SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a)) return 0;
    return a != 0 && (r != 0 || g != 0 || b != 0);
}

static int parsePng(const char *path, unsigned cell_width, unsigned height,
                    const uint8_t *original_widths,
                    uint8_t **bitmaps_out, uint8_t **widths_out) {
    SDL_Surface *surface = SDL_LoadPNG(path);
    uint8_t *bitmaps = NULL;
    uint8_t *widths = NULL;
    const unsigned expected_width = 16 * (cell_width + 1) - 1;
    const unsigned expected_height = 6 * (height + 1) - 1;

    if (!surface) return 0;
    if (cell_width == 0 || cell_width > 8 || height == 0
        || surface->w < (int)expected_width
        || surface->h < (int)expected_height) {
        SDL_DestroySurface(surface);
        return 0;
    }

    bitmaps = (uint8_t *)SDL_calloc(FONT_GLYPH_COUNT, height);
    widths = (uint8_t *)SDL_malloc(FONT_GLYPH_COUNT);
    if (!bitmaps || !widths) goto fail;
    memcpy(widths, original_widths, FONT_GLYPH_COUNT);

    for (unsigned index = 0; index < FONT_GLYPH_COUNT; ++index) {
        const unsigned x0 = (index % 16) * (cell_width + 1);
        const unsigned y0 = (index / 16) * (height + 1);
        for (unsigned y = 0; y < height; ++y) {
            uint8_t bits = 0;
            for (unsigned x = 0; x < cell_width; ++x) {
                if (pixelIsLit(surface, (int)(x0 + x), (int)(y0 + y))) {
                    bits |= (uint8_t)(0x80u >> x);
                }
            }
            bitmaps[index * height + y] = bits;
        }
    }
    SDL_DestroySurface(surface);
    *bitmaps_out = bitmaps;
    *widths_out = widths;
    return 1;

fail:
    SDL_DestroySurface(surface);
    SDL_free(bitmaps);
    SDL_free(widths);
    return 0;
}

int bitmapFontReplacementGet(unsigned font_id, unsigned cell_width,
                             unsigned height, const uint8_t *original_widths,
                             BitmapFontReplacement *result) {
    CachedFont *font;
    char path[1024];

    if (!result || !original_widths || font_id >= FONT_SLOT_COUNT
        || cell_width == 0 || cell_width > 8 || height == 0) {
        return 0;
    }
    result->bitmaps = NULL;
    result->widths = NULL;
    font = &g_fonts[font_id];
    if (!font->tried) {
        font->tried = 1;
        if (replacementPath(font_id, "bdf", path, sizeof(path))) {
            if (parseBdf(path, height, &font->bitmaps, &font->widths)) {
                LogInfo(("asset replacement: loaded bitmap font %u from %s",
                         font_id, path));
            } else {
                LogWarn(("asset replacement: rejected BDF font %u at %s",
                         font_id, path));
            }
        }
        if (!font->bitmaps
            && replacementPath(font_id, "png", path, sizeof(path))) {
            if (parsePng(path, cell_width, height, original_widths,
                         &font->bitmaps, &font->widths)) {
                LogInfo(("asset replacement: loaded bitmap font %u from %s",
                         font_id, path));
            } else {
                LogWarn(("asset replacement: rejected PNG font %u at %s",
                         font_id, path));
            }
        }
    }
    if (!font->bitmaps) return 0;
    result->bitmaps = font->bitmaps;
    result->widths = font->widths;
    return 1;
}

void bitmapFontReplacementShutdown(void) {
    for (unsigned index = 0; index < FONT_SLOT_COUNT; ++index) {
        discardFont(&g_fonts[index]);
        g_fonts[index].tried = 0;
    }
}
