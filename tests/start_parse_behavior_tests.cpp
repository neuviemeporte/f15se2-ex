#include "shared/common.h"
#include "stdata.h"

#include "dos_compat.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

void parseGrid(void);
void parseGridTerrain(void);
void parseTerrain(char *filename);
void replaceExtension(char *path, const char *source);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum StartParseOriginalConstant : int {
    kGridEntryCount = 16,
    kGridBuf2Size = 0x100,
    kGridBuf3Size = 0x200,
    kGridBuf4Size = 0x200,
    kGridBuf5Size = 0x200,
    kMissingGridValidFlag = 0,
    kOpenModeRead = 0,
    kGridMagic = 0x3232,
    kTerrainMagic = 0x3131,
    kBadSignature = 0x1111,
    kTerrainLevelCount = 5,
    kTerrainTileLimit = 32,
    kTerrainOverflowTileCount = 600,
    kGoodGridBuf1Byte = 0x11,
    kGoodGridBuf2Byte = 0x22,
    kGoodGridBuf3Byte = 0x33,
    kGoodGridBuf4Byte = 0x44,
    kGoodGridBuf5Byte = 0x55,
    kGoodGridScriptBytes = 2 + kGridEntryCount + kGridBuf2Size +
                           kGridBuf3Size + kGridBuf4Size + kGridBuf5Size,
    kExpectedOneCall = 1,
    kPromptX = 0,
    kPromptY = 96,
    kPromptColor = 0x0F,
    kReturnedKey = 0x1C0D,
    kDirtyByte = 0xA5,
    kExpectedTwoCalls = 2,
    kTerrainClean = 0,
    kTestFailureExitCode = 1,
};

int g_openCalls = 0;
int g_keyCalls = 0;
int g_messageCalls = 0;
const char *g_lastOpenName = nullptr;
int g_lastOpenMode = 0;
    const char *g_lastMessage = nullptr;
    int g_lastMessageA = 0;
    int g_lastMessageB = 0;
    int g_lastMessageC = 0;
    const uint8 *g_scriptedFileData = nullptr;
    size_t g_scriptedFileSize = 0;
    size_t g_scriptedFileOffset = 0;
    SDL_IOStream *g_scriptedHandle = reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(1));

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetParseState() {
    g_openCalls = 0;
    g_keyCalls = 0;
    g_messageCalls = 0;
    g_lastOpenName = nullptr;
    g_lastOpenMode = 0;
    g_lastMessage = nullptr;
    g_lastMessageA = 0;
    g_lastMessageB = 0;
    g_lastMessageC = 0;
    g_scriptedFileData = nullptr;
    g_scriptedFileSize = 0;
    g_scriptedFileOffset = 0;
    gridValidFlag = 1;
    terrainDirtyFlag = 1;
    std::memset(gridBuf1, kDirtyByte, kGridEntryCount);
    std::memset(gridBuf2, kDirtyByte, kGridBuf2Size);
    std::memset(gridBuf3, kDirtyByte, kGridBuf3Size);
    std::memset(gridBuf4, kDirtyByte, kGridBuf4Size);
    std::memset(gridBuf5, kDirtyByte, kGridBuf5Size);
}

bool allZero(const uint8 *buf, int count) {
    for (int i = 0; i < count; ++i) {
        if (buf[i] != 0) return false;
    }
    return true;
    }

    void setScriptedFile(const uint8 *data, size_t size) {
        g_scriptedFileData = data;
        g_scriptedFileSize = size;
        g_scriptedFileOffset = 0;
    }

    void appendWord(uint8 *data, size_t &offset, uint16 value) {
        data[offset++] = static_cast<uint8>(value & 0xFF);
        data[offset++] = static_cast<uint8>(value >> 8);
    }

} // namespace

SDL_IOStream *openFile(const char *filename, int mode) {
    ++g_openCalls;
    g_lastOpenName = filename;
    g_lastOpenMode = mode;
    return g_scriptedFileData ? g_scriptedHandle : nullptr;
}

void fileClose(SDL_IOStream *) {}

size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *) {
    const size_t requested = size * count;
    size_t available = 0;
    if (g_scriptedFileOffset < g_scriptedFileSize) {
        available = g_scriptedFileSize - g_scriptedFileOffset;
    }
    const size_t copied = requested < available ? requested : available;
    if (copied != 0) {
        std::memcpy(ptr, g_scriptedFileData + g_scriptedFileOffset, copied);
    }
    if (copied < requested) {
        std::memset(static_cast<uint8 *>(ptr) + copied, 0, requested - copied);
    }
    g_scriptedFileOffset += copied;
    return size == 0 ? 0 : copied / size;
}

void mystrcpy(char *dest, const char *source) {
    std::strcpy(dest, source);
}

void nearmemset(void *dst, char value, int count) {
    std::memset(dst, static_cast<unsigned char>(value), count);
}

void doNothing2(const char *msg, int a, int b, int c) {
    ++g_messageCalls;
    g_lastMessage = msg;
    g_lastMessageA = a;
    g_lastMessageB = b;
    g_lastMessageC = c;
}

int far misc_getKey(void) {
    ++g_keyCalls;
    return kReturnedKey;
}

int main() {
    char pathWithDot[] = "REGN.XYZ";
    char pathWithoutDot[] = "REGN";

    replaceExtension(pathWithDot, ".3dG");
    require(std::strcmp(pathWithDot, "REGN.3dG") == 0,
            "replaceExtension overwrites the original extension at the first dot");

    replaceExtension(pathWithoutDot, ".3dT");
    require(std::strcmp(pathWithoutDot, "REGN.3dT") == 0,
            "replaceExtension appends the extension when no dot is present");

    resetParseState();
    std::strcpy(regnPlhPtr, "REGN.XYZ");
    parseGrid();
    require(g_openCalls == kExpectedOneCall &&
                std::strcmp(g_lastOpenName, "REGN.3dG") == 0 &&
                g_lastOpenMode == kOpenModeRead,
            "parseGrid opens the original .3dG filename in read mode");
    require(g_messageCalls == kExpectedOneCall &&
                std::strcmp(g_lastMessage, "Open Error on *.3DG, assuming new file !") == 0 &&
                g_lastMessageA == kPromptX &&
                g_lastMessageB == kPromptY &&
                g_lastMessageC == kPromptColor &&
                g_keyCalls == kExpectedOneCall,
            "parseGrid missing-file path shows the original prompt and waits for a key");
    for (int i = 0; i < kGridEntryCount; ++i) {
        require(gridBuf1[i] == i,
                "parseGrid missing-file path initializes gridBuf1 as an identity table");
    }
    require(allZero(gridBuf2, kGridBuf2Size) &&
                allZero(gridBuf3, kGridBuf3Size) &&
                allZero(gridBuf4, kGridBuf4Size) &&
                allZero(gridBuf5, kGridBuf5Size) &&
                gridValidFlag == kMissingGridValidFlag,
            "parseGrid missing-file path clears all grid buffers and invalidates the grid");

    resetParseState();
    std::strcpy(regnPlhPtr, "REGN.XYZ");
    parseTerrain(regnPlhPtr);
    require(g_openCalls == kExpectedOneCall &&
                std::strcmp(g_lastOpenName, "REGN.3dT") == 0 &&
                g_lastOpenMode == kOpenModeRead,
            "parseTerrain opens the original .3dT filename in read mode");
    require(g_messageCalls == kExpectedOneCall &&
                std::strcmp(g_lastMessage, "Open Error on *.3DT, assuming new file !") == 0 &&
                g_keyCalls == kExpectedOneCall,
            "parseTerrain missing-file path shows the original prompt and waits for a key");

    resetParseState();
    {
        uint8 badGrid[] = {static_cast<uint8>(kBadSignature & 0xFF),
                           static_cast<uint8>(kBadSignature >> 8)};
        setScriptedFile(badGrid, sizeof(badGrid));
        std::strcpy(regnPlhPtr, "REGN.XYZ");
        parseGrid();
    }
    require(g_messageCalls == kExpectedOneCall &&
                std::strcmp(g_lastMessage, "Bad Grid file format.") == 0 &&
                g_keyCalls == kExpectedOneCall,
            "parseGrid bad-signature path shows the original bad grid prompt");

    resetParseState();
    {
        uint8 goodGrid[kGoodGridScriptBytes] = {};
        size_t offset = 0;
        appendWord(goodGrid, offset, kGridMagic);
        std::memset(goodGrid + offset, kGoodGridBuf1Byte, kGridEntryCount);
        offset += kGridEntryCount;
        std::memset(goodGrid + offset, kGoodGridBuf2Byte, kGridBuf2Size);
        offset += kGridBuf2Size;
        std::memset(goodGrid + offset, kGoodGridBuf3Byte, kGridBuf3Size);
        offset += kGridBuf3Size;
        std::memset(goodGrid + offset, kGoodGridBuf4Byte, kGridBuf4Size);
        offset += kGridBuf4Size;
        std::memset(goodGrid + offset, kGoodGridBuf5Byte, kGridBuf5Size);
        setScriptedFile(goodGrid, sizeof(goodGrid));
        std::strcpy(regnPlhPtr, "REGN.XYZ");
        parseGrid();
    }
    require(g_messageCalls == 0 &&
                gridBuf1[0] == kGoodGridBuf1Byte &&
                gridBuf2[0] == kGoodGridBuf2Byte &&
                gridBuf3[0] == kGoodGridBuf3Byte &&
                gridBuf4[0] == kGoodGridBuf4Byte &&
                gridBuf5[0] == kGoodGridBuf5Byte,
            "parseGrid valid-file path reads the original five grid buffers");

    resetParseState();
    {
        uint8 badTerrain[] = {static_cast<uint8>(kBadSignature & 0xFF),
                              static_cast<uint8>(kBadSignature >> 8)};
        setScriptedFile(badTerrain, sizeof(badTerrain));
        std::strcpy(regnPlhPtr, "REGN.XYZ");
        parseTerrain(regnPlhPtr);
    }
    require(g_messageCalls == kExpectedOneCall &&
                std::strcmp(g_lastMessage, "Bad Tile file format.") == 0 &&
                g_keyCalls == kExpectedOneCall,
            "parseTerrain bad-signature path shows the original bad tile prompt");

    resetParseState();
    {
        uint8 tooManyTiles[2 + kTerrainLevelCount * 2] = {};
        size_t offset = 0;
        appendWord(tooManyTiles, offset, kTerrainMagic);
        appendWord(tooManyTiles, offset, kTerrainTileLimit + 1);
        for (int idx = 1; idx < kTerrainLevelCount; ++idx) {
            appendWord(tooManyTiles, offset, 0);
        }
        setScriptedFile(tooManyTiles, sizeof(tooManyTiles));
        std::strcpy(regnPlhPtr, "REGN.XYZ");
        parseTerrain(regnPlhPtr);
    }
    require(g_messageCalls == kExpectedOneCall &&
                std::strcmp(g_lastMessage, "Too many tiles.") == 0 &&
                g_keyCalls == kExpectedOneCall,
            "parseTerrain preserves original per-level tile-count limit prompt");

    resetParseState();
    {
        uint8 tooMuchData[2 + kTerrainLevelCount * 2 + 2] = {};
        size_t offset = 0;
        appendWord(tooMuchData, offset, kTerrainMagic);
        appendWord(tooMuchData, offset, 1);
        for (int idx = 1; idx < kTerrainLevelCount; ++idx) {
            appendWord(tooMuchData, offset, 0);
        }
        appendWord(tooMuchData, offset, kTerrainOverflowTileCount);
        setScriptedFile(tooMuchData, sizeof(tooMuchData));
        std::strcpy(regnPlhPtr, "REGN.XYZ");
        parseTerrain(regnPlhPtr);
    }
    require(g_messageCalls == kExpectedOneCall &&
                std::strcmp(g_lastMessage, "Too much tile data") == 0 &&
                g_keyCalls == kExpectedOneCall,
            "parseTerrain preserves original tile-data overflow prompt");

    resetParseState();
    std::strcpy(regnPlhPtr, "REGN.XYZ");
    parseGridTerrain();
    require(g_openCalls == kExpectedTwoCalls &&
                std::strcmp(g_lastOpenName, "REGN.3dT") == 0,
            "parseGridTerrain runs original grid parse followed by terrain parse");
    require(terrainDirtyFlag == kTerrainClean,
            "parseGridTerrain clears the original terrain dirty flag after parsing");

    std::cout << "start_parse_behavior_tests passed\n";
    return 0;
}
