/*
 * Portable blackbox state metadata.
 *
 * Keep mutable-state capture separate from the event recorder. The replay core
 * records deterministic input/timer/RNG events; this file records the external
 * state needed to make those events meaningful on another developer machine.
 */
#include "blackbox_state.h"
#include "log.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct BlackboxMutableFile {
    char name[32];
    uint8 *data;
    uint32 size;
    int recorded;
} BlackboxMutableFile;

static const char *s_currentBuildVersion = "unknown";
static char s_replayBuildVersion[128];
static int s_haveReplayBuildVersion = 0;
static int s_allowBuildMismatch = 0;
static BlackboxMutableFile s_hallfame;

static int sameName(const char *a, const char *b) {
    unsigned char ca, cb;
    if (!a || !b) return 0;
    do {
        ca = (unsigned char)*a++;
        cb = (unsigned char)*b++;
        if (tolower(ca) != tolower(cb)) return 0;
    } while (ca && cb);
    return 1;
}

static unsigned char hexDigit(unsigned value) {
    static const char digits[] = "0123456789abcdef";
    return (unsigned char)digits[value & 0xfu];
}

static int fromHexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void freeMutableFile(BlackboxMutableFile *file) {
    free(file->data);
    file->data = NULL;
    file->name[0] = '\0';
    file->size = 0;
    file->recorded = 0;
}

void blackbox_state_reset(void) {
    s_replayBuildVersion[0] = '\0';
    s_haveReplayBuildVersion = 0;
    freeMutableFile(&s_hallfame);
}

void blackbox_state_setBuildVersion(const char *version) {
    s_currentBuildVersion = (version && version[0]) ? version : "unknown";
}

void blackbox_state_setAllowBuildMismatch(int allow) {
    s_allowBuildMismatch = allow != 0;
}

const char *blackbox_state_currentBuildVersion(void) {
    return s_currentBuildVersion;
}

const char *blackbox_state_replayBuildVersion(void) {
    return s_haveReplayBuildVersion ? s_replayBuildVersion : "";
}

void blackbox_state_writeRecordHeader(FILE *file) {
    if (!file) return;
    fprintf(file, "build_version %s\n", s_currentBuildVersion);
}

int blackbox_state_shouldCaptureMutableFile(const char *name) {
    return sameName(name, "HallFame") && !s_hallfame.recorded;
}

void blackbox_state_recordMutableFile(FILE *file, const char *name, const uint8 *data, uint32 size) {
    uint32 i;
    if (!file || !name || (!data && size != 0) || !blackbox_state_shouldCaptureMutableFile(name)) return;
    fprintf(file, "mutable_file HallFame %u ", (unsigned)size);
    if (size == 0) fputc('-', file);
    for (i = 0; i < size; i++) {
        fputc(hexDigit(data[i] >> 4), file);
        fputc(hexDigit(data[i]), file);
    }
    fputc('\n', file);
    fflush(file);
    s_hallfame.recorded = 1;
}

static int parseMutableFile(const char *name, unsigned size, const char *hex) {
    uint8 *data;
    unsigned i;
    size_t encodedSize;
    if (!sameName(name, "HallFame")) return -1;
    if (size == 0) {
        if (strcmp(hex, "-") != 0) return -1;
        encodedSize = 0;
    } else {
        encodedSize = (size_t)size * 2u;
        if (strlen(hex) != encodedSize) return -1;
    }
    data = (uint8 *)malloc(size ? size : 1u);
    if (!data) return -1;
    for (i = 0; i < size; i++) {
        int hi = fromHexDigit(hex[i * 2u]);
        int lo = fromHexDigit(hex[i * 2u + 1u]);
        if (hi < 0 || lo < 0) {
            free(data);
            return -1;
        }
        data[i] = (uint8)((hi << 4) | lo);
    }
    freeMutableFile(&s_hallfame);
    strncpy(s_hallfame.name, "HallFame", sizeof(s_hallfame.name) - 1u);
    s_hallfame.name[sizeof(s_hallfame.name) - 1u] = '\0';
    s_hallfame.data = data;
    s_hallfame.size = (uint32)size;
    s_hallfame.recorded = 1;
    return 1;
}

int blackbox_state_parseReplayLine(const char *line) {
    char build[128];
    char name[32];
    char hex[8192];
    unsigned size;

    if (sscanf(line, "build_version %127s", build) == 1) {
        strncpy(s_replayBuildVersion, build, sizeof(s_replayBuildVersion) - 1u);
        s_replayBuildVersion[sizeof(s_replayBuildVersion) - 1u] = '\0';
        s_haveReplayBuildVersion = 1;
        return 1;
    }

    if (sscanf(line, "mutable_file %31s %u %8191s", name, &size, hex) == 3) {
        return parseMutableFile(name, size, hex);
    }

    return 0;
}

int blackbox_state_validateReplay(void) {
    if (!s_haveReplayBuildVersion) {
        log_error("blackbox: replay log has no build_version metadata");
        return 0;
    }
    if (strcmp(s_replayBuildVersion, s_currentBuildVersion) != 0) {
        if (!s_allowBuildMismatch) {
            log_error("blackbox: build mismatch: log=%s current=%s; use --blackbox-replay-ignore-build to inspect anyway",
                      s_replayBuildVersion, s_currentBuildVersion);
            return 0;
        }
        log_info("blackbox: ignoring build mismatch: log=%s current=%s",
                 s_replayBuildVersion, s_currentBuildVersion);
    }
    if (!s_hallfame.recorded) {
        log_error("blackbox: replay log has no HallFame snapshot");
        return 0;
    }
    return 1;
}

int blackbox_state_getReplayMutableFile(const char *name, const uint8 **data, uint32 *size) {
    if (!sameName(name, "HallFame") || !s_hallfame.recorded) return 0;
    *data = s_hallfame.data;
    *size = s_hallfame.size;
    return 1;
}
