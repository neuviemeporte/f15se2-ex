#include "output.h"

#include <stdarg.h>

#include <SDL3/SDL.h>

void DEBUG(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG, format, ap);
    va_end(ap);
}
