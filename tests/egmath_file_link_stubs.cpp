#include "inttype.h"

#include <cstddef>
#include <cstring>

typedef struct SDL_IOStream SDL_IOStream;

SDL_IOStream *fileHandle = nullptr;

void printError(const char *) {}

SDL_IOStream *openFile(const char *, int) {
    return nullptr;
}

void fileClose(SDL_IOStream *) {}

size_t fileRead(void *ptr, size_t size, size_t count, SDL_IOStream *) {
    if (ptr != nullptr && size != 0 && count != 0) {
        std::memset(ptr, 0, size * count);
    }
    return count;
}
