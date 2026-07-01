#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <csignal>
#include "posix_test_compat.h"
#include <vector>

int vgapal_program_main(int argc, char *argv[]);

// The test target renames vgapal.c's program entry point; keep this file's test
// entry point normal.
#ifdef main
#undef main
#endif

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum VgapalBehaviorConstant : int {
    kPaletteComponents = 256 * 3,
    kComponentsPerOutputLine = 12,
    kExpectedOutputLines = kPaletteComponents / kComponentsPerOutputLine,
    kClassicVgaMaxChannel = 63,
    kModernMaxChannel = 255,
    kBrownPaletteIndex = 6,
    kBrownRedComponent = 170,
    kBrownGreenComponent = 85,
    kBrownBlueComponent = 0,
    kStdoutFd = 1,
    kExpectedSuccess = 0,
    kExpectedFailure = 1,
    kTestFailureExitCode = 1,
    kAbortSignal = SIGABRT,
};

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

std::vector<int> parseInts(const std::string &text) {
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), ',', ' ');
    std::istringstream stream(normalized);
    std::vector<int> values;
    int value = 0;
    while (stream >> value) {
        values.push_back(value);
    }
    return values;
}

std::string captureVgapal256() {
    const std::string path = "/tmp/f15se2-ex-vgapal-256.txt";
    int savedStdout = dup(kStdoutFd);
    require(savedStdout >= 0, "test should be able to save stdout");

    FILE *file = std::freopen(path.c_str(), "w", stdout);
    require(file != nullptr, "test should be able to redirect stdout");

    char program[] = "vgapal";
    char levels[] = "256";
    char *argv[] = {program, levels, nullptr};
    const int rc = vgapal_program_main(2, argv);

    std::fflush(stdout);
    dup2(savedStdout, kStdoutFd);
    close(savedStdout);

    require(rc == 0, "vgapal 256 should exit successfully");

    std::ifstream input(path);
    require(input.good(), "test should be able to read captured palette");
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

int runVgapalChild(int argc, char **argv) {
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for independent vgapal process state");
    if (child == 0) {
        FILE *out = std::freopen("/dev/null", "w", stdout);
        FILE *err = std::freopen("/dev/null", "w", stderr);
        require(out != nullptr && err != nullptr,
                "child should be able to silence vgapal stdout/stderr");
        std::exit(vgapal_program_main(argc, argv));
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for vgapal child");
    require(WIFEXITED(status), "vgapal child should exit normally");
    return WEXITSTATUS(status);
}

int runVgapalSignalChild(int argc, char **argv, bool runTwice = false) {
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for aborting vgapal process state");
    if (child == 0) {
        FILE *out = std::freopen("/dev/null", "w", stdout);
        FILE *err = std::freopen("/dev/null", "w", stderr);
        require(out != nullptr && err != nullptr,
                "child should be able to silence aborting vgapal stdout/stderr");
        (void)vgapal_program_main(argc, argv);
        if (runTwice) {
            // The original utility is not re-entrant: palette construction
            // keeps static process state, so a second in-process run aborts.
            (void)vgapal_program_main(argc, argv);
        }
        std::exit(kTestFailureExitCode);
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for aborting vgapal child");
    require(WIFSIGNALED(status), "vgapal child should terminate by signal");
    return WTERMSIG(status);
}

} // namespace

int main() {
    require(runVgapalSignalChild(0, nullptr) == kAbortSignal,
            "vgapal aborts on the original invalid argc/argv precondition");
    {
        char program[] = "vgapal";
        char *argv[] = {program, nullptr};
        require(runVgapalSignalChild(2, argv) == kAbortSignal,
                "vgapal aborts when the original required levels argument pointer is null");
    }
    {
        char program[] = "vgapal";
        char levels[] = "64";
        char *argv[] = {program, levels, nullptr};
        require(runVgapalChild(2, argv) == kExpectedSuccess,
                "vgapal 64 preserves original successful 64-level command-line mode");
        require(runVgapalSignalChild(2, argv, true) == kAbortSignal,
                "vgapal preserves original non-reentrant palette state on a second in-process run");
    }
    {
        char program[] = "vgapal";
        char *argv[] = {program, nullptr};
        require(runVgapalChild(1, argv) == kExpectedFailure,
                "vgapal no-argument mode preserves original help/failure status");
    }
    {
        char program[] = "vgapal";
        char levels[] = "256";
        char extra[] = "extra";
        char *argv[] = {program, levels, extra, nullptr};
        require(runVgapalChild(3, argv) == kExpectedFailure,
                "vgapal too-many-arguments mode preserves original failure status");
    }
    {
        char program[] = "vgapal";
        char levels[] = "128";
        char *argv[] = {program, levels, nullptr};
        require(runVgapalChild(2, argv) == kExpectedFailure,
                "vgapal invalid-level mode preserves original failure status");
    }

    const std::string text = captureVgapal256();
    const std::vector<int> values = parseInts(text);

    require(values.size() == kPaletteComponents,
            "vgapal 256 should emit every RGB component of the 256-color palette");

    for (int value : values) {
        require(value >= 0 && value <= kModernMaxChannel,
                "vgapal 256 should upconvert every component into modern byte range");
    }

    const int brownOffset = kBrownPaletteIndex * 3;
    require(values[brownOffset] == kBrownRedComponent &&
                values[brownOffset + 1] == kBrownGreenComponent &&
                values[brownOffset + 2] == kBrownBlueComponent,
            "standard palette index 6 should stay brown, not dark yellow");

    const int lineBreaks = static_cast<int>(
        std::count(text.begin(), text.end(), '\n'));
    require(lineBreaks == kExpectedOutputLines,
            "vgapal 256 should keep the original twelve-component row format");

    require(std::find(values.begin(), values.end(), kClassicVgaMaxChannel) == values.end() &&
                std::find(values.begin(), values.end(), kModernMaxChannel) != values.end(),
            "vgapal 256 should use the original bit-duplication upconversion instead of raw VGA DAC values");

    std::cout << "vgapal behavior tests passed\n";
    return 0;
}
