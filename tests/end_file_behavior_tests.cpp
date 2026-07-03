#include "endata.h"
#include "inttype.h"
#include "shared/common.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

extern void srandInit(int seed);
extern int loadFileSection(char *name, int b, int c);
extern int writeFileSection(char *name, int b, int c, int d, int e);
extern void enSeedRandom(void);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum EndFileOriginalConstant : int {
    kInitialRandSeed = 0x1111,
    kInitialRandState = 0x2222,
    kSeedValue = 0x3456,
    kTimeOfDaySeed = 0x4567,
    kOpenModeRead = 0,
    kCreateAttrDefault = 0,
    kReadCountAll = -1,
    kLoadOffset = 0x1234,
    kLoadSegment = 0x5678,
    kWriteOffset = 0x2345,
    kWriteSegment = 0x6789,
    kWriteUnused = 0x1357,
    kWriteCount = 0x2468,
    kReadResult = 77,
    kWriteResult = 88,
};

struct OpenCall {
    std::string name;
    int mode;
};

struct ReadCall {
    int count;
    int offset;
    int segment;
};

struct WriteCall {
    int count;
    int offset;
    int segment;
    int unused;
};

std::vector<OpenCall> g_openCalls;
std::vector<OpenCall> g_createCalls;
std::vector<SDL_IOStream *> g_closeCalls;
std::vector<ReadCall> g_readCalls;
std::vector<WriteCall> g_writeCalls;
SDL_IOStream *g_openHandle = reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(0x1000));
SDL_IOStream *g_createHandle = reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(0x2000));
int g_timeCalls = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void resetRecorder(void) {
    g_openCalls.clear();
    g_createCalls.clear();
    g_closeCalls.clear();
    g_readCalls.clear();
    g_writeCalls.clear();
    g_timeCalls = 0;
}

} // namespace

SDL_IOStream *openFileWrapper(const char *filename, int mode) {
    g_openCalls.push_back({filename, mode});
    return g_openHandle;
}

SDL_IOStream *createFile(const char *filename, int attr) {
    g_createCalls.push_back({filename, attr});
    return g_createHandle;
}

void closeFileWrapper(SDL_IOStream *handle) { g_closeCalls.push_back(handle); }

int readFileAt(SDL_IOStream *, int count, int offset, int segment) {
    g_readCalls.push_back({count, offset, segment});
    return kReadResult;
}

int writeFile(SDL_IOStream *, int count, int offset, int segment, int unused) {
    g_writeCalls.push_back({count, offset, segment, unused});
    return kWriteResult;
}

int getTimeOfDay(void) {
    ++g_timeCalls;
    return kTimeOfDaySeed;
}

int main() {
    randSeed = kInitialRandSeed;
    randState = kInitialRandState;
    srandInit(kSeedValue);
    require(randSeed == kSeedValue && randState == 0,
            "srandInit stores the original seed and resets randState");

    randSeed = kInitialRandSeed;
    randState = kInitialRandState;
    resetRecorder();
    enSeedRandom();
    require(randSeed == kTimeOfDaySeed && randState == 0 && g_timeCalls == 1,
            "enSeedRandom preserves original seedRandom behavior using getTimeOfDay");

    resetRecorder();
    char loadName[] = "WORLD.DAT";
    const int loadResult = loadFileSection(loadName, kLoadOffset, kLoadSegment);
    require(loadResult == kReadResult,
            "loadFileSection returns the original readFileAt result");
    require(g_openCalls.size() == 1 && g_openCalls[0].name == loadName &&
                g_openCalls[0].mode == kOpenModeRead,
            "loadFileSection opens the requested file in original read mode");
    require(g_readCalls.size() == 1 && g_readCalls[0].count == kReadCountAll &&
                g_readCalls[0].offset == kLoadOffset &&
                g_readCalls[0].segment == kLoadSegment,
            "loadFileSection reads all bytes into the original offset and segment");
    require(g_closeCalls.size() == 1 && g_closeCalls[0] == g_openHandle,
            "loadFileSection closes the original opened handle");

    resetRecorder();
    char writeName[] = "SAVE.DAT";
    const int writeResult =
        writeFileSection(writeName, kWriteOffset, kWriteSegment, kWriteUnused, kWriteCount);
    require(writeResult == kWriteResult,
            "writeFileSection returns the original writeFile result");
    require(g_createCalls.size() == 1 && g_createCalls[0].name == writeName &&
                g_createCalls[0].mode == kCreateAttrDefault,
            "writeFileSection creates the requested file with original default attributes");
    require(g_writeCalls.size() == 1 && g_writeCalls[0].count == kWriteCount &&
                g_writeCalls[0].offset == kWriteOffset &&
                g_writeCalls[0].segment == kWriteSegment &&
                g_writeCalls[0].unused == kWriteUnused,
            "writeFileSection forwards original write count, offset, segment, and flag");
    require(g_closeCalls.size() == 1 && g_closeCalls[0] == g_createHandle,
            "writeFileSection closes the original created handle");

    std::cout << "end_file_behavior_tests passed\n";
    return 0;
}
