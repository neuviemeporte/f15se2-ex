#include "log.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

// Behavior-sensitive constants are named here or explained at the use site.
// Remaining numeric literals are fixture data, indices, loop/math mechanics,
// or zero/null/sentinel resets.
enum LogBehaviorConstant : int {
    kExpectedCategoryApplication = SDL_LOG_CATEGORY_APPLICATION,
    kWarnValueFixture = 7,
    kDebugValueFixture = 3,
    kExpectedOneCall = 1,
    kCriticalExitStatus = 1,
    kTestFailureExitCode = 1,
};

struct CapturedLog {
    int calls = 0;
    int category = -1;
    SDL_LogPriority priority = SDL_LOG_PRIORITY_INVALID;
    std::string message;
};

CapturedLog g_log;

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

void captureLog(void *, int category, SDL_LogPriority priority,
                const char *message) {
    ++g_log.calls;
    g_log.category = category;
    g_log.priority = priority;
    g_log.message = message ? message : "";
}

void resetCapture() {
    g_log = CapturedLog{};
}

} // namespace

int main() {
    SDL_SetLogOutputFunction(captureLog, nullptr);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_VERBOSE);

    resetCapture();
    log_set_app("egame");
    log_warn("speed %d", kWarnValueFixture);
    require(g_log.calls == kExpectedOneCall,
            "one tagged warning should emit one SDL log line");
    require(g_log.category == kExpectedCategoryApplication,
            "log wrapper uses SDL application category");
    require(g_log.priority == SDL_LOG_PRIORITY_WARN,
            "log_warn preserves SDL warning priority");
    require(g_log.message == "egame: speed 7",
            "log_set_app prefixes non-empty application tags");

    resetCapture();
    log_set_app(nullptr);
    log_info("plain");
    require(g_log.calls == kExpectedOneCall,
            "one untagged info message should emit one SDL log line");
    require(g_log.priority == SDL_LOG_PRIORITY_INFO,
            "log_info preserves SDL info priority");
    require(g_log.message == "plain",
            "null application tag restores unprefixed logging");

    resetCapture();
    log_set_app("start");
    log_message("boot");
    require(g_log.calls == kExpectedOneCall &&
                g_log.priority == SDL_LOG_PRIORITY_INFO &&
                g_log.message == "start: boot",
            "log_message uses info priority and application tag");

    resetCapture();
    log_verbose("detail");
    require(g_log.calls == kExpectedOneCall &&
                g_log.priority == SDL_LOG_PRIORITY_VERBOSE &&
                g_log.message == "start: detail",
            "log_verbose preserves SDL verbose priority");

    resetCapture();
    log_debug("step %d", kDebugValueFixture);
    require(g_log.calls == kExpectedOneCall &&
                g_log.priority == SDL_LOG_PRIORITY_DEBUG &&
                g_log.message == "start: step 3",
            "log_debug preserves SDL debug priority and formatting");

    resetCapture();
    log_error("bad");
    require(g_log.calls == kExpectedOneCall &&
                g_log.priority == SDL_LOG_PRIORITY_ERROR &&
                g_log.message == "start: bad",
            "log_error preserves SDL error priority");

    {
        const pid_t pid = fork();
        require(pid >= 0, "test should be able to fork for log_critical exit behavior");
        if (pid == 0) {
            SDL_SetLogOutputFunction(captureLog, nullptr);
            log_critical("fatal");
            std::exit(kTestFailureExitCode);
        }
        int status = 0;
        require(waitpid(pid, &status, 0) == pid,
                "test should be able to wait for log_critical child process");
        require(WIFEXITED(status) &&
                    WEXITSTATUS(status) == kCriticalExitStatus,
                "log_critical preserves the original fatal exit status");
    }

    SDL_SetLogOutputFunction(nullptr, nullptr);
    SDL_ResetLogPriorities();
    std::cout << "log behavior tests passed\n";
    return 0;
}
