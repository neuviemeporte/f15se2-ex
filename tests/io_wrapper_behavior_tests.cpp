#include "egtypes.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

typedef struct SDL_IOStream SDL_IOStream;

extern SDL_IOStream *createFileWrapper(const char *filename, int attr);
extern int readFile1Wrapper(int handle, int count, int bufOffset);
extern int readFile2Wrapper(int handle, int count, int bufOffset, int bufSegment);
extern int writeFileAtRawWrapper(int handle, int count, int bufOffset, int bufSegment, int offsetAddend);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum IoWrapperOriginalConstant : int {
    kCreateAttr = 0x20,
    kReadHandle = 0x1234,
    kReadCount = 0x40,
    kReadOffset = 0x5678,
    kReadSegment = 0x9ABC,
    kWriteHandle = 0x4321,
    kWriteCount = 0x55,
    kWriteOffset = 0x1111,
    kWriteSegment = 0x2222,
    kWriteAddend = 0x33,
    kCreateSentinelHandle = 0x12345678,
    kRead1Result = 0x101,
    kRead2Result = 0x202,
    kWriteResult = 0x303,
    kExpectedOneCall = 1,
    kTestFailureExitCode = 1,
};

int g_createCalls = 0;
int g_read1Calls = 0;
int g_read2Calls = 0;
int g_writeCalls = 0;
const char *g_lastCreateName = nullptr;
int g_lastCreateAttr = 0;
int g_lastReadHandle = 0;
int g_lastReadCount = 0;
int g_lastReadOffset = 0;
int g_lastReadSegment = 0;
int g_lastWriteHandle = 0;
int g_lastWriteCount = 0;
int g_lastWriteOffset = 0;
int g_lastWriteSegment = 0;
int g_lastWriteAddend = 0;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void resetIoWrapperState() {
    g_createCalls = 0;
    g_read1Calls = 0;
    g_read2Calls = 0;
    g_writeCalls = 0;
    g_lastCreateName = nullptr;
    g_lastCreateAttr = 0;
    g_lastReadHandle = 0;
    g_lastReadCount = 0;
    g_lastReadOffset = 0;
    g_lastReadSegment = 0;
    g_lastWriteHandle = 0;
    g_lastWriteCount = 0;
    g_lastWriteOffset = 0;
    g_lastWriteSegment = 0;
    g_lastWriteAddend = 0;
}

} // namespace

SDL_IOStream *createFile(const char *filename, int attr) {
    ++g_createCalls;
    g_lastCreateName = filename;
    g_lastCreateAttr = attr;
    return reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kCreateSentinelHandle));
}

int readFile1(int handle, int count, int bufOffset) {
    ++g_read1Calls;
    g_lastReadHandle = handle;
    g_lastReadCount = count;
    g_lastReadOffset = bufOffset;
    return kRead1Result;
}

int readFile2(int handle, int count, int bufOffset, int bufSegment) {
    ++g_read2Calls;
    g_lastReadHandle = handle;
    g_lastReadCount = count;
    g_lastReadOffset = bufOffset;
    g_lastReadSegment = bufSegment;
    return kRead2Result;
}

int writeFileAtRaw(int handle, int count, int bufOffset, int bufSegment, int offsetAddend) {
    ++g_writeCalls;
    g_lastWriteHandle = handle;
    g_lastWriteCount = count;
    g_lastWriteOffset = bufOffset;
    g_lastWriteSegment = bufSegment;
    g_lastWriteAddend = offsetAddend;
    return kWriteResult;
}

int main() {
    resetIoWrapperState();
    SDL_IOStream *created = createFileWrapper("pilot.dat", kCreateAttr);
    require(g_createCalls == kExpectedOneCall &&
                std::strcmp(g_lastCreateName, "pilot.dat") == 0 &&
                g_lastCreateAttr == kCreateAttr &&
                created == reinterpret_cast<SDL_IOStream *>(static_cast<uintptr_t>(kCreateSentinelHandle)),
            "createFileWrapper forwards the original filename and attribute to createFile");

    resetIoWrapperState();
    require(readFile1Wrapper(kReadHandle, kReadCount, kReadOffset) == kRead1Result &&
                g_read1Calls == kExpectedOneCall &&
                g_lastReadHandle == kReadHandle &&
                g_lastReadCount == kReadCount &&
                g_lastReadOffset == kReadOffset,
            "readFile1Wrapper forwards the original near-buffer read arguments");

    resetIoWrapperState();
    require(readFile2Wrapper(kReadHandle, kReadCount, kReadOffset, kReadSegment) == kRead2Result &&
                g_read2Calls == kExpectedOneCall &&
                g_lastReadHandle == kReadHandle &&
                g_lastReadCount == kReadCount &&
                g_lastReadOffset == kReadOffset &&
                g_lastReadSegment == kReadSegment,
            "readFile2Wrapper forwards the original far-buffer read arguments");

    resetIoWrapperState();
    require(writeFileAtRawWrapper(kWriteHandle, kWriteCount, kWriteOffset, kWriteSegment, kWriteAddend) ==
                    kWriteResult &&
                g_writeCalls == kExpectedOneCall &&
                g_lastWriteHandle == kWriteHandle &&
                g_lastWriteCount == kWriteCount &&
                g_lastWriteOffset == kWriteOffset &&
                g_lastWriteSegment == kWriteSegment &&
                g_lastWriteAddend == kWriteAddend,
            "writeFileAtRawWrapper forwards the original far-buffer write arguments");

    std::cout << "io_wrapper_behavior_tests passed\n";
    return 0;
}
