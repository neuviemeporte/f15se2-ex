#include "comm.h"
#include "const.h"
#include "eg3dload.h"
#include "egdata.h"
#include "egtypes.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

typedef struct SDL_IOStream SDL_IOStream;

extern void load3DG(void);
extern void load3D3(char *fileName);
extern void load3DT(char *fileName);
extern void load15Flt3d3(void);

char aRegn_xxx[] = "regn.xxx";
struct Game *gameData = nullptr;
struct GameComm *commData = nullptr;
SDL_IOStream *fileHandle = nullptr;

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum LoadOriginalConstant : int {
    kOpenModeRead = 0,
    kFailedOpenCount = 1,
    kFakeHandle = 0x515100,
    kPromptColor = 0x0F,
    kPromptFirstX = 104,
    kPromptFirstY = 40,
    kPromptSecondX = 104,
    kPromptSecondY = 50,
    kPhotoRetryPromptX = 108,
    kHeaderSkipBytes = 16,
    kGrid1Bytes = 0x100,
    kGridBytes = 0x200,
    kObjectChunkBytes = 0x800,
    kGridCopyBytes = 64,
    kTheaterIndex = 3,
    kExpectedTwoCalls = 2,
    kExpectedOneCall = 1,
    kDrawLogCapacity = 8,
    kNoGridOpenFailures = 0,
    kTileCategoryCount = 5,
    kTileObjectCount = 1,
    kTileObjectX = 11,
    kTileObjectY = -22,
    kTileObjectZ = 33,
    kTileObjectShapeWord = 0x87,
    kTileMatrixReadPrefix = 7,
    kTileObjectWordReads = 4,
    kTileOverflowObjectCount = 573,
    kTileOverflowTriggerObject = 572,
    kObjectOffsetWordCount = 1,
    kObjectDataBytes = 3,
    kObjectDataNearOverflowBytes = 0xADD2,
    kObjectDataTooBigBytes = 0xADD5,
    kObjectDataSeed = 0x70,
    kPhotoOffsetWordCount = 2,
    kPhotoFirstOffset = 0,
    kPhotoSecondOffset = 0x805,
    kPhotoObjectDataBytes = 0x808,
    kPhotoFirstChunkBytes = 0x800,
    kPhotoSecondChunkBytes = 5,
    kPhotoThirdChunkBytes = 3,
    kPhotoFinalStoredBytes = 6,
    kPhotoDiscardSeed = 0x60,
    kPhotoFirstDataSeed = 0x61,
    kPhotoSecondDataSeed = 0x62,
    kTargetSlotSubCount = 1,
    kOptionalObjectTriples = 2,
    kOptionalVertexXCount = 2,
    kOptionalVertexYCount = 1,
    kOptionalVertexZCount = 1,
    kOptionalSeed1 = 0x21,
    kOptionalSeed2 = 0x31,
    kOptionalSeed3 = 0x41,
    kOptionalVertexX0 = 0x1111,
    kOptionalVertexX1 = 0x2222,
    kOptionalVertexY0 = 0x3333,
    kOptionalVertexZ0 = 0x4444,
    kFlt15HeaderWord = 0x1357,
    kFlt15OffsetWordCount = 2,
    kFlt15FirstOffset = 0x1111,
    kFlt15SecondOffset = 0x2222,
    kFlt15ModelBytes = 0x805,
    kFlt15FirstChunkBytes = 0x800,
    kFlt15SecondChunkBytes = 5,
    kFlt15ModelSeed = 0x40,
    kPhotoPresenceOpenCount = 1,
    kObjectMinimalReadCount = 6,
    kPhotoSubobjectReadCount = 13,
    kPhotoOpenCountWithRetry = 3,
    kPhotoOpenCountWithSubobject = 3,
    kBadSignature = 0x9999,
    kTestFailureExitCode = 1,
};

enum class LoaderFile {
    None,
    Grid3DG,
    Tile3DT,
    Object3D3,
    Flt15_3D3,
    Photo3D3,
};

struct DrawCall {
    const char *text;
    int x;
    int y;
    int color;
};

SDL_IOStream *g_fakeHandle = reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kFakeHandle));
int g_openCalls = 0;
int g_readCalls = 0;
int g_closeCalls = 0;
int g_flipCalls = 0;
int g_keyCalls = 0;
int g_waitRetraceCalls = 0;
int g_drawCalls = 0;
int g_lastOpenMode = -1;
const char *g_lastOpenName = nullptr;
char g_lastOpenNameCopy[64] = {};
LoaderFile g_activeFile = LoaderFile::None;
int g_activeFileReadCalls = 0;
int g_gridOpenFailuresRemaining = kFailedOpenCount;
LoaderFile g_forcedOpenFailureFile = LoaderFile::None;
int g_forcedOpenFailuresRemaining = 0;
bool g_badGridSignature = false;
bool g_badTileSignature = false;
bool g_badObjectSignature = false;
bool g_tooManyTiles = false;
bool g_tileDataOverflow = false;
bool g_objectDataTooBig = false;
bool g_objectDataNearOverflow = false;
int g_objectDataNearOverflowRemaining = 0;
bool g_objectOptionalBuffers = false;
DrawCall g_drawLog[kDrawLogCapacity] = {};
struct Game g_game = {};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetLoadState() {
    std::strcpy(regnStr, "IRAN.xxx");
    std::memset(buf3d3, 0, sizeof(uint16) * 100);
    std::memset(g_world3dData, 0, 0xadd4);
    std::memset(buf_3dt, 0, MAX_TILE_DATA);
    std::memset(sizes3dt, 0, sizeof(uint16) * kTileCategoryCount);
    std::memset(matrix3dt, 0, sizeof(uint16) * kTileCategoryCount * 32);
    std::memset(matrix3dt_2, 0, sizeof(matrix3dt_2));
    std::memset(g_targetSlots, 0, sizeof(struct TargetSlot) * 2);
    std::memset(buf1_3dg, 0, kGrid1Bytes);
    std::memset(buf2_3dg, 0, kGridBytes);
    std::memset(buf3_3dg, 0, kGridBytes);
    std::memset(buf4_3dg, 0, kGridBytes);
    std::memset(g_topLodGrid, 0, kGridCopyBytes);
    std::memset(g_drawLog, 0, sizeof(g_drawLog));
    g_game = {};
    g_game.theater = kTheaterIndex;
    gameData = &g_game;
    fileHandle = nullptr;
    sign3dg = 0;
    sign3d3 = 0;
    sign3dt = 0;
    size3d3 = 0;
    size3d3_2 = 0;
    size3d3_3 = 0;
    g_unusedLoadDoneFlag = -1;
    g_openCalls = 0;
    g_readCalls = 0;
    g_closeCalls = 0;
    g_flipCalls = 0;
    g_keyCalls = 0;
    g_waitRetraceCalls = 0;
    g_drawCalls = 0;
    g_lastOpenMode = -1;
    g_lastOpenName = nullptr;
    g_lastOpenNameCopy[0] = 0;
    g_activeFile = LoaderFile::None;
    g_activeFileReadCalls = 0;
    g_gridOpenFailuresRemaining = kFailedOpenCount;
    g_forcedOpenFailureFile = LoaderFile::None;
    g_forcedOpenFailuresRemaining = 0;
    g_badGridSignature = false;
    g_badTileSignature = false;
    g_badObjectSignature = false;
    g_tooManyTiles = false;
    g_tileDataOverflow = false;
    g_objectDataTooBig = false;
    g_objectDataNearOverflow = false;
    g_objectDataNearOverflowRemaining = 0;
    g_objectOptionalBuffers = false;
}

void fillPattern(void *ptr, size_t count, int seed) {
    unsigned char *bytes = static_cast<unsigned char *>(ptr);
    for (size_t idx = 0; idx < count; ++idx) {
        bytes[idx] = static_cast<unsigned char>(seed + static_cast<int>(idx));
    }
}

void writeInt16(void *ptr, int value) {
    *static_cast<int16 *>(ptr) = static_cast<int16>(value);
}

void writeUint16(void *ptr, unsigned int value) {
    *static_cast<uint16 *>(ptr) = static_cast<uint16>(value);
}

bool hasSuffix(const char *text, const char *suffix) {
    const size_t textLen = std::strlen(text);
    const size_t suffixLen = std::strlen(suffix);
    return textLen >= suffixLen &&
           std::strcmp(text + textLen - suffixLen, suffix) == 0;
}

} // namespace

SDL_IOStream *openFile(const char *filename, int mode) {
    ++g_openCalls;
    g_lastOpenName = filename;
    std::strncpy(g_lastOpenNameCopy, filename, sizeof(g_lastOpenNameCopy) - 1);
    g_lastOpenNameCopy[sizeof(g_lastOpenNameCopy) - 1] = 0;
    g_lastOpenMode = mode;
    if (std::strcmp(filename, "photo.3d3") == 0) {
        g_activeFile = LoaderFile::Photo3D3;
    } else if (std::strcmp(filename, "15FLT.3D3") == 0) {
        g_activeFile = LoaderFile::Flt15_3D3;
    } else if (hasSuffix(filename, ".3dG")) {
        g_activeFile = LoaderFile::Grid3DG;
    } else if (hasSuffix(filename, ".3dT")) {
        g_activeFile = LoaderFile::Tile3DT;
    } else if (hasSuffix(filename, ".3D3")) {
        g_activeFile = LoaderFile::Object3D3;
    } else {
        g_activeFile = LoaderFile::None;
    }
    g_activeFileReadCalls = 0;
    if (g_activeFile == LoaderFile::Grid3DG && g_gridOpenFailuresRemaining > 0) {
        --g_gridOpenFailuresRemaining;
        return nullptr;
    }
    if (g_activeFile == g_forcedOpenFailureFile && g_forcedOpenFailuresRemaining > 0) {
        --g_forcedOpenFailuresRemaining;
        return nullptr;
    }
    return g_fakeHandle;
}

size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *handle) {
    require(handle == g_fakeHandle, "3D loaders read from the opened handle");
    const size_t bytes = size * count;
    ++g_readCalls;
    ++g_activeFileReadCalls;
    switch (g_activeFile) {
    case LoaderFile::Grid3DG:
        switch (g_activeFileReadCalls) {
        case 1:
            require(bytes == sizeof(int16), "load3DG reads the original 3DG signature word");
            writeInt16(ptr, g_badGridSignature ? kBadSignature : SIGNATURE_3DG);
            break;
        case 2:
            require(bytes == kHeaderSkipBytes, "load3DG reads the original unused 16-byte header");
            fillPattern(ptr, bytes, 0x10);
            break;
        case 3:
            require(bytes == kGrid1Bytes, "load3DG reads the original top-level grid bytes");
            fillPattern(ptr, bytes, 0x20);
            break;
        case 4:
            require(bytes == kGridBytes, "load3DG reads the original second grid buffer");
            fillPattern(ptr, bytes, 0x30);
            break;
        case 5:
            require(bytes == kGridBytes, "load3DG reads the original third grid buffer");
            fillPattern(ptr, bytes, 0x40);
            break;
        case 6:
            require(bytes == kGridBytes, "load3DG reads the original fourth grid buffer");
            fillPattern(ptr, bytes, 0x50);
            break;
        default:
            require(false, "load3DG should issue exactly the original six reads");
        }
        break;
    case LoaderFile::Tile3DT:
        switch (g_activeFileReadCalls) {
        case 1:
            require(bytes == sizeof(int16), "load3DT reads the original 3DT signature word");
            writeInt16(ptr, g_badTileSignature ? kBadSignature : SIGNATURE_3DT);
            break;
        case 2:
            require(bytes == sizeof(uint16) * kTileCategoryCount,
                    "load3DT reads the five original tile-category counts");
            writeUint16(ptr, g_tooManyTiles ? 33 : kTileObjectCount);
            std::memset(static_cast<uint16 *>(ptr) + 1, 0,
                        sizeof(uint16) * (kTileCategoryCount - 1));
            break;
        case 3:
            require(bytes == sizeof(uint16), "load3DT reads category-0 object-count matrix");
            writeUint16(ptr, g_tileDataOverflow ? kTileOverflowObjectCount : kTileObjectCount);
            break;
        case 4:
        case 5:
        case 6:
        case 7:
            require(bytes == 0, "load3DT still issues zero-count matrix reads for empty categories");
            break;
        case 8:
            require(bytes == sizeof(int16), "load3DT reads tile object X");
            writeInt16(ptr, kTileObjectX);
            break;
        case 9:
            require(bytes == sizeof(int16), "load3DT reads tile object Y");
            writeInt16(ptr, kTileObjectY);
            break;
        case 10:
            require(bytes == sizeof(int16), "load3DT reads tile object Z");
            writeInt16(ptr, kTileObjectZ);
            break;
        case 11:
            require(bytes == sizeof(int16), "load3DT reads tile object shape as a word");
            writeInt16(ptr, kTileObjectShapeWord);
            break;
        default:
            require(g_tileDataOverflow && bytes == sizeof(int16),
                    "load3DT overflow fixture repeats original x/y/z/shape word reads");
            switch ((g_activeFileReadCalls - 8) % 4) {
            case 0:
                writeInt16(ptr, kTileObjectX);
                break;
            case 1:
                writeInt16(ptr, kTileObjectY);
                break;
            case 2:
                writeInt16(ptr, kTileObjectZ);
                break;
            default:
                writeInt16(ptr, kTileObjectShapeWord);
                break;
            }
        }
        break;
    case LoaderFile::Object3D3:
        if (g_objectDataNearOverflow && g_activeFileReadCalls > 4) {
            if (g_objectDataNearOverflowRemaining > 0) {
                const int expectedChunk = g_objectDataNearOverflowRemaining > kObjectChunkBytes
                                              ? kObjectChunkBytes
                                              : g_objectDataNearOverflowRemaining;
                require(bytes == static_cast<size_t>(expectedChunk),
                        "load3D3 reads original chunked near-limit object data");
                fillPattern(ptr, bytes, kObjectDataSeed);
                g_objectDataNearOverflowRemaining -= expectedChunk;
            } else {
                require(bytes == 1, "load3D3 reads the optional-buffer count byte after chunked data");
                *static_cast<uint8 *>(ptr) = 0;
            }
            break;
        }
        switch (g_activeFileReadCalls) {
        case 1:
            require(bytes == sizeof(int16), "load3D3 reads the original 3D3 signature word");
            writeInt16(ptr, g_badObjectSignature ? kBadSignature : SIGNATURE_3D3);
            break;
        case 2:
            require(bytes == sizeof(uint16), "load3D3 reads the original object-offset count");
            writeUint16(ptr, kObjectOffsetWordCount);
            break;
        case 3:
            require(bytes == sizeof(uint16) * kObjectOffsetWordCount,
                    "load3D3 reads the original object-offset table");
            writeUint16(ptr, 0);
            break;
        case 4:
            require(bytes == sizeof(uint16), "load3D3 reads the original object-data byte count");
            writeUint16(ptr, g_objectDataTooBig ? kObjectDataTooBigBytes :
                             g_objectDataNearOverflow ? kObjectDataNearOverflowBytes :
                                                        kObjectDataBytes);
            if (g_objectDataNearOverflow) {
                g_objectDataNearOverflowRemaining = kObjectDataNearOverflowBytes;
            }
            break;
        case 5:
            require(bytes == kObjectDataBytes,
                    "load3D3 reads the original object-data bytes");
            fillPattern(ptr, bytes, kObjectDataSeed);
            break;
        case 6:
            require(bytes == 1, "load3D3 reads the optional-buffer count byte");
            *static_cast<uint8 *>(ptr) = g_objectOptionalBuffers ? kOptionalObjectTriples : 0;
            break;
        case 7:
            require(g_objectOptionalBuffers && bytes == kOptionalObjectTriples,
                    "load3D3 reads original optional buffer 1 bytes");
            fillPattern(ptr, bytes, kOptionalSeed1);
            break;
        case 8:
            require(g_objectOptionalBuffers && bytes == kOptionalObjectTriples,
                    "load3D3 reads original optional buffer 2 bytes");
            fillPattern(ptr, bytes, kOptionalSeed2);
            break;
        case 9:
            require(g_objectOptionalBuffers && bytes == kOptionalObjectTriples,
                    "load3D3 reads original optional buffer 3 bytes");
            fillPattern(ptr, bytes, kOptionalSeed3);
            break;
        case 10:
            require(g_objectOptionalBuffers && bytes == 1,
                    "load3D3 reads original vertex-X count byte");
            *static_cast<uint8 *>(ptr) = kOptionalVertexXCount;
            break;
        case 11:
            require(g_objectOptionalBuffers && bytes == sizeof(int16) * kOptionalVertexXCount,
                    "load3D3 reads original vertex-X words");
            static_cast<int16 *>(ptr)[0] = kOptionalVertexX0;
            static_cast<int16 *>(ptr)[1] = kOptionalVertexX1;
            break;
        case 12:
            require(g_objectOptionalBuffers && bytes == 1,
                    "load3D3 reads original vertex-Y count byte");
            *static_cast<uint8 *>(ptr) = kOptionalVertexYCount;
            break;
        case 13:
            require(g_objectOptionalBuffers && bytes == sizeof(int16) * kOptionalVertexYCount,
                    "load3D3 reads original vertex-Y words");
            static_cast<int16 *>(ptr)[0] = kOptionalVertexY0;
            break;
        case 14:
            require(g_objectOptionalBuffers && bytes == 1,
                    "load3D3 reads original vertex-Z count byte");
            *static_cast<uint8 *>(ptr) = kOptionalVertexZCount;
            break;
        case 15:
            require(g_objectOptionalBuffers && bytes == sizeof(int16) * kOptionalVertexZCount,
                    "load3D3 reads original vertex-Z words");
            static_cast<int16 *>(ptr)[0] = kOptionalVertexZ0;
            break;
        default:
            require(false, "load3D3 should issue the original minimal valid read sequence");
        }
        break;
    case LoaderFile::Flt15_3D3:
        switch (g_activeFileReadCalls) {
        case 1:
            require(bytes == sizeof(int16), "load15Flt3d3 reads the original discarded header word");
            writeInt16(ptr, kFlt15HeaderWord);
            break;
        case 2:
            require(bytes == sizeof(uint16), "load15Flt3d3 reads the aircraft offset-table word count");
            writeUint16(ptr, kFlt15OffsetWordCount);
            break;
        case 3:
            require(bytes == sizeof(uint16) * kFlt15OffsetWordCount,
                    "load15Flt3d3 reads the original aircraft offset table");
            static_cast<uint16 *>(ptr)[0] = kFlt15FirstOffset;
            static_cast<uint16 *>(ptr)[1] = kFlt15SecondOffset;
            break;
        case 4:
            require(bytes == sizeof(uint16), "load15Flt3d3 reads the aircraft model byte count");
            writeUint16(ptr, kFlt15ModelBytes);
            break;
        case 5:
            require(bytes == kFlt15FirstChunkBytes,
                    "load15Flt3d3 reads the first aircraft model chunk at the original 0x800-byte limit");
            fillPattern(ptr, bytes, kFlt15ModelSeed);
            break;
        case 6:
            require(bytes == kFlt15SecondChunkBytes,
                    "load15Flt3d3 reads the final partial aircraft model chunk");
            fillPattern(ptr, bytes, kFlt15ModelSeed + 1);
            break;
        default:
            require(false, "load15Flt3d3 should issue the original six reads for this fixture");
        }
        break;
    case LoaderFile::Photo3D3:
        switch (g_activeFileReadCalls) {
        case 1:
            require(bytes == sizeof(int16), "load3D3 reads photo.3d3 signature word for target subobjects");
            writeInt16(ptr, SIGNATURE_3D3);
            break;
        case 2:
            require(bytes == sizeof(uint16), "load3D3 reads photo.3d3 offset-table word count");
            writeUint16(ptr, kPhotoOffsetWordCount);
            break;
        case 3:
            require(bytes == sizeof(uint16) * kPhotoOffsetWordCount,
                    "load3D3 reads photo.3d3 target offset table");
            static_cast<uint16 *>(ptr)[0] = kPhotoFirstOffset;
            static_cast<uint16 *>(ptr)[1] = kPhotoSecondOffset;
            break;
        case 4:
            require(bytes == sizeof(uint16), "load3D3 reads photo.3d3 object-data byte count");
            writeUint16(ptr, kPhotoObjectDataBytes);
            break;
        case 5:
            require(bytes == kPhotoFirstChunkBytes,
                    "load3D3 discards full original 0x800-byte photo.3d3 chunks before target data");
            fillPattern(ptr, bytes, kPhotoDiscardSeed);
            break;
        case 6:
            require(bytes == kPhotoSecondChunkBytes,
                    "load3D3 reads the remaining bytes for the first target subobject");
            fillPattern(ptr, bytes, kPhotoFirstDataSeed);
            break;
        case 7:
            require(bytes == kPhotoThirdChunkBytes,
                    "load3D3 reads the second target subobject from the original offset table");
            fillPattern(ptr, bytes, kPhotoSecondDataSeed);
            break;
        default:
            require(false, "load3D3 should issue the original photo.3d3 target read sequence");
        }
        break;
    case LoaderFile::None:
        require(false, "3D loader read should be associated with a known file type");
    }
    return count;
}

void fileClose(SDL_IOStream *handle) {
    require(handle == g_fakeHandle, "3D loaders close the opened handle");
    ++g_closeCalls;
}

void drawStringBothPages(const char *text, int x, int y, int color) {
    require(g_drawCalls < kDrawLogCapacity, "3D loader draw log capacity is sufficient");
    g_drawLog[g_drawCalls++] = {text, x, y, color};
}

void gfx_flipPage(void) { ++g_flipCalls; }
int FAR CDECL misc_getKey(void) {
    ++g_keyCalls;
    return 0;
}
void gfx_waitRetrace(void) { ++g_waitRetraceCalls; }
void setDrawColor(int) {}
void fillRectBoth(int, int, int, int) {}
void setViewPosition(int, int, int) {}
int fixedMulQ14(int a, int b) { return static_cast<int>((static_cast<int32>(a) * b) >> 15); }
int sine(int) { return 0; }
void shiftLongLeftInPlace(int count, long *value) { *value <<= count; }
void shiftLongRightInPlace(int count, long *value) { *value >>= count; }
int FAR CDECL fillSpanRect(const int16 *, int, int, int, int) { return 0; }
int getTimeOfDay(void) { return 1; }
void drawStringActivePage(const char *, int, int, int) {}
int FAR CDECL misc_readJoystick(int16) { return 0; }

int main() {
    resetLoadState();
    load3DG();

    require(std::strcmp(regnStr, "IRAN.3dG") == 0,
            "load3DG rewrites the region filename to the original .3dG extension");
    require(g_openCalls == kExpectedTwoCalls &&
                std::strcmp(g_lastOpenName, "IRAN.3dG") == 0 &&
                g_lastOpenMode == kOpenModeRead,
            "load3DG retries opening the original region grid filename in read mode");
    require(g_drawCalls == kExpectedTwoCalls &&
                std::strcmp(g_drawLog[0].text, "Please insert F15 Disk B") == 0 &&
                g_drawLog[0].x == kPromptFirstX &&
                g_drawLog[0].y == kPromptFirstY &&
                g_drawLog[0].color == kPromptColor &&
                std::strcmp(g_drawLog[1].text, "  Press a key when ready") == 0 &&
                g_drawLog[1].x == kPromptSecondX &&
                g_drawLog[1].y == kPromptSecondY &&
                g_drawLog[1].color == kPromptColor,
            "load3DG preserves the original disk-B retry prompt text and placement");
    require(g_flipCalls == kExpectedOneCall &&
                g_keyCalls == kExpectedOneCall &&
                g_waitRetraceCalls == kExpectedOneCall,
            "load3DG preserves original retry flip/key and post-open retrace waits");
    require(sign3dg == SIGNATURE_3DG &&
                g_readCalls == 6 &&
                g_closeCalls == kExpectedOneCall,
            "load3DG reads the original grid sections and closes the file");
    require(buf1_3dg[0] == 0x20 &&
                buf2_3dg[0] == 0x30 &&
                buf3_3dg[0] == 0x40 &&
                buf4_3dg[0] == 0x50,
            "load3DG stores the original grid buffers after the discarded 16-byte header");
    require(std::memcmp(g_topLodGrid,
                        g_theaterGrids + kTheaterIndex * kGridCopyBytes,
                        kGridCopyBytes) == 0,
            "load3DG copies the original theater top-LOD grid slice");

    resetLoadState();
    g_gridOpenFailuresRemaining = kNoGridOpenFailures;
    g_badGridSignature = true;
    load3DG();
    require(g_readCalls == kExpectedOneCall &&
                g_closeCalls == kExpectedOneCall &&
                g_drawCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Bad Grid file format.") == 0,
            "load3DG preserves original bad-signature error, close, and key-wait path");

    resetLoadState();
    load3DT(regnStr);

    require(std::strcmp(regnStr, "IRAN.3dT") == 0 &&
                std::strcmp(g_lastOpenNameCopy, "IRAN.3dT") == 0 &&
                g_lastOpenMode == kOpenModeRead,
            "load3DT rewrites and opens the original lowercase tile extension in read mode");
    require(sign3dt == SIGNATURE_3DT &&
                sizes3dt[0] == kTileObjectCount &&
                matrix3dt[0][0] == kTileObjectCount,
            "load3DT preserves the original signature, category count, and object-count matrix");
    require(matrix3dt_2[0][0] == reinterpret_cast<struct TileSceneObject *>(buf_3dt),
            "load3DT stores the original per-category tile object pointer");
    require(matrix3dt_2[0][0]->x == kTileObjectX &&
                matrix3dt_2[0][0]->y == kTileObjectY &&
                matrix3dt_2[0][0]->z == kTileObjectZ &&
                matrix3dt_2[0][0]->shape == static_cast<uint8>(kTileObjectShapeWord),
            "load3DT stores tile object coordinates and truncates the original shape word to a byte");
    require(g_readCalls == 11 && g_closeCalls == kExpectedOneCall,
            "load3DT issues the original minimal valid read sequence and closes the file");

    resetLoadState();
    g_forcedOpenFailureFile = LoaderFile::Tile3DT;
    g_forcedOpenFailuresRemaining = kFailedOpenCount;
    load3DT(regnStr);
    require(g_readCalls == 0 &&
                g_closeCalls == 0 &&
                g_drawCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Open Error on *.3DT") == 0,
            "load3DT preserves original open-error message without closing a missing file");

    resetLoadState();
    g_badTileSignature = true;
    load3DT(regnStr);
    require(g_readCalls == kExpectedOneCall &&
                g_closeCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Bad Tile file format.") == 0,
            "load3DT preserves original bad-signature error and close path");

    resetLoadState();
    g_tooManyTiles = true;
    load3DT(regnStr);
    require(g_readCalls == kExpectedTwoCalls &&
                g_closeCalls == 0 &&
                std::strcmp(g_drawLog[0].text, "Too many tiles.") == 0,
            "load3DT preserves original too-many-tiles error without closing the open handle");

    resetLoadState();
    g_tileDataOverflow = true;
    load3DT(regnStr);
    require(g_readCalls == kTileMatrixReadPrefix + kTileOverflowTriggerObject * kTileObjectWordReads &&
                g_closeCalls == 0 &&
                std::strcmp(g_drawLog[0].text, "Too much tile data") == 0,
            "load3DT preserves original late tile-data overflow guard without closing the open handle");

    resetLoadState();
    load3D3(regnStr);

    require(std::strcmp(regnStr, "IRAN.3D3") == 0 &&
                std::strcmp(g_lastOpenNameCopy, "photo.3d3") == 0,
            "load3D3 rewrites the object filename and probes the original photo.3d3 disk-B file");
    require(sign3d3 == SIGNATURE_3D3 &&
                size3d3 == kObjectOffsetWordCount &&
                buf3d3[kObjectOffsetWordCount] == kObjectDataBytes,
            "load3D3 preserves the original signature, offset count, and appended end offset");
    require(static_cast<uint8>(g_world3dData[0]) == kObjectDataSeed &&
                static_cast<uint8>(g_world3dData[1]) == kObjectDataSeed + 1 &&
                static_cast<uint8>(g_world3dData[2]) == kObjectDataSeed + 2 &&
                size3d3_3 == 0,
            "load3D3 reads object bytes and skips optional vertex buffers when the count is zero");
    require(g_openCalls == kExpectedTwoCalls &&
                g_readCalls == 6 &&
                g_closeCalls == kExpectedTwoCalls &&
                g_waitRetraceCalls == kExpectedOneCall,
            "load3D3 closes the object file, probes photo.3d3 once, and waits for retrace");

    resetLoadState();
    g_forcedOpenFailureFile = LoaderFile::Photo3D3;
    g_forcedOpenFailuresRemaining = kFailedOpenCount;
    load3D3(regnStr);
    require(g_openCalls == kPhotoOpenCountWithRetry &&
                g_readCalls == kObjectMinimalReadCount &&
                g_closeCalls == kExpectedTwoCalls &&
                g_drawCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Please insert F15 Disk B") == 0 &&
                g_drawLog[0].x == kPhotoRetryPromptX &&
                g_drawLog[0].y == kPromptFirstY &&
                g_drawLog[0].color == kPromptColor &&
                g_flipCalls == kExpectedOneCall &&
                g_keyCalls == kExpectedOneCall,
            "load3D3 preserves the original photo.3d3 disk-B retry prompt and retry open");

    resetLoadState();
    g_targetSlots[0].flags = kTargetSlotSubCount << 8;
    load3D3(regnStr);
    require(g_openCalls == kPhotoOpenCountWithSubobject &&
                g_readCalls == kPhotoSubobjectReadCount &&
                g_closeCalls == kPhotoOpenCountWithSubobject,
            "load3D3 opens photo.3d3 again and reads the original target-subobject stream");
    require(static_cast<uint8>(g_world3dData[kObjectDataBytes]) == kPhotoSecondDataSeed &&
                buf3d3[kObjectOffsetWordCount + 1] == kPhotoFinalStoredBytes,
            "load3D3 preserves the original photo subobject overwrite and appended end-offset behavior");

    resetLoadState();
    g_objectDataNearOverflow = true;
    g_targetSlots[0].flags = kTargetSlotSubCount << 8;
    load3D3(regnStr);
    require(g_drawCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "ObjData overflow") == 0,
            "load3D3 preserves original final object-data overflow guard after photo subobjects");

    resetLoadState();
    g_forcedOpenFailureFile = LoaderFile::Object3D3;
    g_forcedOpenFailuresRemaining = kFailedOpenCount;
    load3D3(regnStr);
    require(g_readCalls == 0 &&
                g_closeCalls == 0 &&
                std::strcmp(g_drawLog[0].text, "Open Error on *.3D3") == 0,
            "load3D3 preserves original open-error message without closing a missing object file");

    resetLoadState();
    g_badObjectSignature = true;
    load3D3(regnStr);
    require(g_readCalls == kExpectedOneCall &&
                g_closeCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Bad Obj file format.") == 0,
            "load3D3 preserves original bad-signature error and close path");

    resetLoadState();
    g_objectDataTooBig = true;
    load3D3(regnStr);
    require(g_readCalls == 4 &&
                g_closeCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Object data too big.") == 0,
            "load3D3 preserves original object-data size guard and close path");

    resetLoadState();
    g_objectOptionalBuffers = true;
    load3D3(regnStr);
    require(size3d3_3 == kOptionalObjectTriples &&
                static_cast<uint8>(buf3d3_1[0]) == kOptionalSeed1 &&
                static_cast<uint8>(buf3d3_2[0]) == kOptionalSeed2 &&
                static_cast<uint8>(buf3d3_3[0]) == kOptionalSeed3 &&
                g_replayLog.vertexX[0] == kOptionalVertexX0 &&
                g_replayLog.vertexX[1] == kOptionalVertexX1 &&
                reinterpret_cast<int16 *>(g_modelVertY)[0] == kOptionalVertexY0 &&
                reinterpret_cast<int16 *>(g_modelVertZ)[0] == kOptionalVertexZ0,
            "load3D3 preserves original optional object buffers and vertex-coordinate sections");

    resetLoadState();
    std::memset(g_aircraftModels, 0, kFlt15ModelBytes);
    load15Flt3d3();
    require(std::strcmp(a15flt_xxx, "15FLT.3D3") == 0 &&
                std::strcmp(g_lastOpenNameCopy, "15FLT.3D3") == 0 &&
                g_lastOpenMode == kOpenModeRead,
            "load15Flt3d3 rewrites and opens the original aircraft-model filename");
    require(flt15HeaderWord == kFlt15HeaderWord &&
                flt15_size == kFlt15OffsetWordCount &&
                reinterpret_cast<uint16 *>(flt15_buf1)[0] == kFlt15FirstOffset &&
                reinterpret_cast<uint16 *>(flt15_buf1)[1] == kFlt15SecondOffset,
            "load15Flt3d3 reads the original header and aircraft offset table");
    require(static_cast<uint8>(g_aircraftModels[0]) == kFlt15ModelSeed &&
                static_cast<uint8>(g_aircraftModels[kFlt15FirstChunkBytes - 1]) ==
                    static_cast<uint8>(kFlt15ModelSeed + kFlt15FirstChunkBytes - 1) &&
                static_cast<uint8>(g_aircraftModels[kFlt15FirstChunkBytes]) ==
                    static_cast<uint8>(kFlt15ModelSeed + 1),
            "load15Flt3d3 reads aircraft model bytes in original 0x800-byte chunks");
    require(g_readCalls == 6 && g_closeCalls == kExpectedOneCall,
            "load15Flt3d3 issues the original read sequence and closes the file");

    resetLoadState();
    g_forcedOpenFailureFile = LoaderFile::Flt15_3D3;
    g_forcedOpenFailuresRemaining = kExpectedOneCall;
    load15Flt3d3();
    require(std::strcmp(g_lastOpenNameCopy, "15FLT.3D3") == 0 &&
                g_readCalls == 0 &&
                g_closeCalls == 0 &&
                g_drawCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "Open Error on *.3D3") == 0,
            "load15Flt3d3 preserves original open-error message and returns before reads");

    resetLoadState();
    g_gridOpenFailuresRemaining = kNoGridOpenFailures;
    load3DAll();

    require(std::strcmp(regnStr, "IRAN.3D3") == 0,
            "load3DAll preserves the original load order ending with the .3D3 filename");
    require(g_unusedLoadDoneFlag == 0,
            "load3DAll clears the original unused completion flag");
    require(g_openCalls == 4 &&
                g_closeCalls == 4 &&
                g_waitRetraceCalls == 2,
            "load3DAll opens grid, tile, object, and photo-probe files with the original close/retrace pattern");

    resetLoadState();
    printError("loader error");

    require(g_flipCalls == kExpectedOneCall &&
                g_drawCalls == kExpectedOneCall &&
                std::strcmp(g_drawLog[0].text, "loader error") == 0 &&
                g_drawLog[0].x == 0 &&
                g_drawLog[0].y == 96 &&
                g_drawLog[0].color == kPromptColor &&
                g_keyCalls == kExpectedOneCall,
            "printError preserves the original flip, message placement, color, and key wait");

    std::cout << "load_behavior_tests passed\n";
    return 0;
}
