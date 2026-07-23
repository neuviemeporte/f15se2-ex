#include "gfx_impl.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

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

void setReplacementRoot(const std::filesystem::path *root) {
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", root ? root->string().c_str() : "");
#else
    if (root) setenv("F15_REPLACEMENT_ROOT", root->string().c_str(), 1);
    else unsetenv("F15_REPLACEMENT_ROOT");
#endif
}

} // namespace

int main() {
    const std::filesystem::path convertedRoot = F15_CONVERTED_ASSETS;
    const int fontIds[] = {0, 1, 3, 4, 5};

    setReplacementRoot(&convertedRoot);
    for (int fontId : fontIds) {
        uint8 legacyBitmap[96 * 32] = {};
        uint8 replacementBitmap[96 * 32] = {};
        uint8 legacyWidths[96] = {};
        uint8 replacementWidths[96] = {};
        int legacyHeight = 0;
        int legacyMaxWidth = 0;
        int replacementHeight = 0;
        int replacementMaxWidth = 0;

        require(gfx_testCopyBuiltinFont(
                    (uint16)fontId, legacyBitmap, sizeof(legacyBitmap),
                    legacyWidths, sizeof(legacyWidths),
                    &legacyHeight, &legacyMaxWidth),
                "cannot read built-in font_" + std::to_string(fontId));
        require(gfx_testCopyEffectiveFont(
                    (uint16)fontId, replacementBitmap,
                    sizeof(replacementBitmap), replacementWidths,
                    sizeof(replacementWidths), &replacementHeight,
                    &replacementMaxWidth),
                "cannot load replacement font_" + std::to_string(fontId));
        require(replacementHeight == legacyHeight &&
                    replacementMaxWidth == legacyMaxWidth,
                "font metrics differ for font_" + std::to_string(fontId));
        require(std::memcmp(
                    replacementWidths, legacyWidths, sizeof(legacyWidths)) == 0,
                "glyph widths differ for font_" + std::to_string(fontId));
        require(std::memcmp(replacementBitmap, legacyBitmap,
                            (size_t)legacyHeight * 96u) == 0,
                "glyph rows differ for font_" + std::to_string(fontId));
    }

    setReplacementRoot(nullptr);
    std::cout << "bitmap_font_full_validation_tests compared "
              << sizeof(fontIds) / sizeof(fontIds[0]) << " font slots\n";
    return 0;
}
