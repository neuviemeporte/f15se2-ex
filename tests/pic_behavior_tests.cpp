#include "egpic.h"
#include "egtypes.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

typedef struct SDL_IOStream SDL_IOStream;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum PicOriginalConstant : int {
    kOpenModeRead = 0,
    kPicPage = 1,
    kFakeHandle = 0x424200,
    kExpectedOneCall = 1,
    kTestFailureExitCode = 1,
};

int g_openCalls = 0;
int g_showCalls = 0;
int g_closeCalls = 0;
int g_lastOpenMode = -1;
int g_lastShowPage = -1;
const char *g_lastOpenName = nullptr;
SDL_IOStream *g_lastShowHandle = nullptr;
SDL_IOStream *g_lastCloseHandle = nullptr;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

} // namespace

SDL_IOStream *openFileWrapper(const char *filename, int mode) {
    ++g_openCalls;
    g_lastOpenName = filename;
    g_lastOpenMode = mode;
    return reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeHandle));
}

void showPicFile(SDL_IOStream *handle, int pageNum) {
    ++g_showCalls;
    g_lastShowHandle = handle;
    g_lastShowPage = pageNum;
}

void closeFileWrapper(SDL_IOStream *handle) {
    ++g_closeCalls;
    g_lastCloseHandle = handle;
}

int main() {
    openBlitClosePic("cockpit.pic", kPicPage);
    SDL_IOStream *expectedHandle = reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeHandle));
    require(g_openCalls == kExpectedOneCall &&
                std::strcmp(g_lastOpenName, "cockpit.pic") == 0 &&
                g_lastOpenMode == kOpenModeRead,
            "openBlitClosePic opens the original filename in read mode");
    require(g_showCalls == kExpectedOneCall &&
                g_lastShowHandle == expectedHandle &&
                g_lastShowPage == kPicPage,
            "openBlitClosePic passes the opened handle and page to the original PIC blitter");
    require(g_closeCalls == kExpectedOneCall && g_lastCloseHandle == expectedHandle,
            "openBlitClosePic closes the original handle immediately after blitting");

    std::cout << "pic_behavior_tests passed\n";
    return 0;
}
