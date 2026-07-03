#include "egdata.h"
#include "inttype.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

// loadColorPalette lives in eg3dview.c; the surrounding render orchestration
// (render3DView) moved to the r3d backend seam and is no longer isolable here.
extern void loadColorPalette(int idx);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
enum ViewOriginalConstant : int {
    kPaletteRecordBytes = 16,     // one 16-entry colour-remap record
    kGuardSentinel = 0xAB,        // canary just past the copied record
    kTestFailureExitCode = 1,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

} // namespace

int main() {
    // loadColorPalette copies exactly one 16-byte record from g_colorPalettes,
    // selected by idx*16, into the front of colorLut. Assert the record, the
    // idx*16 stride (distinct records select distinct windows), and that it
    // stops at 16 bytes (guard byte untouched).
    colorLut[kPaletteRecordBytes] = kGuardSentinel;
    loadColorPalette(2);
    require(std::memcmp(colorLut, g_colorPalettes + 2 * kPaletteRecordBytes,
                        kPaletteRecordBytes) == 0,
            "loadColorPalette copies the idx*16 palette record into colorLut");
    require(colorLut[kPaletteRecordBytes] == kGuardSentinel,
            "loadColorPalette copies exactly one 16-byte record without overrun");

    loadColorPalette(0);
    require(std::memcmp(colorLut, g_colorPalettes, kPaletteRecordBytes) == 0,
            "loadColorPalette reselects record 0 via the idx*16 stride");
    require(std::memcmp(g_colorPalettes, g_colorPalettes + 2 * kPaletteRecordBytes,
                        kPaletteRecordBytes) != 0,
            "distinct palette indices map to distinct 16-byte records");

    std::cout << "view_behavior_tests passed\n";
    return 0;
}
