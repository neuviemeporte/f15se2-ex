#include "inttype.h"
#include "pointers.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

typedef struct SDL_IOStream SDL_IOStream;

void showPicFile(SDL_IOStream *handle, int page);
void decodePic(SDL_IOStream *handle, int segment);
void decodePicRaw(SDL_IOStream *handle, int segment);
void picBlit(SDL_IOStream *handle, int pageIndex);

uint8 picDecodedRowBuf[320] = {};

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum SharedPicImplOriginalConstant : int {
    kPicReadBlockSize = 512,
    kPicHeaderMaxWidthNibbleMode = 0x09,
    kPicHeaderMaxWidthByteMode = 0xF7,
    kShowPage = 2,
    kSpriteSegment = 0x2468,
    kRawSegmentIgnored = 0x1357,
    kSurfaceWidth = 320,
    kTinySurfaceWidth = 4,
    kOneDecodedRow = 1,
    kZeroRows = 0,
    kSurfacePitch = 320,
    kHiResWidth = 640,
    kHiResPitch = 640,
    kHiResProgressRows = 8,
    kRleEscapeByte = 0x90,
    kRleLiteralZeroCount = 0,
    kRleNoOutputCount = 1,
    kRleSafetyBreakPairs = 50001, // decodeRow breaks after more than 50000 no-output iterations.
    kRleRepeatCount = 3,
    kRleRepeatOutputPixels = 3,
    kLiteralBeforeRle = 0x34,
    kLzwLiteralA = 0x41,
    kFirstDictionaryCode = 256,
    kFirstKwKwKCode = 257,
    kExpectedNoCalls = 0,
    kExpectedOneCall = 1,
    kTestFailureExitCode = 1,
};

int g_setPageCalls = 0;
int g_fileReadCalls = 0;
int g_spriteSurfaceCalls = 0;
int g_hiResSurfaceCalls = 0;
int g_presentHiResCalls = 0;
bool g_repeatPicStreamReads = false;
bool g_sequentialPicStreamReads = false;
int g_lastPage = -1;
int g_lastReadCount = 0;
size_t g_picStreamOffset = 0;
int g_lastSpriteSegment = 0;
SDL_IOStream *g_lastReadHandle = nullptr;
SDL_Surface g_pageSurface = {};
SDL_Surface g_spriteSurface = {};
SDL_Surface g_hiResSurface = {};
uint8 g_pagePixels[kSurfacePitch] = {};
uint8 g_spritePixels[kSurfacePitch] = {};
uint8 g_hiResPixels[kHiResPitch * kHiResProgressRows] = {};
std::vector<uint8> g_picStream;

SDL_IOStream *fakeHandle(int value) {
    return reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(value));
}

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void setupZeroHeightSurface(SDL_Surface *surface, uint8 *pixels) {
    std::memset(surface, 0, sizeof(*surface));
    surface->format = SDL_PIXELFORMAT_INDEX8;
    surface->w = kSurfaceWidth;
    surface->h = kZeroRows;
    surface->pitch = kSurfacePitch;
    surface->pixels = pixels;
}

void setupOneRowSurface(SDL_Surface *surface, uint8 *pixels) {
    std::memset(surface, 0, sizeof(*surface));
    surface->format = SDL_PIXELFORMAT_INDEX8;
    surface->w = kTinySurfaceWidth;
    surface->h = kOneDecodedRow;
    surface->pitch = kSurfacePitch;
    surface->pixels = pixels;
}

void appendLzwCodeBits(std::vector<uint8> &bytes, unsigned &bitPos, uint16 code) {
    for (int bit = 0; bit < 9; ++bit) {
        if ((code >> bit) & 1U) {
            bytes[bitPos / 8] = static_cast<uint8>(bytes[bitPos / 8] | (1U << (bitPos % 8)));
        }
        ++bitPos;
        if (bitPos / 8 >= bytes.size()) bytes.push_back(0);
    }
}

std::vector<uint8> makePicStream(uint8 headerLowByte, const std::vector<uint16> &codes) {
    std::vector<uint8> bytes(kPicReadBlockSize, 0);
    unsigned bitPos = 8; // Header high byte is the first original LZW data byte.
    bytes[0] = headerLowByte;
    for (uint16 code : codes) appendLzwCodeBits(bytes, bitPos, code);
    return bytes;
}

std::vector<uint8> makeRepeatedNoOutputPicStream(int pairs) {
    std::vector<uint16> codes;
    codes.reserve(static_cast<size_t>(pairs) * 2U);
    for (int pair = 0; pair < pairs; ++pair) {
        codes.push_back(kRleEscapeByte);
        codes.push_back(kRleNoOutputCount);
    }
    return makePicStream(kPicHeaderMaxWidthByteMode, codes);
}

void resetPicImplState() {
    g_setPageCalls = 0;
    g_fileReadCalls = 0;
    g_spriteSurfaceCalls = 0;
    g_hiResSurfaceCalls = 0;
    g_presentHiResCalls = 0;
    g_repeatPicStreamReads = false;
    g_sequentialPicStreamReads = false;
    g_lastPage = -1;
    g_lastReadCount = 0;
    g_picStreamOffset = 0;
    g_lastSpriteSegment = 0;
    g_lastReadHandle = nullptr;
    setupZeroHeightSurface(&g_pageSurface, g_pagePixels);
    setupZeroHeightSurface(&g_spriteSurface, g_spritePixels);
    setupZeroHeightSurface(&g_hiResSurface, g_hiResPixels);
    g_hiResSurface.w = kHiResWidth;
    g_hiResSurface.pitch = kHiResPitch;
    std::memset(picDecodedRowBuf, 0, sizeof(picDecodedRowBuf));
    std::memset(g_pagePixels, 0xEE, sizeof(g_pagePixels));
    std::memset(g_spritePixels, 0xEE, sizeof(g_spritePixels));
    std::memset(g_hiResPixels, 0xEE, sizeof(g_hiResPixels));
    g_picStream.clear();
}

} // namespace

void FAR CDECL gfx_setPageN(uint16 pageNum) {
    ++g_setPageCalls;
    g_lastPage = pageNum;
}

SDL_Surface *gfx_getCurPageSurface(void) {
    return &g_pageSurface;
}

SDL_Surface *gfx_getPageSurface(int) {
    return nullptr;
}

SDL_Surface *gfx_getSpriteSurface(int handle) {
    ++g_spriteSurfaceCalls;
    g_lastSpriteSegment = handle;
    return &g_spriteSurface;
}

int fileReadRaw(SDL_IOStream *handle, void *dst, int count) {
    ++g_fileReadCalls;
    g_lastReadHandle = handle;
    g_lastReadCount = count;
    if (!g_picStream.empty() && g_sequentialPicStreamReads) {
        std::memset(dst, 0, static_cast<size_t>(count));
        const size_t available = g_picStream.size() - std::min(g_picStreamOffset, g_picStream.size());
        const size_t copied = std::min(available, static_cast<size_t>(count));
        if (copied != 0) {
            std::memcpy(dst, g_picStream.data() + g_picStreamOffset, copied);
            g_picStreamOffset += copied;
        }
    } else if (!g_picStream.empty() && (g_fileReadCalls == kExpectedOneCall || g_repeatPicStreamReads)) {
        std::memset(dst, 0, static_cast<size_t>(count));
        std::memcpy(dst, g_picStream.data(),
                    std::min(g_picStream.size(), static_cast<size_t>(count)));
    } else {
        std::memset(dst, 0, static_cast<size_t>(count));
    }
    if (g_picStream.empty() && g_fileReadCalls == kExpectedOneCall) {
        static_cast<uint8 *>(dst)[0] = kPicHeaderMaxWidthByteMode;
    }
    return count;
}

SDL_Surface *gfx_getHiResSurface(void) {
    ++g_hiResSurfaceCalls;
    return &g_hiResSurface;
}

void gfx_presentHiRes(void) {
    ++g_presentHiResCalls;
}

int main() {
    resetPicImplState();
    showPicFile(nullptr, kShowPage);
    require(g_setPageCalls == kExpectedNoCalls &&
                g_fileReadCalls == kExpectedNoCalls,
            "showPicFile preserves the original no-op behavior for an invalid handle");

    resetPicImplState();
    SDL_IOStream *showHandle = fakeHandle(1);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    showPicFile(showHandle, kShowPage);
    require(g_setPageCalls == kExpectedNoCalls,
            "showPicFile decodes into the current back-buffer page without reselecting (page arg collapsed to a single buffer)");
    require(g_fileReadCalls == kExpectedOneCall &&
                g_lastReadHandle == showHandle &&
                g_lastReadCount == kPicReadBlockSize,
            "showPicFile initializes the original 512-byte PIC input buffer");
    require(g_pagePixels[0] == 0 && g_pagePixels[kTinySurfaceWidth - 1] == 0 &&
                g_pagePixels[kTinySurfaceWidth] == 0xEE,
            "showPicFile copies only the visible row width from the original decoded PIC row");

    resetPicImplState();
    showHandle = fakeHandle(5);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    g_picStream = makePicStream(kPicHeaderMaxWidthNibbleMode, {0xAB});
    showPicFile(showHandle, kShowPage);
    require(g_pagePixels[0] == 0x0B &&
                g_pagePixels[1] == 0x0A &&
                g_pagePixels[kTinySurfaceWidth] == 0xEE,
            "showPicFile preserves the original nibble-mode low-nibble/high-nibble pixel expansion");

    resetPicImplState();
    showHandle = fakeHandle(6);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    g_picStream = makePicStream(kPicHeaderMaxWidthByteMode,
                                {kLiteralBeforeRle, kRleEscapeByte, kRleRepeatCount});
    showPicFile(showHandle, kShowPage);
    require(g_pagePixels[0] == kLiteralBeforeRle &&
                g_pagePixels[1] == kLiteralBeforeRle &&
                g_pagePixels[2] == kLiteralBeforeRle &&
                g_pagePixels[kRleRepeatOutputPixels] == 0,
            "showPicFile preserves original PIC RLE repeat escape behavior");

    resetPicImplState();
    showHandle = fakeHandle(7);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    g_picStream = makePicStream(kPicHeaderMaxWidthByteMode,
                                {kRleEscapeByte, kRleLiteralZeroCount});
    showPicFile(showHandle, kShowPage);
    require(g_pagePixels[0] == kRleEscapeByte,
            "showPicFile preserves original PIC literal RLE escape behavior");

    resetPicImplState();
    showHandle = fakeHandle(10);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    g_picStream = makeRepeatedNoOutputPicStream(kRleSafetyBreakPairs);
    g_sequentialPicStreamReads = true;
    showPicFile(showHandle, kShowPage);
    require(g_pagePixels[0] == 0 &&
                g_pagePixels[kTinySurfaceWidth] == 0xEE &&
                g_fileReadCalls > kExpectedOneCall,
            "showPicFile preserves original PIC decoder safety break on no-output RLE streams");

    resetPicImplState();
    showHandle = fakeHandle(8);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    g_picStream = makePicStream(kPicHeaderMaxWidthByteMode,
                                {kLzwLiteralA, kFirstDictionaryCode});
    showPicFile(showHandle, kShowPage);
    require(g_pagePixels[0] == kLzwLiteralA &&
                g_pagePixels[1] == 0 &&
                g_pagePixels[2] == kLzwLiteralA,
            "showPicFile preserves original first-code dictionary-chain expansion");

    resetPicImplState();
    showHandle = fakeHandle(9);
    setupOneRowSurface(&g_pageSurface, g_pagePixels);
    g_picStream = makePicStream(kPicHeaderMaxWidthByteMode,
                                {kLzwLiteralA, kFirstKwKwKCode});
    showPicFile(showHandle, kShowPage);
    require(g_pagePixels[0] == kLzwLiteralA &&
                g_pagePixels[1] == kLzwLiteralA,
            "showPicFile preserves original LZW KwKwK not-yet-in-dictionary expansion");

    resetPicImplState();
    SDL_IOStream *decodeHandle = fakeHandle(2);
    setupOneRowSurface(&g_spriteSurface, g_spritePixels);
    decodePic(decodeHandle, kSpriteSegment);
    require(g_spriteSurfaceCalls == kExpectedOneCall &&
                g_lastSpriteSegment == kSpriteSegment,
            "decodePic resolves the original sprite-buffer segment target");
    require(g_fileReadCalls == kExpectedOneCall &&
                g_lastReadHandle == decodeHandle &&
                g_lastReadCount == kPicReadBlockSize,
            "decodePic initializes the original 512-byte PIC input buffer");
    require(g_spritePixels[0] == 0 && g_spritePixels[kTinySurfaceWidth - 1] == 0 &&
                g_spritePixels[kTinySurfaceWidth] == 0xEE,
            "decodePic writes decoded byte-mode PIC pixels into the sprite surface");

    resetPicImplState();
    SDL_IOStream *rawHandle = fakeHandle(4);
    decodePicRaw(rawHandle, kRawSegmentIgnored);
    require(g_fileReadCalls > kExpectedOneCall &&
                g_lastReadHandle == rawHandle &&
                g_lastReadCount == kPicReadBlockSize,
            "decodePicRaw preserves the original raw PIC decode stream path through the scratch target");

    resetPicImplState();
    picBlit(nullptr, kShowPage);
    require(g_hiResSurfaceCalls == kExpectedNoCalls &&
                g_presentHiResCalls == kExpectedNoCalls &&
                g_fileReadCalls == kExpectedNoCalls,
            "picBlit preserves the original no-op behavior for an invalid handle");

    resetPicImplState();
    g_hiResSurface.h = kHiResProgressRows;
    SDL_IOStream *blitHandle = fakeHandle(3);
    picBlit(blitHandle, kShowPage);
    require(g_hiResSurfaceCalls == kExpectedOneCall &&
                g_fileReadCalls > kExpectedOneCall &&
                g_lastReadHandle == blitHandle,
            "picBlit initializes the original stream and decodes the fixed 700-row title sweep");
    require(g_hiResPixels[0] == 0 && g_hiResPixels[kSurfaceWidth] == 0 &&
                g_hiResPixels[kHiResPitch - 1] == 0,
            "picBlit clears and writes the first original left/right half-row into hi-res pixels");
    require(g_presentHiResCalls > kExpectedOneCall,
            "picBlit preserves the original progressive title reveal presentations");

    std::cout << "shared_picimpl_behavior_tests passed\n";
    return 0;
}
