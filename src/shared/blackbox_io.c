#include "blackbox_io.h"

#include "blackbox.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>

static const unsigned char kEmptySnapshotByte = 0;

static void captureMutableFile(const char *name, const char *path) {
    FILE *file;
    long fileSize;
    unsigned char *data;

    if (!blackbox_shouldCaptureMutableFile(name)) return;
    file = fopen(path, "rb");
    if (!file) {
        /* Absence is state too: replay must not fall back to another machine's
         * local pilot roster when the recording started without one. */
        blackbox_captureMutableFile(name, NULL, 0);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (fileSize = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0 || (unsigned long)fileSize > 0xfffffffful) {
        log_error("blackbox: unable to size mutable file %s", name);
        fclose(file);
        return;
    }
    data = fileSize ? (unsigned char *)malloc((size_t)fileSize) : NULL;
    if ((fileSize && !data) ||
        (fileSize && fread(data, 1, (size_t)fileSize, file) != (size_t)fileSize)) {
        log_error("blackbox: unable to capture mutable file %s", name);
        free(data);
        fclose(file);
        return;
    }
    blackbox_captureMutableFile(name, data, (uint32)fileSize);
    free(data);
    fclose(file);
}

SDL_IOStream *blackbox_openReadPath(const char *name, const char *resolvedPath) {
    const uint8 *replayData = NULL;
    uint32 replaySize = 0;
    if (blackbox_replayMutableFile(name, &replayData, &replaySize)) {
        log_info("blackbox: replay loaded %s from captured mutable state", name);
        return SDL_IOFromConstMem(replaySize ? replayData : &kEmptySnapshotByte,
                                  replaySize);
    }
    captureMutableFile(name, resolvedPath);
    return SDL_IOFromFile(resolvedPath, "rb");
}

SDL_IOStream *blackbox_openWritePath(const char *name, const char *resolvedPath) {
    if (blackbox_suppressPersistentWrites()) {
        /* A successful disposable stream preserves the caller's normal path;
         * returning NULL would incorrectly turn every save into an I/O error. */
        log_info("blackbox: redirected write to %s into memory", name);
        return SDL_IOFromDynamicMem();
    }
    return SDL_IOFromFile(resolvedPath, "wb");
}
