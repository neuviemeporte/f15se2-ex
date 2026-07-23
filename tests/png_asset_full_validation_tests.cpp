#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"
#include "shared/common.h"
#include "shared/png_asset.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

extern bool setGamePath(const char *path);
extern void decodePic(SDL_IOStream *handle, int segment);
extern void picBlit(SDL_IOStream *handle, int pageIndex);

#if !defined(F15_ORIGINAL_ASSETS)
#define F15_ORIGINAL_ASSETS ""
#endif

#if !defined(F15_CONVERTED_ASSETS)
#define F15_CONVERTED_ASSETS ""
#endif

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return (char)std::toupper(c); });
    return value;
}

void setReplacementRoot(const std::filesystem::path *root) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", root ? root->string().c_str() : "");
#else
    if (root) setenv("F15_REPLACEMENT_ROOT", root->string().c_str(), 1);
    else unsetenv("F15_REPLACEMENT_ROOT");
#endif
}

bool isRuntimeImage(const std::filesystem::path &path) {
    const std::string extension = upper(path.extension().string());
    const std::string filename = upper(path.filename().string());
    /*
     * These four files belong to DEMO.EXE's VGGA path. Calling the regular
     * game PIC decoder for them would compare against the wrong old loader.
     */
    if (filename == "1.PIC" || filename == "2.PIC" ||
        filename == "3.PIC" || filename == "4.PIC") {
        return false;
    }
    return extension == ".PIC" || extension == ".SPR";
}

std::vector<unsigned char> surfacePixels(SDL_Surface *surface) {
    require(surface != nullptr, "missing indexed destination surface");
    std::vector<unsigned char> pixels(
        (size_t)surface->w * (size_t)surface->h);
    if (SDL_MUSTLOCK(surface)) require(SDL_LockSurface(surface), "surface lock");
    for (int y = 0; y < surface->h; ++y) {
        const unsigned char *row =
            (const unsigned char *)surface->pixels + (size_t)y * surface->pitch;
        std::memcpy(pixels.data() + (size_t)y * surface->w,
                    row, (size_t)surface->w);
    }
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
    return pixels;
}

std::vector<SDL_Color> paletteColors() {
    SDL_Palette *palette = gfx_getPalette();
    require(palette && palette->ncolors > 0, "missing active game palette");
    return std::vector<SDL_Color>(
        palette->colors, palette->colors + palette->ncolors);
}

void restorePalette(const std::vector<SDL_Color> &colors) {
    SDL_Palette *palette = gfx_getPalette();
    require(palette && palette->ncolors == (int)colors.size(),
            "active palette size changed");
    require(SDL_SetPaletteColors(palette, colors.data(), 0, palette->ncolors),
            "restore active palette");
}

bool samePalette(const std::vector<SDL_Color> &left,
                 const std::vector<SDL_Color> &right) {
    if (left.size() != right.size()) return false;
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i].r != right[i].r || left[i].g != right[i].g ||
            left[i].b != right[i].b) {
            return false;
        }
    }
    return true;
}

void clearSurface(SDL_Surface *surface) {
    require(surface != nullptr, "missing surface to clear");
    if (SDL_MUSTLOCK(surface)) require(SDL_LockSurface(surface), "surface lock");
    std::memset(surface->pixels, 0, (size_t)surface->pitch * surface->h);
    if (SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
}

void compareImage(const std::filesystem::path &originalRoot,
                  const std::filesystem::path &convertedRoot,
                  const std::filesystem::path &assetPath) {
    const std::string logicalName =
        std::filesystem::relative(assetPath, originalRoot).generic_string();
    const bool sprite = upper(assetPath.extension().string()) == ".SPR";
    const bool title640 = upper(assetPath.filename().string()) == "TITLE640.PIC";
    SDL_Surface *surface;
    int spriteHandle = 0;

    if (title640) {
        surface = gfx_getHiResSurface();
    } else if (sprite) {
        spriteHandle = gfx_allocSpriteBuf();
        require(spriteHandle != 0,
                "could not allocate sprite buffer for " + logicalName);
        surface = gfx_getSpriteSurface(spriteHandle);
    } else {
        surface = gfx_getCurPageSurface();
    }

    const std::vector<SDL_Color> initialPalette = paletteColors();
    clearSurface(surface);
    setReplacementRoot(nullptr);
    SDL_IOStream *legacy = openFile(logicalName.c_str(), 0);
    require(legacy != nullptr, "cannot open legacy image " + logicalName);
    if (title640) picBlit(legacy, 0);
    else if (sprite) decodePic(legacy, spriteHandle);
    else showPicFile(legacy, 0);
    fileClose(legacy);
    const std::vector<unsigned char> legacyPixels = surfacePixels(surface);
    const std::vector<SDL_Color> legacyPalette = paletteColors();

    restorePalette(initialPalette);
    clearSurface(surface);
    setReplacementRoot(&convertedRoot);
    int loaded;
    if (title640) {
        loaded = loadReplacementPngToHiResTitle(logicalName.c_str());
    } else if (sprite) {
        loaded = loadReplacementPngToSprite(logicalName.c_str(), spriteHandle);
    } else {
        loaded = loadReplacementPngToPage(logicalName.c_str(), 0);
    }
    setReplacementRoot(nullptr);
    require(loaded != 0, "missing PNG replacement for " + logicalName);

    require(surfacePixels(surface) == legacyPixels,
            "runtime pixel mismatch for " + logicalName);
    require(samePalette(paletteColors(), legacyPalette),
            "runtime palette mismatch for " + logicalName);

    restorePalette(initialPalette);
    if (spriteHandle) gfx_freeSpriteBuf(spriteHandle);
}

} // namespace

int main() {
    const std::filesystem::path originalRoot = F15_ORIGINAL_ASSETS;
    const std::filesystem::path convertedRoot = F15_CONVERTED_ASSETS;
    require(std::filesystem::is_directory(originalRoot),
            "F15SE2_ORIGINAL_ASSETS must name an original asset directory");
    require(std::filesystem::is_directory(convertedRoot),
            "F15SE2_CONVERTED_ASSETS must name a converted asset directory");

    test_headless_init();
    gfx_videoInit();
    require(setGamePath(originalRoot.string().c_str()), "setGamePath failed");

    int compared = 0;
    for (const auto &entry :
         std::filesystem::recursive_directory_iterator(originalRoot)) {
        if (!entry.is_regular_file() || !isRuntimeImage(entry.path())) continue;
        compareImage(originalRoot, convertedRoot, entry.path());
        ++compared;
    }

    setReplacementRoot(nullptr);
    gfx_videoShutdown();
    require(compared > 0, "no runtime PIC/SPR assets were compared");
    std::cout << "png_asset_full_validation_tests compared "
              << compared << " runtime images\n";
    return 0;
}
