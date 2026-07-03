// Golden-image support for render behaviour tests.
//
// A render test draws into an 8-bit page/sprite surface and calls golden_check()
// to compare it, pixel-index for pixel-index, against a committed reference
// image under tests/goldens/. The reference is a BMP (SDL's built-in
// SDL_SaveBMP/SDL_LoadBMP, no extra dependency) so anyone can open it in an
// image viewer to *see* what a test covers without reading the assertion code.
//
// Workflow:
//   * Missing golden  -> it is created from the current render and the test
//     passes (bootstrap). Commit the new file under tests/goldens/.
//   * Env F15_UPDATE_GOLDENS set -> every golden is rewritten and tests pass.
//     Use this to intentionally re-bless output after a deliberate render
//     change, then eyeball the git diff of the BMPs.
//   * Otherwise -> the render is compared to the golden; on any size/pixel
//     mismatch the test writes "<name>.actual.bmp" into the build dir for
//     inspection and returns false (the caller fails the test).
//
// golden_loadSurface() does the inverse for *input*: it loads a committed BMP
// as an initial state to inject into a page before drawing.
#ifndef F15_TEST_GOLDEN_H
#define F15_TEST_GOLDEN_H

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

// tests/goldens absolute path, injected by the CMake test helper. Falls back to
// a relative path so the header is still usable in an ad-hoc build.
#ifndef F15_GOLDEN_DIR
#define F15_GOLDEN_DIR "goldens"
#endif

static inline std::string golden_path(const char *name) {
    return std::string(F15_GOLDEN_DIR) + "/" + name + ".bmp";
}

// Load a committed golden BMP as an 8-bit indexed surface (initial-state
// injection). Returns NULL if the file is absent; caller owns the surface.
static inline SDL_Surface *golden_loadSurface(const char *name) {
    return SDL_LoadBMP(golden_path(name).c_str());
}

// Compare `surf` to tests/goldens/<name>.bmp by palette index. See the file
// header for the bootstrap / update / compare behaviour. Prints a diagnostic to
// stderr on mismatch and writes <name>.actual.bmp to the cwd (build dir).
static inline bool golden_check(SDL_Surface *surf, const char *name) {
    if (!surf) {
        std::fprintf(stderr, "golden_check(%s): null surface\n", name);
        return false;
    }
    const std::string path = golden_path(name);
    const bool forceUpdate = SDL_getenv("F15_UPDATE_GOLDENS") != nullptr;

    SDL_Surface *golden = forceUpdate ? nullptr : SDL_LoadBMP(path.c_str());
    if (!golden) {
        // Missing (or forced): (re)write the golden and pass. SDL_SaveBMP keeps
        // the 8-bit indices verbatim, which is what we compare on.
        if (!SDL_SaveBMP(surf, path.c_str())) {
            std::fprintf(stderr, "golden_check(%s): could not write golden %s: %s\n",
                         name, path.c_str(), SDL_GetError());
            return false;
        }
        std::fprintf(stderr, "golden_check(%s): %s golden %s\n", name,
                     forceUpdate ? "updated" : "created", path.c_str());
        return true;
    }

    bool ok = (golden->w == surf->w && golden->h == surf->h);
    if (ok) {
        const Uint8 *gp = (const Uint8 *)golden->pixels;
        const Uint8 *sp = (const Uint8 *)surf->pixels;
        for (int y = 0; y < surf->h && ok; y++) {
            const Uint8 *grow = gp + (size_t)y * golden->pitch;
            const Uint8 *srow = sp + (size_t)y * surf->pitch;
            if (std::memcmp(grow, srow, (size_t)surf->w) != 0) {
                for (int x = 0; x < surf->w; x++) {
                    if (grow[x] != srow[x]) {
                        std::fprintf(stderr,
                                     "golden_check(%s): first mismatch at (%d,%d): golden %u actual %u\n",
                                     name, x, y, grow[x], srow[x]);
                        break;
                    }
                }
                ok = false;
            }
        }
    } else {
        std::fprintf(stderr, "golden_check(%s): size mismatch golden %dx%d vs actual %dx%d\n",
                     name, golden->w, golden->h, surf->w, surf->h);
    }
    SDL_DestroySurface(golden);

    if (!ok) {
        const std::string actual = std::string(name) + ".actual.bmp";
        SDL_SaveBMP(surf, actual.c_str());
        std::fprintf(stderr, "golden_check(%s): wrote %s for inspection\n", name, actual.c_str());
    }
    return ok;
}

#endif // F15_TEST_GOLDEN_H
