#ifndef F15_SE2_ENAWARD
#define F15_SE2_ENAWARD
/* awards screen + buffer alloc (enaward.c) */
#include "inttype.h"
#include <SDL3/SDL_iostream.h>

void *allocBuffer(int16 size);
void showPostMissionAwards(void);
void loadPicFromFile(const char *name, uint16 segment);
void loadPicFromFileAt(const char *name, uint16 segment, int16 off, SDL_IOWhence whence);

#endif /* F15_SE2_ENAWARD */
