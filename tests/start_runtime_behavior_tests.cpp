#include "stalloc.h"
#include "stdata.h"
#include "stinit.h"
#include "stsprit.h"
#include "struct.h"

#include <cstdlib>
#include <iostream>
#include <cstddef>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum StartRuntimeOriginalConstant : int {
    kAllocSize = 0x2200,
    kMenuSpriteHandle = 0x1357,
    kSpritePage = 1,
    kSpriteDstX = 22,
    kSpriteDstY = 33,
    kSpriteSrcX = 44,
    kSpriteSrcY = 55,
    kSpriteWidth = 66,
    kSpriteHeight = 77,
    kSpriteFlags = 0x10,
    kSpriteByte12 = 0x6D,
    kSpriteByte16 = 0x3F,
    kSpriteByte17 = 0x01,
    kExpectedOneCall = 1,
    kExpectedNoCalls = 0,
    kChildExitOk = 0,
    kTestFailureExitCode = 1,
};

int g_allocCalls = 0;
int g_lastAllocSize = 0;
int g_cleanupCalls = 0;
int g_printCalls = 0;
int g_blitCalls = 0;
int g_seedCalls = 0;
int g_clearKeyFlagsCalls = 0;
void *g_allocResult = reinterpret_cast<void *>(0x123456);
struct SpriteParams *g_lastSpriteParams = nullptr;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetRuntimeState() {
    g_allocCalls = 0;
    g_lastAllocSize = 0;
    g_cleanupCalls = 0;
    g_printCalls = 0;
    g_blitCalls = 0;
    g_seedCalls = 0;
    g_clearKeyFlagsCalls = 0;
    g_allocResult = reinterpret_cast<void *>(0x123456);
    g_lastSpriteParams = nullptr;
    menuSprites = kMenuSpriteHandle;
}

} // namespace

void *dos_alloc(const size_t size) {
    ++g_allocCalls;
    g_lastAllocSize = static_cast<int>(size);
    return g_allocResult;
}

void cleanup(void) { ++g_cleanupCalls; }

void dos_printstring(const char *) { ++g_printCalls; }

int FAR CDECL gfx_blitSprite(struct SpriteParams *spritePtr) {
    ++g_blitCalls;
    g_lastSpriteParams = spritePtr;
    return 0;
}

void seedRandom(void) { ++g_seedCalls; }

void FAR CDECL misc_clearKeyFlags(void) { ++g_clearKeyFlagsCalls; }

int main() {
    resetRuntimeState();
    require(allocBuffer(kAllocSize) == g_allocResult &&
                g_allocCalls == kExpectedOneCall &&
                g_lastAllocSize == kAllocSize &&
                g_cleanupCalls == kExpectedNoCalls &&
                g_printCalls == kExpectedNoCalls,
            "allocBuffer forwards the original allocation size and returns the allocated buffer");

    resetRuntimeState();
    g_allocResult = nullptr;
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for allocBuffer failure behavior");
    if (child == 0) {
        allocBuffer(kAllocSize);
        std::exit(kTestFailureExitCode);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for allocBuffer failure child");
    require(WIFEXITED(status) && WEXITSTATUS(status) == kChildExitOk,
            "allocBuffer failure path preserves original cleanup/print/exit behavior");

    resetRuntimeState();
    showSprite(kSpritePage, kSpriteDstX, kSpriteDstY,
               kSpriteSrcX, kSpriteSrcY, kSpriteWidth, kSpriteHeight);
    require(g_blitCalls == kExpectedOneCall && g_lastSpriteParams == &spriteParams,
            "showSprite submits the original global sprite descriptor to the gfx blitter");
    require(spriteParams.bufPtr == kMenuSpriteHandle &&
                spriteParams.srcX == kSpriteSrcX &&
                spriteParams.srcY == kSpriteSrcY &&
                spriteParams.page == kSpritePage &&
                spriteParams.dstX == kSpriteDstX &&
                spriteParams.dstY == kSpriteDstY &&
                spriteParams.width == kSpriteWidth &&
                spriteParams.height == kSpriteHeight &&
                spriteParams.flags == kSpriteFlags,
            "showSprite fills the original sprite descriptor fields");
    require(spriteParams.byte12 == kSpriteByte12 &&
                spriteParams.byte16 == kSpriteByte16 &&
                spriteParams.byte17 == kSpriteByte17,
            "showSprite preserves original sprite descriptor constant bytes");

    // initGraphics no longer sets up gfx pages (that plumbing was deleted with
    // the DOS segment page model); it now just seeds the RNG and clears keys.
    resetRuntimeState();
    initGraphics();
    require(g_seedCalls == kExpectedOneCall && g_clearKeyFlagsCalls == kExpectedOneCall,
            "initGraphics seeds the RNG and clears keyboard flags");

    std::cout << "start_runtime_behavior_tests passed\n";
    return 0;
}
