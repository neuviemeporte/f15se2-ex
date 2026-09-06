#include "blackbox_gl.h"

#include "blackbox.h"
#include "r2d.h"

#include <SDL3/SDL_opengl.h>
#include <stdio.h>

static const unsigned char kDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};

static void drawCell(float x, float y, float w, float h) {
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
}

static void drawDigit(float x, float y, float w, float h, int digit) {
    int row, col;
    for (row = 0; row < 5; row++) {
        unsigned char bits = kDigits[digit][row];
        for (col = 0; col < 3; col++) {
            if (bits & (1u << (2 - col)))
                drawCell(x + (float)col * w, y + (float)row * h, w, h);
        }
    }
}

void blackbox_drawGlOverlay(SDL_Surface *page, int shakeOffset, SDL_Window *window,
                            SDL_Palette *palette,
                            BlackboxGlSetupFn setupOverlayState) {
    char text[16];
    int winW, winH, i;
    float x, y, cellW, cellH, bgX, bgY, bgW, bgH;
    R2DMapping mapping;
    SDL_Color foreground;
    uint32 tick;
    uint64 displayedTime;

    if (!blackbox_enabled() || !page || !window || !palette ||
        !setupOverlayState) return;
    SDL_GetWindowSizeInPixels(window, &winW, &winH);
    r2d_computeMapping(page->w, page->h, winW, winH, 0, &mapping);

    cellW = mapping.scaleX;
    cellH = mapping.scaleY;
    bgX = (float)mapping.offX - (float)shakeOffset * mapping.scaleX;
    bgY = (float)mapping.offY;
    bgW = 52.0f * mapping.scaleX;
    bgH = 7.0f * mapping.scaleY;
    x = bgX + mapping.scaleX;
    y = bgY + mapping.scaleY;

    setupOverlayState();
    glDisable(GL_BLEND);
    glColor3ub(0, 0, 0);
    glBegin(GL_QUADS);
    drawCell(bgX, bgY, bgW, bgH);
    glEnd();

    tick = blackbox_tick();
    displayedTime = (uint64)(tick / 60u) * 100u + (uint64)(tick % 60u);
    snprintf(text, sizeof(text), "%llu", (unsigned long long)displayedTime);
    foreground = palette->colors[15];
    glColor3ub(foreground.r, foreground.g, foreground.b);
    glBegin(GL_QUADS);
    for (i = 0; text[i]; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            drawDigit(x, y, cellW, cellH, text[i] - '0');
            x += 4.0f * cellW;
        }
    }
    glEnd();
}
