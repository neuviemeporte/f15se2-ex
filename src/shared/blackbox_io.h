#ifndef F15_SE2_BLACKBOX_IO_H
#define F15_SE2_BLACKBOX_IO_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Blackbox-aware file seams. Outside record/replay these are ordinary SDL file
 * opens. During replay mutable snapshots are served from memory; writes use a
 * disposable successful stream so game control flow remains unchanged. */
SDL_IOStream *blackbox_openReadPath(const char *name, const char *resolvedPath);
SDL_IOStream *blackbox_openWritePath(const char *name, const char *resolvedPath);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_IO_H */
