// LINK_CORE + headless. END's per-tick quit guard checkQuitFlag(): when quitFlag
// is set it tears down (cleanup + restoreCbreakHandler) and exit(0)s; when clear
// it must return to the caller. Both branches are observed in forked children so
// the "returns without exiting" case is actually verified (not just asserted) —
// the child reaching a distinct sentinel code proves control flow returned.
#include "endata.h"
#include "headless.h"

#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

/* END's per-tick quit guard (file-scope in enmain.c). */
void checkQuitFlag(void);

namespace {

// Distinct from exit(0) (the quit path) and from 1 (require's failure code), so
// each observed child status maps to exactly one branch.
enum { kReturnedSentinel = 7, kTestFailureExitCode = 1 };

void require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "failed: " << message << '\n';
        std::exit(kTestFailureExitCode);
    }
}

// Run checkQuitFlag() in a child with the given quitFlag and return the child's
// exit status. If checkQuitFlag returns, the child reaches exit(kReturnedSentinel).
int runInChild(int flag) {
    const pid_t child = fork();
    require(child >= 0, "test should be able to fork for checkQuitFlag behavior");
    if (child == 0) {
        quitFlag = flag;
        checkQuitFlag();
        std::exit(kReturnedSentinel); /* only reached if checkQuitFlag returned */
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child, "parent should wait for checkQuitFlag child");
    require(WIFEXITED(status), "checkQuitFlag child should exit normally, not signal");
    return WEXITSTATUS(status);
}

} // namespace

int main() {
    test_headless_init();

    require(runInChild(/*quitFlag=*/0) == kReturnedSentinel,
            "checkQuitFlag returns to its caller (no teardown/exit) when quitFlag is clear");
    require(runInChild(/*quitFlag=*/1) == 0,
            "checkQuitFlag cleans up and exit(0)s when quitFlag is set");

    std::cout << "end_runtime_behavior_tests passed\n";
    return 0;
}
