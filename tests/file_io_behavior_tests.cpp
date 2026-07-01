#include "shared/common.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstdarg>
#include <cstring>
#include <string>
#include "posix_test_compat.h"

extern int fileReadRaw(SDL_IOStream *io, void *dst, int count);
extern int readFile(SDL_IOStream *io, int count, int bufOffset);
extern int readFileAt(SDL_IOStream *io, int count, int offset, int segment);
extern int writeFile(SDL_IOStream *io, int count, int offset, int segment, int unused);
extern int writeFileAtRaw(SDL_IOStream *io, void *buf, uint16 count);
extern void dos_printstring(const char *str);
extern void errorAndExit(const char *msg);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum FileIoOriginalConstant : int {
    kDosReadMode = 0,
    kDosDefaultCreateAttr = 0,
    kReadWholeRemainingStream = -1,
    kNullStreamError = -1,
    kZeroSizedTransferResult = 0,
    kUpperDatItemSize = 2,
    kUpperDatItemCount = 2,
    kUpperDatBytesRead = 4,
    kUpperDatRemainingBytes = 2,
    kReplacementItemSize = 1,
    kReplacementItemCount = 2,
    kRawFileBytes = 3,
    kDosNegativeWriteCount = -1,
    kDosNegativeCountBytes = 0xFFFF,
    kMappedDosSegment = 0x0100,
    kMappedDosOffset = 0,
    kMappedDosAddress = 0x01000000,
    kNullTransferItemSize = 0,
    kNullTransferItemCount = 5,
    kSingleNullReadByte = 1,
    kDosStringTerminator = '$',
    kErrorExitStatus = 1,
    kScratchBufferBytes = 8,
    kMappedDosBytes = 0x10000,
};

std::string g_lastLogInfo;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

void resetLogCapture() {
    g_lastLogInfo.clear();
}

std::string readWholeFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

void log_info(const char *fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_lastLogInfo = buf;
}

int main() {
    const auto oldCwd = std::filesystem::current_path();
    const auto testDir = std::filesystem::temp_directory_path() / "f15se2-ex-file-io-tests";
    std::filesystem::remove_all(testDir);
    std::filesystem::create_directories(testDir);
    std::filesystem::current_path(testDir);

    {
        std::ofstream out("UPPER.DAT", std::ios::binary);
        out << "abcdef";
    }

    SDL_IOStream *input = openFile("upper.dat", kDosReadMode);
    require(input != nullptr, "openFile resolves DOS-style uppercase asset names");
    char buf[kScratchBufferBytes] = {};
    require(fileRead(buf, kUpperDatItemSize, kUpperDatItemCount, input) == kUpperDatItemCount,
            "fileRead returns whole item count");
    require(std::string(buf, kUpperDatBytesRead) == "abcd", "fileRead transfers requested bytes");
    require(fileReadRaw(input, buf, kReadWholeRemainingStream) == kUpperDatRemainingBytes,
            "fileReadRaw negative count reads remaining stream bytes");
    fileClose(input);

    SDL_IOStream *output = createFile("upper.dat", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile resolves existing path case-insensitively");
    const char replacement[] = "XY";
    require(fileWrite(replacement, kReplacementItemSize, kReplacementItemCount, output) == kReplacementItemCount,
            "fileWrite returns whole item count");
    fileClose(output);
    require(readWholeFile(testDir / "UPPER.DAT") == "XY",
            "createFile overwrites the existing uppercase spelling");

    output = createFile("newfile.bin", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile creates new files");
    char raw[] = {'1', '2', '3'};
    require(writeFileAtRaw(output, raw, sizeof(raw)) == kRawFileBytes, "writeFileAtRaw writes host buffers");
    fileClose(output);
    require(readWholeFile(testDir / "newfile.bin") == "123", "writeFileAtRaw persisted bytes");

    void *dosMapped = mmap(reinterpret_cast<void *>(static_cast<uintptr_t>(kMappedDosAddress)),
                           kMappedDosBytes,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                           -1,
                           0);
    require(dosMapped != MAP_FAILED,
            "test should be able to map a deterministic legacy MK_FP buffer");
    std::memset(dosMapped, 'D', kMappedDosBytes);
    output = createFile("doswrite.bin", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile creates DOS-address write test file");
    require(writeFile(output,
                      kDosNegativeWriteCount,
                      kMappedDosOffset,
                      kMappedDosSegment,
                      0) == kDosNegativeCountBytes,
            "writeFile preserves original DOS negative-count 0xffff-byte write");
    fileClose(output);
    require(readWholeFile(testDir / "doswrite.bin").size() == kDosNegativeCountBytes,
            "writeFile persisted the original negative-count byte total");
    require(munmap(dosMapped, kMappedDosBytes) == 0,
            "test should release the deterministic legacy MK_FP buffer");

    require(fileReadRaw(nullptr, buf, kSingleNullReadByte) == kNullStreamError,
            "fileReadRaw reports null stream");
    require(readFile(nullptr, kSingleNullReadByte, 0) == kNullStreamError,
            "readFile reports null stream before dereferencing DOS offset");
    require(readFileAt(nullptr, kSingleNullReadByte, 0, 0) == kNullStreamError,
            "readFileAt reports null stream before dereferencing DOS segment offset");
    require(writeFile(nullptr, kSingleNullReadByte, 0, 0, 0) == kNullStreamError,
            "writeFile reports null stream before dereferencing DOS segment offset");
    require(fileRead(buf, kNullTransferItemSize, kNullTransferItemCount, nullptr) == kZeroSizedTransferResult,
            "fileRead handles null/zero-size reads");
    require(fileWrite(buf, kNullTransferItemSize, kNullTransferItemCount, nullptr) == kZeroSizedTransferResult,
            "fileWrite handles null/zero-size writes");
    require(writeFileAtRaw(nullptr, raw, sizeof(raw)) == kNullStreamError,
            "writeFileAtRaw reports null stream");
    fileClose(nullptr);

    resetLogCapture();
    char dosMessage[] = {'O', 'K', kDosStringTerminator, 'X', 0};
    dos_printstring(dosMessage);
    require(g_lastLogInfo == "OK",
            "dos_printstring logs bytes up to the original DOS '$' terminator");

    {
        const pid_t pid = fork();
        require(pid >= 0, "test should be able to fork for errorAndExit behavior");
        if (pid == 0) {
            char fatalMessage[] = {'F', 'A', 'T', 'A', 'L', kDosStringTerminator, 0};
            errorAndExit(fatalMessage);
            std::exit(1);
        }
        int status = 0;
        require(waitpid(pid, &status, 0) == pid,
                "test should be able to wait for errorAndExit child process");
        require(WIFEXITED(status) &&
                    WEXITSTATUS(status) == kErrorExitStatus,
                "errorAndExit preserves the original fatal exit status");
    }

    std::filesystem::current_path(oldCwd);
    std::filesystem::remove_all(testDir);

    std::cout << "file_io_behavior_tests passed\n";
    return 0;
}
