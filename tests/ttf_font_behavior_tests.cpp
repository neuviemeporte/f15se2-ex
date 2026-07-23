#include "gfx.h"
#include "gfx_impl.h"
#include "headless.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root =
        fs::temp_directory_path() / "f15se2-ex-ttf-font-test";
    const fs::path fontDirectory = root / "fonts";
    const fs::path replacement = fontDirectory / "font_4.ttf";

    fs::remove_all(root);
    fs::create_directories(fontDirectory);
    fs::copy_file(F15_TEST_TTF_PATH, replacement,
                  fs::copy_options::overwrite_existing);
#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", root.string().c_str());
#else
    setenv("F15_REPLACEMENT_ROOT", root.string().c_str(), 1);
#endif

    test_headless_init();
    gfx_videoInit();
    gfx_setMode13();

    const int asciiAdvance = gfx_getGlyphAdvance('n', 4);
    const int cyrillicAdvance = gfx_getGlyphAdvance(0x041f, 4);
    const int lineAdvance =
        gfx_getStringAdvanceUtf8(
            "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82", 4);
    require(asciiAdvance > 0 && cyrillicAdvance == asciiAdvance,
            "TTF replacement supplies compatible Cyrillic and ASCII metrics");
    require(lineAdvance > cyrillicAdvance,
            "TTF replacement measures a complete UTF-8 Cyrillic line");

    int16 params[7] = {};
    params[0] = 0;
    params[2] = 15;
    params[4] = 20;
    params[5] = 20;
    params[6] = 4;
    gfx_drawString(params, "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
    require(params[4] == 20 + lineAdvance,
            "TTF overlay submission uses the same line metric as layout");
    gfx_repaint();
    gfx_invalidateTtfTextOverlayRect(0, 0, 319, 199);
    gfx_clearTtfTextOverlay();
    gfx_videoShutdown();

#if defined(_WIN32)
    _putenv_s("F15_REPLACEMENT_ROOT", "");
#else
    unsetenv("F15_REPLACEMENT_ROOT");
#endif
    fs::remove_all(root);
    std::cout << "ttf_font_behavior_tests passed\n";
    return 0;
}
