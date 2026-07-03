#include "endata.h"
#include "headless.h"

#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

/* END's per-tick quit guard (file-scope in enmain.c). */
void checkQuitFlag(void);

namespace {

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    test_headless_init();

    /* quitFlag clear: checkQuitFlag returns to its caller without tearing down. */
    quitFlag = 0;
    checkQuitFlag();
    require(true, "checkQuitFlag returns without exiting when quitFlag is clear");

    /* quitFlag set: checkQuitFlag cleans up and exits(0). Observe via a child. */
    quitFlag = 1;
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for END quitFlag exit behavior");
    if (child == 0) {
        checkQuitFlag();
        std::exit(2); /* must not be reached */
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child,
            "parent should wait for END quitFlag exit child");
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "checkQuitFlag cleans up and exits(0) when quitFlag is set");

    std::cout << "end_runtime_behavior_tests passed\n";
    return 0;
}
