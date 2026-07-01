#include "stalloc.h"
#include "stdata.h"
#include "stinit.h"
#include "stsprit.h"
#include "struct.h"

#include <cstdlib>
#include <iostream>
#include <cstddef>
#include "posix_test_compat.h"

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
    kInitPage0 = 0,
    kInitPage1 = 1,
    kInitPage0Segment = 0x2100,
    kInitPage1Segment = 0x2200,
    kExpectedOneCall = 1,
    kExpectedTwoCalls = 2,
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
int g_setPageCalls = 0;
int g_allocPageCalls = 0;
int g_storeBufPtrCalls = 0;
int g_clearKeyFlagsCalls = 0;
void *g_allocResult = reinterpret_cast<void *>(0x123456);
struct SpriteParams *g_lastSpriteParams = nullptr;
int g_allocPages[2] = {};
int g_lastSetPage = -1;
int g_lastStoreSeg = 0;
int g_lastStorePage = -1;

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
    g_setPageCalls = 0;
    g_allocPageCalls = 0;
    g_storeBufPtrCalls = 0;
    g_clearKeyFlagsCalls = 0;
    g_allocResult = reinterpret_cast<void *>(0x123456);
    g_lastSpriteParams = nullptr;
    g_allocPages[0] = -1;
    g_allocPages[1] = -1;
    g_lastSetPage = -1;
    g_lastStoreSeg = 0;
    g_lastStorePage = -1;
    menuSprites = kMenuSpriteHandle;
}

} // namespace

void *dos_alloc(const size_t size) {
    ++g_allocCalls;
    g_lastAllocSize = static_cast<int>(size);
    return g_allocResult;
}

void cleanup(void) {
    ++g_cleanupCalls;
}

void dos_printstring(const char *) {
    ++g_printCalls;
}

int FAR CDECL gfx_blitSprite(struct SpriteParams *spritePtr) {
    ++g_blitCalls;
    g_lastSpriteParams = spritePtr;
    return 0;
}

void seedRandom(void) {
    ++g_seedCalls;
}

void FAR CDECL gfx_setPageN(uint16 pageNum) {
    ++g_setPageCalls;
    g_lastSetPage = pageNum;
}

int FAR CDECL gfx_allocPage(int pageNum) {
    require(g_allocPageCalls < kExpectedTwoCalls,
            "initGraphics should allocate exactly the original two graphics pages");
    g_allocPages[g_allocPageCalls++] = pageNum;
    return pageNum == kInitPage0 ? kInitPage0Segment : kInitPage1Segment;
}

void FAR CDECL gfx_storeBufPtr(uint16 seg, int pageIdx) {
    ++g_storeBufPtrCalls;
    g_lastStoreSeg = seg;
    g_lastStorePage = pageIdx;
}

void misc_clearKeyFlags(void) {
    ++g_clearKeyFlagsCalls;
}

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

    resetRuntimeState();
    initGraphics();
    require(g_seedCalls == kExpectedOneCall,
            "initGraphics preserves the original startup RNG seed call");
    require(g_setPageCalls == kExpectedOneCall && g_lastSetPage == kInitPage0,
            "initGraphics selects original graphics page 0 before allocation");
    require(g_allocPageCalls == kExpectedTwoCalls &&
                g_allocPages[0] == kInitPage0 &&
                g_allocPages[1] == kInitPage1,
            "initGraphics allocates the original graphics pages 0 and 1");
    require(g_storeBufPtrCalls == kExpectedOneCall &&
                g_lastStoreSeg == kInitPage1Segment &&
                g_lastStorePage == kInitPage1,
            "initGraphics stores the original page-1 buffer pointer");
    require(g_clearKeyFlagsCalls == kExpectedOneCall,
            "initGraphics clears keyboard flags after graphics setup");

    std::cout << "start_runtime_behavior_tests passed\n";
    return 0;
}
