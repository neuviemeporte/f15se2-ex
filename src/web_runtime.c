#include "web_runtime.h"
#include <emscripten.h>

void web_yield(void) {
    static double nextYieldMs = 0.0;
    const double nowMs = emscripten_get_now();
    if (nowMs < nextYieldMs) return;

    // Legacy polling loops must return control to browser input and audio.
    emscripten_sleep(0);
    nextYieldMs = emscripten_get_now() + 8.0;
}
