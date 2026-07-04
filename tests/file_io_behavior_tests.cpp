#include "shared/common.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

/* file_io helpers not surfaced through common.h. */
extern int fileReadRaw(SDL_IOStream *io, void *dst, int count);
extern void errorAndExit(const char *msg);

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, or sentinel resets.
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
    kRawItemSize = 1,
    kRawItemCount = 3,
    kNullTransferItemSize = 0,
    kNullTransferItemCount = 5,
    kSingleByte = 1,
    kErrorExitStatus = 1,
    kScratchBufferBytes = 8,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

std::string readWholeFile(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

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

    // openFile resolves DOS-style uppercase asset names case-insensitively.
    SDL_IOStream *input = openFile("upper.dat", kDosReadMode);
    require(input != nullptr, "openFile resolves DOS-style uppercase asset names");
    char buf[kScratchBufferBytes] = {};
    require(fileRead(buf, kUpperDatItemSize, kUpperDatItemCount, input) == kUpperDatItemCount,
            "fileRead returns whole item count");
    require(std::string(buf, kUpperDatBytesRead) == "abcd", "fileRead transfers requested bytes");
    require(fileReadRaw(input, buf, kReadWholeRemainingStream) == kUpperDatRemainingBytes,
            "fileReadRaw negative count reads remaining stream bytes");
    fileClose(input);

    // createFile resolves the existing spelling and truncates it for writing.
    SDL_IOStream *output = createFile("upper.dat", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile resolves existing path case-insensitively");
    const char replacement[] = "XY";
    require(fileWrite(replacement, kReplacementItemSize, kReplacementItemCount, output) == kReplacementItemCount,
            "fileWrite returns whole item count");
    fileClose(output);
    require(readWholeFile(testDir / "UPPER.DAT") == "XY",
            "createFile overwrites the existing uppercase spelling");

    // createFile creates new files; fileWrite persists a host buffer.
    output = createFile("newfile.bin", kDosDefaultCreateAttr);
    require(output != nullptr, "createFile creates new files");
    char raw[] = {'1', '2', '3'};
    require((int)fileWrite(raw, kRawItemSize, kRawItemCount, output) == kRawItemCount,
            "fileWrite writes host buffers");
    fileClose(output);
    require(readWholeFile(testDir / "newfile.bin") == "123", "fileWrite persisted bytes");

    // Null/zero-size guards: the I/O helpers reject bad streams before dereferencing.
    require(fileReadRaw(nullptr, buf, kSingleByte) == kNullStreamError,
            "fileReadRaw reports null stream");
    require(fileRead(buf, kNullTransferItemSize, kNullTransferItemCount, nullptr) == kZeroSizedTransferResult,
            "fileRead handles null/zero-size reads");
    require(fileWrite(buf, kNullTransferItemSize, kNullTransferItemCount, nullptr) == kZeroSizedTransferResult,
            "fileWrite handles null/zero-size writes");
    fileClose(nullptr);

    // errorAndExit prints its '$'-terminated message and exits with the fatal status.
    {
        const pid_t pid = fork();
        require(pid >= 0, "test should be able to fork for errorAndExit behavior");
        if (pid == 0) {
            char fatalMessage[] = {'F', 'A', 'T', 'A', 'L', '$', 0};
            errorAndExit(fatalMessage);
            std::exit(0); /* must not be reached */
        }
        int status = 0;
        require(waitpid(pid, &status, 0) == pid,
                "test should be able to wait for errorAndExit child process");
        require(WIFEXITED(status) && WEXITSTATUS(status) == kErrorExitStatus,
                "errorAndExit preserves the original fatal exit status");
    }

    std::filesystem::current_path(oldCwd);
    std::filesystem::remove_all(testDir);

    std::cout << "file_io_behavior_tests passed\n";
    return 0;
}
