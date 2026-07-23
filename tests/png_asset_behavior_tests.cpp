#include "shared/png_asset.h"
#include "gfx_impl.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void setReplacementRoot(const std::string &value) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", value.c_str());
#else
    setenv("F15_REPLACEMENT_ROOT", value.c_str(), 1);
#endif
}

} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "f15se2-ex-png-asset-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    SDL_Surface *source = SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_INDEX8);
    SDL_Palette *sourcePalette = SDL_CreatePalette(256);
    require(source && sourcePalette, "indexed PNG fixture allocation");
    SDL_Color colors[2] = {
        {0, 0, 0, 255},
        {255, 0, 0, 255},
    };
    require(SDL_SetPaletteColors(sourcePalette, colors, 0, 2),
            "indexed PNG fixture palette colors");
    require(SDL_SetSurfacePalette(source, sourcePalette),
            "indexed PNG fixture palette attachment");
    uint8 *pixels = (uint8 *)source->pixels;
    pixels[0] = 0;
    pixels[1] = 1;
    pixels[source->pitch] = 1;
    pixels[source->pitch + 1] = 0;
    require(SDL_SavePNG(source, (root / "WALL.PNG").string().c_str()),
            "indexed PNG fixture save");
    SDL_DestroySurface(source);
    SDL_DestroyPalette(sourcePalette);

    setReplacementRoot(root.string());
    SDL_Surface *destination = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_INDEX8);
    require(destination, "indexed destination allocation");
    require(loadReplacementPng("Wall.Pic", destination),
            "PIC name resolves to an indexed PNG replacement");

    const uint8 *row0 = (const uint8 *)destination->pixels;
    const uint8 *row3 = row0 + destination->pitch * 3;
    require(row0[0] == 0 && row0[3] == 1,
            "replacement scales the first indexed row");
    require(row3[0] == 1 && row3[3] == 0,
            "replacement scales the last indexed row");

    SDL_Palette *activePalette = gfx_getPalette();
    require(activePalette && activePalette->colors[1].r == 0
            && activePalette->colors[1].g == 0
            && activePalette->colors[1].b == 170,
            "indexed PNG preserves the caller-selected game palette");

    SDL_Surface *truecolor = SDL_CreateSurface(1, 1, SDL_PIXELFORMAT_RGBA32);
    require(truecolor, "true-color PNG fixture allocation");
    uint8 *truecolorPixel = (uint8 *)truecolor->pixels;
    truecolorPixel[0] = 255;
    truecolorPixel[1] = 0;
    truecolorPixel[2] = 0;
    truecolorPixel[3] = 255;
    require(SDL_SavePNG(truecolor, (root / "TRUE.PNG").string().c_str()),
            "true-color PNG fixture save");
    SDL_DestroySurface(truecolor);
    int expectedRedIndex = 0;
    unsigned int bestRedDistance = ~0U;
    for (int i = 0; i < activePalette->ncolors; ++i) {
        const int dr = 255 - activePalette->colors[i].r;
        const int dg = activePalette->colors[i].g;
        const int db = activePalette->colors[i].b;
        const unsigned int distance =
            (unsigned int)(dr * dr + dg * dg + db * db);
        if (distance < bestRedDistance) {
            bestRedDistance = distance;
            expectedRedIndex = i;
        }
    }
    require(loadReplacementPng("True.Pic", destination),
            "true-color PNG replacement converts to the active game palette");
    require(((const uint8 *)destination->pixels)[0] == expectedRedIndex,
            "true-color conversion chooses the nearest palette index");

    SDL_Surface *wrongDestination =
        SDL_CreateSurface(2, 2, SDL_PIXELFORMAT_RGBA32);
    require(wrongDestination, "non-indexed destination allocation");
    require(!loadReplacementPng("True.Pic", wrongDestination),
            "legacy compatibility path rejects a non-indexed destination");
    SDL_DestroySurface(wrongDestination);

    require(!loadReplacementPng("Missing.Pic", destination),
            "missing replacement leaves legacy fallback available");

    SDL_DestroySurface(destination);
    std::filesystem::remove_all(root);
    std::cout << "png_asset_behavior_tests passed\n";
    return 0;
}
