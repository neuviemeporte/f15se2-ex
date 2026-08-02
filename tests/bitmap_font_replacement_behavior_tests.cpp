#include "shared/bitmap_font_replacement.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
    if (value.empty()) unsetenv("F15_REPLACEMENT_ROOT");
    else setenv("F15_REPLACEMENT_ROOT", value.c_str(), 1);
#endif
}

void writeBdf(const std::filesystem::path &path, bool complete) {
    std::ofstream out(path);
    out << "STARTFONT 2.1\n";
    const int glyph_count = complete ? 96 : 95;
    for (int index = 0; index < glyph_count; ++index) {
        out << "STARTCHAR glyph\nENCODING " << 32 + index
            << "\nDWIDTH " << 1 + index % 8
            << " 0\nBBX 8 2 0 0\nBITMAP\n"
            << (index == 0 ? "80\n40\n" : "00\n00\n")
            << "ENDCHAR\n";
    }
    out << "ENDFONT\n";
}

void writePng(const std::filesystem::path &path) {
    SDL_Surface *surface =
        SDL_CreateSurface(16 * 9 - 1, 6 * 3 - 1, SDL_PIXELFORMAT_RGBA32);
    require(surface != nullptr, "font PNG test surface is created");
    SDL_FillSurfaceRect(surface, nullptr, 0);
    require(SDL_WriteSurfacePixel(surface, 0, 0, 255, 255, 255, 255),
            "font PNG test pixel is written");
    require(SDL_SavePNG(surface, path.string().c_str()),
            "font PNG test atlas is saved");
    SDL_DestroySurface(surface);
}

} // namespace

int main() {
    const auto root =
        std::filesystem::temp_directory_path() / "f15se2-ex-font-tests";
    const auto fonts = root / "fonts";
    const auto bdf = fonts / "font_1.bdf";
    const auto png = fonts / "font_1.png";
    const uint8_t original_widths[96] = {4};
    BitmapFontReplacement replacement = {};

    std::filesystem::remove_all(root);
    std::filesystem::create_directories(fonts);
    setReplacementRoot(root.string());
    writeBdf(bdf, true);

    require(bitmapFontReplacementGet(1, 8, 2, original_widths, &replacement),
            "complete BDF loads");
    require(replacement.bitmaps[0] == 0x80
            && replacement.bitmaps[1] == 0x40,
            "BDF bitmap rows retain their legacy byte layout");
    require(replacement.widths[0] == 1 && replacement.widths[7] == 8,
            "BDF advance widths replace the legacy table");

    bitmapFontReplacementShutdown();
    writeBdf(bdf, false);
    writePng(png);
    replacement = {};
    require(bitmapFontReplacementGet(1, 8, 2, original_widths, &replacement),
            "valid PNG is used when BDF is incomplete");
    require(replacement.bitmaps[0] == 0x80,
            "PNG atlas is converted to the legacy glyph-row layout");
    require(replacement.widths[0] == original_widths[0],
            "PNG atlas preserves original advance widths");

    bitmapFontReplacementShutdown();
    setReplacementRoot("");
    std::filesystem::remove_all(root);
    std::cout << "bitmap_font_replacement_behavior_tests passed\n";
    return 0;
}
