#pragma once

#ifdef __EMSCRIPTEN__
void web_yield(void);
#else
static inline void web_yield(void) {}
#endif
