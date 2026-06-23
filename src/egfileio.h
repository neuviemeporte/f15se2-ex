#ifndef F15_SE2_EGFILEIO
#define F15_SE2_EGFILEIO
/* public interface of egfileio.c */
#include "egtypes.h"

typedef struct SDL_IOStream SDL_IOStream;

SDL_IOStream * __cdecl openFileWrapper(const char *filename, int mode);
void closeFileWrapper(SDL_IOStream *handle);

#endif /* F15_SE2_EGFILEIO */
