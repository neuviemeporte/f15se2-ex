#ifndef OUTPUT_H
#define OUTPUT_H

#include "inttype.h"

#include <stdlib.h>
#include <SDL3/SDL.h>

#define INFO(...)  SDL_Log(__VA_ARGS__)
#define ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define FATAL(...) do { SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__); exit(1); } while (0)

void DEBUG(const char *format, ...);

#endif // OUTPUT_H
