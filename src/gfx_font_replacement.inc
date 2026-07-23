#ifdef F15_HAVE_FREETYPE
static FT_Library g_freetypeLibrary = NULL;
static FT_Face g_fontReplacementTtfFaces[8] = {NULL};
static char *g_fontReplacementTtfPaths[8] = {NULL};
typedef struct TtfTextOverlayRecord {
    char *text;
    int x, y, color;
    uint16 fontIdx;
    int clipL, clipR, clipT, clipB;
} TtfTextOverlayRecord;
static TtfTextOverlayRecord g_ttfTextOverlayRecords[512];
static int g_ttfTextOverlayCount;
static void clearTtfTextOverlayRecords(void);
#endif


/* Release one FreeType face and clear its replacement-font cache entry. */
static void freeReplacementFont(uint16 fontIdx) {
    if (fontIdx >= 8) return;
#ifdef F15_HAVE_FREETYPE
    if (g_fontReplacementTtfFaces[fontIdx]) {
        FT_Done_Face(g_fontReplacementTtfFaces[fontIdx]);
        g_fontReplacementTtfFaces[fontIdx] = NULL;
    }
    SDL_free(g_fontReplacementTtfPaths[fontIdx]);
    g_fontReplacementTtfPaths[fontIdx] = NULL;
#endif
}

/* Release all vector-font faces and the shared FreeType library. */
static void cleanupReplacementFonts(void) {
#ifdef F15_HAVE_FREETYPE
    int i;
    clearTtfTextOverlayRecords();
    for (i = 0; i < 8; i++) freeReplacementFont((uint16)i);
    if (g_freetypeLibrary) {
        FT_Done_FreeType(g_freetypeLibrary);
        g_freetypeLibrary = NULL;
    }
#endif
}
/* Decode one UTF-8 codepoint and advance the caller cursor safely. */
static int decodeUtf8Codepoint(const char *text, uint32 *codepointOut, int *byteCountOut) {
    uint32_t codepoint;
    size_t byteCount;
    if (!utf8DecodeCodepoint(text, &codepoint, &byteCount)) return 0;
    *codepointOut = (uint32)codepoint;
    *byteCountOut = (int)byteCount;
    return 1;
}

#ifdef F15_HAVE_FREETYPE
/* Discard every retained vector-text overlay record for a new screen. */
static void clearTtfTextOverlayRecords(void) {
    int i;
    for (i = 0; i < g_ttfTextOverlayCount; i++) {
        SDL_free(g_ttfTextOverlayRecords[i].text);
        g_ttfTextOverlayRecords[i].text = NULL;
    }
    g_ttfTextOverlayCount = 0;
}

/* Mark all retained vector-text overlays dirty after underlying pixels change. */
static void invalidateTtfTextOverlayRecords(void) {
    if (g_ttfTextOverlayCount > 0) clearTtfTextOverlayRecords();
}

/* Detect broad framebuffer changes that invalidate the complete retained overlay. */
static int ttfOverlayRectIsScreenChange(int x1, int y1, int x2, int y2) {
    int w, h;
    if (x2 < x1) {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y2 < y1) {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    w = x2 - x1 + 1;
    h = y2 - y1 + 1;
    return w > 0 && h > 0 && w * h >= 12000;
}

/* Return the logical width occupied by one retained vector-text record. */
static int ttfTextOverlayRecordWidth(const TtfTextOverlayRecord *record) {
    if (!record || !record->text) return 0;
    return replacementTtfStringAdvance(record->fontIdx, record->text);
}

/* Return whether two retained text records cover overlapping logical rectangles. */
static int ttfTextOverlayRecordsOverlap(const TtfTextOverlayRecord *a,
                                        int x, int y, int width, int height,
                                        int margin) {
    int ax1, ay1, ax2, ay2;
    int bx1, by1, bx2, by2;
    if (!a || width <= 0 || height <= 0) return 0;
    ax1 = a->x - margin;
    ay1 = a->y - margin;
    ax2 = a->x + ttfTextOverlayRecordWidth(a) + margin - 1;
    ay2 = a->y + g_fontHeightsArr[a->fontIdx] + margin - 1;
    bx1 = x - margin;
    by1 = y - margin;
    bx2 = x + width + margin - 1;
    by2 = y + height + margin - 1;
    return ax2 >= bx1 && ax1 <= bx2 && ay2 >= by1 && ay1 <= by2;
}

/* Recognize changing numeric HUD text that should replace an older row record. */
static int ttfTextOverlaySameDynamicRow(const TtfTextOverlayRecord *a,
                                        int x, int y, int width, int height) {
    int dy;
    if (!a) return 0;
    dy = a->y - y;
    if (dy < 0) dy = -dy;
    /* Only treat one-pixel HUD jitter as a replacement. Menu rows are retained
     * text and can share wide clip windows; deleting by broad overlap there can
     * remove neighboring labels such as the TODAY'S MISSION header. */
    return dy <= 1 && ttfTextOverlayRecordsOverlap(a, x, y, width, height, 1);
}

/* Remove one retained overlay record while keeping the array densely packed. */
static void removeTtfTextOverlayRecord(int index) {
    if (index < 0 || index >= g_ttfTextOverlayCount) return;
    SDL_free(g_ttfTextOverlayRecords[index].text);
    if (index + 1 < g_ttfTextOverlayCount) {
        memmove(
            &g_ttfTextOverlayRecords[index],
            &g_ttfTextOverlayRecords[index + 1],
            (size_t)(g_ttfTextOverlayCount - index - 1) * sizeof(g_ttfTextOverlayRecords[0])
        );
    }
    g_ttfTextOverlayCount--;
    memset(&g_ttfTextOverlayRecords[g_ttfTextOverlayCount], 0, sizeof(g_ttfTextOverlayRecords[0]));
}

/* Invalidate retained text touched by a legacy framebuffer update. */
static void invalidateTtfTextOverlayRect(int x1, int y1, int x2, int y2) {
    int i;
    if (x2 < x1) {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y2 < y1) {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    for (i = g_ttfTextOverlayCount - 1; i >= 0; i--) {
        TtfTextOverlayRecord *record = &g_ttfTextOverlayRecords[i];
        int rx1 = record->x;
        int ry1 = record->y;
        int rx2 = record->x + ttfTextOverlayRecordWidth(record) - 1;
        int ry2 = record->y + g_fontHeightsArr[record->fontIdx] - 1;
        if (rx2 >= x1 && rx1 <= x2 && ry2 >= y1 && ry1 <= y2) {
            removeTtfTextOverlayRecord(i);
        }
    }
}

/* Recolor retained text records affected by a legacy palette animation. */
static void switchTtfTextOverlayColorRect(int x1, int y1, int x2, int y2, int oldColor, int newColor) {
    int i;
    if (x2 < x1) {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (y2 < y1) {
        int tmp = y1;
        y1 = y2;
        y2 = tmp;
    }
    for (i = 0; i < g_ttfTextOverlayCount; i++) {
        TtfTextOverlayRecord *record = &g_ttfTextOverlayRecords[i];
        int rx1 = record->x;
        int ry1 = record->y;
        int rx2 = record->x + ttfTextOverlayRecordWidth(record) - 1;
        int ry2 = record->y + g_fontHeightsArr[record->fontIdx] - 1;
        if ((record->color & 0xff) == (oldColor & 0xff) &&
            rx2 >= x1 && rx1 <= x2 && ry2 >= y1 && ry1 <= y2) {
            record->color = newColor;
        }
    }
}
#endif

#ifdef F15_HAVE_FREETYPE
/* Initialize the shared FreeType library exactly once. */
static int ensureFreetypeLibrary(void) {
    if (g_freetypeLibrary) return 1;
    if (FT_Init_FreeType(&g_freetypeLibrary) != 0) {
        g_freetypeLibrary = NULL;
        LogWarn(("asset replacement: FreeType initialization failed; TTF/OTF fonts disabled"));
        return 0;
    }
    return 1;
}

/* Sample either monochrome or antialiased FreeType bitmap coverage. */
static uint8 freetypeBitmapPixel(const FT_Bitmap *bitmap, int x, int y) {
    const uint8 *row;
    int pitch;
    if (!bitmap || !bitmap->buffer || x < 0 || y < 0 || x >= (int)bitmap->width || y >= (int)bitmap->rows) return 0;
    pitch = bitmap->pitch < 0 ? -bitmap->pitch : bitmap->pitch;
    row = bitmap->buffer + (size_t)y * (size_t)pitch;
    if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
        return (row[x >> 3] & (0x80u >> (x & 7))) ? 255 : 0;
    }
    if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
        return row[x];
    }
    return 0;
}

/* Choose a vector-font pixel size that matches the legacy font slot metrics. */
static int replacementTtfPixelSize(uint16 fontIdx) {
    int h;
    if (fontIdx >= 8) return 8;
    h = g_fontHeightsArr[fontIdx];
    /* Replacement TTF text must occupy the same screen footprint as the original
     * bitmap fonts. FreeType still supplies shape/coverage, but layout remains
     * compatible with the legacy menu coordinates and clipping windows. */
    return h;
}

/* Return the FreeType advance for one Unicode codepoint at the active size. */
static int replacementTtfUnicodeAdvance(uint16 fontIdx) {
    const uint8 *widths;
    int idx;
    if (fontIdx >= 8) return 8;
    widths = g_fontWidthTables[fontIdx];
    if (widths) {
        idx = 'n' - 0x20;
        if (idx >= 0 && idx < 96 && widths[idx] > 0) return widths[idx];
    }
    return g_fontMaxWidths[fontIdx] > 0 ? g_fontMaxWidths[fontIdx] : 8;
}

/* Return the logical advance for one legacy byte through its UTF-8-compatible mapping. */
static int replacementTtfAdvance(uint16 fontIdx, uint32 codepoint) {
    FT_Face face;
    int advance;
    if (fontIdx >= 8 || !g_fontReplacementTtfFaces[fontIdx]) return 8;
    if (codepoint >= 0x20 && codepoint < 0x80 && g_fontWidthTables[fontIdx]) {
        return g_fontWidthTables[fontIdx][codepoint - 0x20];
    }
    if (codepoint >= 0x80) return replacementTtfUnicodeAdvance(fontIdx);
    face = g_fontReplacementTtfFaces[fontIdx];
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)replacementTtfPixelSize(fontIdx)) != 0) return 8;
    if (FT_Load_Char(face, (FT_ULong)codepoint, FT_LOAD_DEFAULT) != 0) return 8;
    advance = (int)((face->glyph->advance.x + 32) >> 6);
    return advance > 0 ? advance : 1;
}

/* Choose one consistent line scale that fits the original game text rectangle. */
static void replacementTtfLayoutScale(uint16 fontIdx, int pixelSize,
                                      float targetScaleX, float targetScaleY,
                                      float *layoutScaleX, float *layoutScaleY) {
    FT_Face face;
    const uint8 *legacyWidths;
    int ch;
    int legacyAdvance = 0;
    int ftAdvance = 0;
    float targetHeight;
    float ftHeight;

    *layoutScaleX = 1.0f;
    *layoutScaleY = 1.0f;
    if (fontIdx >= 8 || !g_fontReplacementTtfFaces[fontIdx]) return;
    face = g_fontReplacementTtfFaces[fontIdx];
    legacyWidths = g_fontWidthTables[fontIdx];
    if (!legacyWidths) return;
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)SDL_max(1, pixelSize)) != 0) return;

    /* Calibrate the scalable face once per rendered font size against the whole
     * printable ASCII width table. This keeps all TTF/OTF lines for a font at a
     * single consistent scale while still matching the old menu footprint. */
    for (ch = 0x20; ch < 0x80; ch++) {
        legacyAdvance += legacyWidths[ch - 0x20];
        if (FT_Load_Glyph(face, FT_Get_Char_Index(face, (FT_ULong)ch), FT_LOAD_DEFAULT) == 0) {
            ftAdvance += (int)(face->glyph->advance.x >> 6);
        }
    }
    if (ftAdvance > 0) {
        *layoutScaleX = ((float)legacyAdvance * targetScaleX) / (float)ftAdvance;
    }

    targetHeight = (float)g_fontHeightsArr[fontIdx] * targetScaleY;
    ftHeight = face->size ? (float)(face->size->metrics.height >> 6) : (float)pixelSize;
    if (ftHeight > 0.0f) {
        *layoutScaleY = targetHeight / ftHeight;
    }
}

/* Measure a complete UTF-8 string using the same layout rules as rendering. */
static int replacementTtfStringAdvance(uint16 fontIdx, const char *text) {
    FT_Face face;
    FT_UInt previousGlyphIndex = 0;
    float layoutScaleX;
    float layoutScaleY;
    float x = 0.0f;
    int charIdx;
    int drawnChars;

    if (!text || fontIdx >= 8 || !g_fontReplacementTtfFaces[fontIdx]) return 0;
    if (fontIdx == 0) {
        int width = 0;
        for (charIdx = 0, drawnChars = 0; text[charIdx] != 0 && drawnChars < 256;) {
            uint32 codepoint;
            uint8 ch = (uint8)text[charIdx];
            int byteCount = 1;
            if (!decodeUtf8Codepoint(&text[charIdx], &codepoint, &byteCount)) {
                codepoint = ch;
                byteCount = 1;
            }
            if (byteCount == 1 && (ch & 0x80)) {
                charIdx++;
                continue;
            }
            width += replacementTtfAdvance(fontIdx, codepoint);
            charIdx += byteCount;
            drawnChars++;
        }
        return width;
    }
    face = g_fontReplacementTtfFaces[fontIdx];
    replacementTtfLayoutScale(fontIdx, replacementTtfPixelSize(fontIdx), 1.0f, 1.0f,
                              &layoutScaleX, &layoutScaleY);
    (void)layoutScaleY;
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)replacementTtfPixelSize(fontIdx)) != 0) return 0;

    for (charIdx = 0, drawnChars = 0; text[charIdx] != 0 && drawnChars < 256;) {
        uint32 codepoint;
        uint8 ch = (uint8)text[charIdx];
        int byteCount = 1;
        FT_UInt glyphIndex;
        FT_Vector kerning;

        if (!decodeUtf8Codepoint(&text[charIdx], &codepoint, &byteCount)) {
            codepoint = ch;
            byteCount = 1;
        }
        if (byteCount == 1 && (ch & 0x80)) {
            charIdx++;
            continue;
        }
        glyphIndex = FT_Get_Char_Index(face, (FT_ULong)codepoint);
        if (previousGlyphIndex && glyphIndex && FT_HAS_KERNING(face)) {
            if (FT_Get_Kerning(face, previousGlyphIndex, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0) {
                x += (float)(kerning.x >> 6) * layoutScaleX;
            }
        }
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) == 0) {
            x += (float)(face->glyph->advance.x >> 6) * layoutScaleX;
        }
        previousGlyphIndex = glyphIndex;
        charIdx += byteCount;
        drawnChars++;
    }
    return (int)(x + 0.5f);
}

/* Load and cache an optional TTF or OTF face for one legacy font slot. */
static int loadReplacementFontTtf(uint16 fontIdx, const char *replacementPath) {
    FT_Face face = NULL;
    int fontWidth;
    int fontHeight;

    if (fontIdx >= 8 || !replacementPath) return 0;
    fontWidth = g_fontMaxWidths[fontIdx];
    fontHeight = g_fontHeightsArr[fontIdx];
    if (fontWidth <= 0 || fontWidth > 8 || fontHeight <= 0 || fontHeight > 32) return 0;
    if (!ensureFreetypeLibrary()) return 0;
    if (FT_New_Face(g_freetypeLibrary, replacementPath, 0, &face) != 0 || !face) {
        LogWarn(("asset replacement: failed to load TTF/OTF font %u at %s", (unsigned)fontIdx, replacementPath));
        return 0;
    }
    /* Runtime TTF/OTF replacement is the Unicode source of truth. Some faces do
     * not expose their Unicode cmap as the default charmap, which makes Cyrillic
     * and other non-ASCII input resolve to glyph 0 and render as blank cells. */
    (void)FT_Select_Charmap(face, FT_ENCODING_UNICODE);

    freeReplacementFont(fontIdx);
    g_fontReplacementTtfFaces[fontIdx] = face;
    g_fontReplacementTtfPaths[fontIdx] = SDL_strdup(replacementPath);

    LogInfo(("asset replacement: loaded font %u from runtime TTF/OTF %s", (unsigned)fontIdx, replacementPath));
    return 1;
}

/* Retain one UTF-8 line for vector rendering and return its logical advance. */
static int recordTtfTextOverlayAndAdvance(int16 *params, const char *string,
                                          int clipL, int clipR, int clipT, int clipB) {
    TtfTextOverlayRecord *record;
    int charIdx;
    int drawnChars;
    int color;
    uint16 fontIdx;
    int i;
    int newX;
    int newY;
    int newWidth;
    int newHeight;
    int recordIndex;

    if (!params || !string) return 0;
    fontIdx = (uint16)params[6] & 7;
    if (fontIdx >= 8 || !g_fontReplacementTtfFaces[fontIdx]) return 0;
    newX = (int)params[4];
    newY = (int)params[5];
    newWidth = replacementTtfStringAdvance(fontIdx, string);
    newHeight = g_fontHeightsArr[fontIdx];

    recordIndex = -1;
    for (i = g_ttfTextOverlayCount - 1; i >= 0; i--) {
        TtfTextOverlayRecord *candidate = &g_ttfTextOverlayRecords[i];
        if (candidate->fontIdx == fontIdx &&
            candidate->clipL == clipL && candidate->clipR == clipR &&
            candidate->clipT == clipT && candidate->clipB == clipB &&
            candidate->x == newX && candidate->y == newY) {
            recordIndex = i;
            continue;
        }
        /* Dynamic HUD values are often cleared/redrawn with a one-pixel anchor
         * shift as their width changes. Exact-position keys leave the old TTF
         * overlay on screen, because it was never baked into the page being
         * cleared. Treat near/overlapping same-font text in the same clip window
         * as a replacement candidate. */
        if (candidate->fontIdx == fontIdx &&
            candidate->clipL == clipL && candidate->clipR == clipR &&
            candidate->clipT == clipT && candidate->clipB == clipB &&
/* Recognize changing numeric HUD text that should replace an older row record. */
            ttfTextOverlaySameDynamicRow(candidate, newX, newY, newWidth, newHeight)) {
            removeTtfTextOverlayRecord(i);
            if (recordIndex > i) recordIndex--;
        }
    }

    if (string[0] == '\0') {
        if (recordIndex >= 0) {
            removeTtfTextOverlayRecord(recordIndex);
        }
        return 1;
    }

    if (recordIndex < 0) {
        if (g_ttfTextOverlayCount >= (int)(sizeof(g_ttfTextOverlayRecords) / sizeof(g_ttfTextOverlayRecords[0]))) return 0;
        record = &g_ttfTextOverlayRecords[g_ttfTextOverlayCount++];
        memset(record, 0, sizeof(*record));
    } else {
        record = &g_ttfTextOverlayRecords[recordIndex];
        SDL_free(record->text);
        record->text = NULL;
    }

    record->text = SDL_strdup(string);
    if (!record->text) {
        if (recordIndex < 0) {
            --g_ttfTextOverlayCount;
            memset(record, 0, sizeof(*record));
        } else {
            removeTtfTextOverlayRecord(recordIndex);
        }
        return 0;
    }
    record->x = newX;
    record->y = newY;
    record->color = (int)params[2];
    record->fontIdx = fontIdx;
    record->clipL = clipL;
    record->clipR = clipR;
    record->clipT = clipT;
    record->clipB = clipB;

    color = record->color;
    for (charIdx = 0, drawnChars = 0; string[charIdx] != 0 && drawnChars < 256;) {
        uint32 codepoint;
        uint8 ch = (uint8)string[charIdx];
        int byteCount = 1;
        if (!decodeUtf8Codepoint(&string[charIdx], &codepoint, &byteCount)) {
            codepoint = ch;
            byteCount = 1;
        }
        if (byteCount == 1 && (ch & 0x80)) {
            color = ch & 0x7F;
            charIdx++;
            continue;
        }
        charIdx += byteCount;
        drawnChars++;
    }
    params[4] = (int16)(record->x + newWidth);
    params[2] = (int16)color;
    return 1;
}

/* Render one FreeType glyph through the SDL framebuffer backend. */
static void renderTtfTextOverlayGlyphSDL(SDL_Renderer *renderer, FT_Bitmap *bitmap, float x, float y,
                                         float pixelScaleX, float pixelScaleY, uint8 r, uint8 g, uint8 b, SDL_FRect clip) {
    int row, col;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (row = 0; row < (int)bitmap->rows; row++) {
        for (col = 0; col < (int)bitmap->width; col++) {
            uint8 coverage = freetypeBitmapPixel(bitmap, col, row);
            SDL_FRect px;
            if (!coverage) continue;
            px.x = x + (float)col * pixelScaleX;
            px.y = y + (float)row * pixelScaleY;
            px.w = pixelScaleX;
            px.h = pixelScaleY;
            if (px.x < clip.x || px.y < clip.y || px.x >= clip.x + clip.w || px.y >= clip.y + clip.h) continue;
            SDL_SetRenderDrawColor(renderer, r, g, b, coverage);
            SDL_RenderFillRect(renderer, &px);
        }
    }
}

/* Render one FreeType glyph through the OpenGL overlay backend. */
static void renderTtfTextOverlayGlyphGL(FT_Bitmap *bitmap, float x, float y, float pixelScaleX, float pixelScaleY,
                                        uint8 r, uint8 g, uint8 b, SDL_FRect clip) {
    int row, col;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    for (row = 0; row < (int)bitmap->rows; row++) {
        for (col = 0; col < (int)bitmap->width; col++) {
            uint8 coverage = freetypeBitmapPixel(bitmap, col, row);
            float x0, y0, x1, y1;
            if (!coverage) continue;
            x0 = x + (float)col * pixelScaleX;
            y0 = y + (float)row * pixelScaleY;
            if (x0 < clip.x || y0 < clip.y || x0 >= clip.x + clip.w || y0 >= clip.y + clip.h) continue;
            x1 = x0 + pixelScaleX;
            y1 = y0 + pixelScaleY;
            glColor4ub(r, g, b, coverage);
            glVertex2f(x0, y0);
            glVertex2f(x1, y0);
            glVertex2f(x1, y1);
            glVertex2f(x0, y1);
        }
    }
    glEnd();
    glDisable(GL_BLEND);
}

/* Lay out and render one retained UTF-8 text record as a coherent line. */
static void renderTtfTextOverlayRecord(const TtfTextOverlayRecord *record, R2DMapping *m,
                                       SDL_Renderer *renderer, int useGL) {
    SDL_Color c;
    SDL_Palette *pal;
    FT_Face face;
    FT_UInt previousGlyphIndex = 0;
    float x;
    int charIdx;
    int drawnChars;
    int pixelSize;
    float cellY;
    float cellHeight;
    float baseline;
    float ascender;
    float layoutScaleX;
    float layoutScaleY;
    SDL_FRect clip;

    if (!record || !record->text || record->fontIdx >= 8 || !g_fontReplacementTtfFaces[record->fontIdx]) return;
    face = g_fontReplacementTtfFaces[record->fontIdx];
    pixelSize = SDL_max(1, (int)((float)g_fontHeightsArr[record->fontIdx] * m->scaleY + 0.5f));
    if (FT_Set_Pixel_Sizes(face, 0, (FT_UInt)pixelSize) != 0) return;
    replacementTtfLayoutScale(record->fontIdx, pixelSize, m->scaleX, m->scaleY,
                              &layoutScaleX, &layoutScaleY);
    pal = gfx_getPalette();
    if (!pal) return;
    c = pal->colors[record->color & 0xff];
    x = (float)m->offX + (float)record->x * m->scaleX;
    cellY = (float)m->offY + (float)record->y * m->scaleY;
    cellHeight = (float)g_fontHeightsArr[record->fontIdx] * m->scaleY;
    ascender = face->size ? (float)(face->size->metrics.ascender >> 6) * layoutScaleY : cellHeight;
    baseline = cellY + ascender;
    clip.x = (float)m->offX + (float)record->clipL * m->scaleX;
    clip.y = (float)m->offY + (float)record->clipT * m->scaleY;
    clip.w = (float)(record->clipR - record->clipL + 1) * m->scaleX;
    clip.h = (float)(record->clipB - record->clipT + 1) * m->scaleY;
    if (record->fontIdx == 0 && record->y >= HUD_BOTTOM_TTF_CLIP_Y) {
        float maxBottom = (float)m->offY + (float)VGA_PAGE_HEIGHT * m->scaleY;
        float wantedBottom =
            ((float)record->y + (float)g_fontHeightsArr[record->fontIdx] +
             HUD_BOTTOM_TTF_EXTRA_LINES) *
                m->scaleY +
            (float)m->offY;
        if (wantedBottom > clip.y + clip.h) clip.h = wantedBottom - clip.y;
        if (clip.y + clip.h > maxBottom) clip.h = maxBottom - clip.y;
    }

    for (charIdx = 0, drawnChars = 0; record->text[charIdx] != 0 && drawnChars < 256;) {
        uint32 codepoint;
        uint8 ch = (uint8)record->text[charIdx];
        int byteCount = 1;
        FT_UInt glyphIndex;
        FT_Vector kerning;
        FT_GlyphSlot glyph;
        FT_Bitmap *bitmap;
        float gx;
        float gy;
        float glyphScaleX;
        float glyphScaleY;

        if (!decodeUtf8Codepoint(&record->text[charIdx], &codepoint, &byteCount)) {
            codepoint = ch;
            byteCount = 1;
        }
        if (byteCount == 1 && (ch & 0x80)) {
            c = pal->colors[ch & 0x7f];
            charIdx++;
            continue;
        }
        glyphIndex = FT_Get_Char_Index(face, (FT_ULong)codepoint);
        if (previousGlyphIndex && glyphIndex && FT_HAS_KERNING(face)) {
            if (FT_Get_Kerning(face, previousGlyphIndex, glyphIndex, FT_KERNING_DEFAULT, &kerning) == 0) {
                x += (float)(kerning.x >> 6) * layoutScaleX;
            }
        }
        if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_RENDER) == 0) {
            glyph = face->glyph;
            bitmap = &glyph->bitmap;
            glyphScaleX = layoutScaleX;
            glyphScaleY = layoutScaleY;
            if (record->fontIdx == 0) {
                float targetAdvance = (float)replacementTtfAdvance(record->fontIdx, codepoint) * m->scaleX;
                /* HUD font_0 is position-sensitive: original glyph cells are
                 * anchored at the exact (x,y) page coordinate. FreeType natural
                 * bearings make speed/altitude labels drift by pixels compared
                 * with the bitmap HUD. Keep high-resolution rasterization, but fit
                 * each glyph into the old cell width so digits/letters do not
                 * collide or overflow the tiny legacy clear rectangles. */
                if (bitmap->width > 0 && targetAdvance > 1.0f) {
                    float fitScale = (targetAdvance * 0.82f) / (float)bitmap->width;
                    if (fitScale > 0.0f && fitScale < glyphScaleX) glyphScaleX = fitScale;
                }
                gx = x;
                if (glyph->bitmap_top > 0 &&
                    (float)bitmap->rows * glyphScaleY < cellHeight * 0.8f) {
                    gy = baseline - (float)glyph->bitmap_top * glyphScaleY;
                } else {
                    gy = cellY;
                }
            } else {
                gx = x + (float)glyph->bitmap_left * layoutScaleX;
                gy = baseline - (float)glyph->bitmap_top * layoutScaleY;
            }
            if (gy < cellY) gy = cellY;
            if (useGL) renderTtfTextOverlayGlyphGL(bitmap, gx, gy, glyphScaleX, glyphScaleY, c.r, c.g, c.b, clip);
            else renderTtfTextOverlayGlyphSDL(renderer, bitmap, gx, gy, glyphScaleX, glyphScaleY, c.r, c.g, c.b, clip);
        }
        if (record->fontIdx == 0) {
            x += (float)replacementTtfAdvance(record->fontIdx, codepoint) * m->scaleX;
        } else if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) == 0) {
            x += (float)(face->glyph->advance.x >> 6) * layoutScaleX;
        }
        previousGlyphIndex = glyphIndex;
        charIdx += byteCount;
        drawnChars++;
    }
}

/* Render all retained vector-text records for the current frame. */
static void renderTtfTextOverlay(R2DMapping *m, SDL_Renderer *renderer, int useGL) {
    int i;
    if (!m || g_ttfTextOverlayCount <= 0) return;
    for (i = 0; i < g_ttfTextOverlayCount; i++) {
        renderTtfTextOverlayRecord(&g_ttfTextOverlayRecords[i], m, renderer, useGL);
    }
}

/* Render retained vector text into the active OpenGL presentation mapping. */
void gfx_renderTtfTextOverlayOpenGL(int virtW, int virtH, int winW, int winH) {
    R2DMapping m;
    r2d_computeMapping(virtW, virtH, winW, winH, 0, &m);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, winW, winH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    renderTtfTextOverlay(&m, NULL, 1);
}

/* Clear retained vector text when the legacy screen contents are replaced. */
void gfx_clearTtfTextOverlay(void) {
    clearTtfTextOverlayRecords();
}

/* Invalidate retained vector text intersecting a changed logical rectangle. */
void gfx_invalidateTtfTextOverlayRect(int x1, int y1, int x2, int y2) {
    invalidateTtfTextOverlayRect(x1, y1, x2, y2);
}
#else
/* Render retained vector text into the active OpenGL presentation mapping. */
void gfx_renderTtfTextOverlayOpenGL(int virtW, int virtH, int winW, int winH) {
    (void)virtW;
    (void)virtH;
    (void)winW;
    (void)winH;
}
/* Clear retained vector text when the legacy screen contents are replaced. */
void gfx_clearTtfTextOverlay(void) {}
/* Invalidate retained vector text intersecting a changed logical rectangle. */
void gfx_invalidateTtfTextOverlayRect(int x1, int y1, int x2, int y2) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
}
#endif


/* Find a TTF or OTF replacement using stable per-slot filenames. */
static int findReplacementVectorFont(uint16 fontIdx, const char *extension,
                                     char *path, size_t pathSize) {
    char relative[64];
    snprintf(relative, sizeof(relative), "fonts/font_%u.%s",
             (unsigned)fontIdx, extension);
    return findAssetReplacement(relative, path, pathSize);
}

/* Activate a vector-font replacement for one legacy font slot when available. */
static void tryLoadReplacementFont(uint16 fontIdx) {
    char path[512];
    int vectorFontPresent = 0;

    if (fontIdx >= 8 || g_fontReplacementTried[fontIdx]) return;
    g_fontReplacementTried[fontIdx] = 1;
#ifdef F15_HAVE_FREETYPE
    if (findReplacementVectorFont(fontIdx, "ttf", path, sizeof(path))) {
        vectorFontPresent = 1;
        if (loadReplacementFontTtf(fontIdx, path)) return;
    }
    if (findReplacementVectorFont(fontIdx, "otf", path, sizeof(path))) {
        vectorFontPresent = 1;
        if (loadReplacementFontTtf(fontIdx, path)) return;
    }
#else
    vectorFontPresent =
        findReplacementVectorFont(fontIdx, "ttf", path, sizeof(path)) ||
        findReplacementVectorFont(fontIdx, "otf", path, sizeof(path));
#endif
    if (vectorFontPresent) {
        LogWarn(("asset replacement: rejected vector font %u; using bitmap fallback",
                 (unsigned)fontIdx));
    }
    tryLoadBitmapFontReplacement(fontIdx);
}
