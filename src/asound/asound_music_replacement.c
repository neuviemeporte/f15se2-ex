/*
 * asound_music_replacement.c - Load ASOUND byte streams from converter JSON.
 */

#include "asound_music_replacement.h"

#include "../shared/asset_path.h"
#include "../log.h"

#include <SDL3/SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ReplacementMusic {
    AsoundU8 *intro[ASOUND_STREAM_COUNT];
    AsoundU8 *release[ASOUND_STREAM_COUNT];
    size_t intro_size[ASOUND_STREAM_COUNT];
    size_t release_size[ASOUND_STREAM_COUNT];
    int loaded;
    int tried;
} ReplacementMusic;

static ReplacementMusic g_music;

static void freeStreams(void) {
    for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
        SDL_free(g_music.intro[voice]);
        SDL_free(g_music.release[voice]);
        g_music.intro[voice] = NULL;
        g_music.release[voice] = NULL;
        g_music.intro_size[voice] = 0;
        g_music.release_size[voice] = 0;
    }
    g_music.loaded = 0;
}

static char *readTextFile(const char *path) {
    SDL_IOStream *stream = SDL_IOFromFile(path, "rb");
    if (!stream) return NULL;
    const Sint64 length = SDL_GetIOSize(stream);
    if (length <= 0 || (uint64_t)length >= SIZE_MAX) {
        SDL_CloseIO(stream);
        return NULL;
    }
    char *text = (char *)SDL_malloc((size_t)length + 1);
    const size_t read = text ? SDL_ReadIO(stream, text, (size_t)length) : 0;
    SDL_CloseIO(stream);
    if (read != (size_t)length) {
        SDL_free(text);
        return NULL;
    }
    text[length] = '\0';
    return text;
}

static const char *findStreamObject(const char *json, const char *symbol,
                                    const char **object_end) {
    const char *key = json;
    const size_t symbol_length = strlen(symbol);

    /*
     * Converter output includes nested event objects before source_symbol.
     * Locate the exact symbol first, then recover its nearest still-open
     * object and matching close. This keeps stream_bytes bounded to one voice
     * without assuming that voice objects are flat.
     */
    while ((key = strstr(key, "\"source_symbol\"")) != NULL) {
        const char *value = strchr(key, ':');
        if (!value) return NULL;
        do {
            ++value;
        } while (*value && isspace((unsigned char)*value));
        if (*value++ == '"' && !memcmp(value, symbol, symbol_length)
            && value[symbol_length] == '"') {
            const char *object = key;
            int reverse_depth = 0;
            while (object > json) {
                --object;
                if (*object == '}') {
                    ++reverse_depth;
                } else if (*object == '{') {
                    if (reverse_depth == 0) break;
                    --reverse_depth;
                }
            }
            if (*object != '{') return NULL;

            int depth = 0;
            int quoted = 0;
            int escaped = 0;
            for (const char *end = object; *end; ++end) {
                if (quoted) {
                    if (escaped) escaped = 0;
                    else if (*end == '\\') escaped = 1;
                    else if (*end == '"') quoted = 0;
                    continue;
                }
                if (*end == '"') quoted = 1;
                else if (*end == '{') ++depth;
                else if (*end == '}' && --depth == 0) {
                    *object_end = end;
                    return object;
                }
            }
            return NULL;
        }
        ++key;
    }
    return NULL;
}

static AsoundU8 *parseStream(const char *json, const char *symbol,
                             size_t *stream_size) {
    const char *object_end = NULL;
    const char *position = findStreamObject(json, symbol, &object_end);
    if (!position) return NULL;
    position = strstr(position, "\"stream_bytes\"");
    if (!position || position >= object_end) return NULL;
    position = strchr(position, '[');
    if (!position || position >= object_end) return NULL;
    ++position;

    AsoundU8 *bytes = NULL;
    size_t count = 0;
    size_t capacity = 0;
    while (position < object_end && *position != ']') {
        while (isspace((unsigned char)*position) || *position == ',') ++position;
        if (position >= object_end || *position == ']') break;

        char *end = NULL;
        const long value = strtol(position, &end, 10);
        if (end == position || value < 0 || value > 255) {
            SDL_free(bytes);
            return NULL;
        }
        if (count == capacity) {
            const size_t next = capacity ? capacity * 2 : 32;
            AsoundU8 *grown = (AsoundU8 *)SDL_realloc(bytes, next);
            if (!grown) {
                SDL_free(bytes);
                return NULL;
            }
            bytes = grown;
            capacity = next;
        }
        bytes[count++] = (AsoundU8)value;
        position = end;
    }
    if (position >= object_end || *position != ']' || !count) {
        SDL_free(bytes);
        return NULL;
    }
    if (stream_size) *stream_size = count;
    return bytes;
}

int asound_reload_replacement_music(void) {
    char path[1024];
    /*
     * Playback stream pointers refer directly to these allocations. Runtime
     * code loads once before playback; explicit reload exists for tests and
     * must only be called while no replacement music is active.
     */
    freeStreams();
    g_music.tried = 1;
    if (!findAssetReplacement("sounds/intro_music.asound.json",
                              path, sizeof(path))) {
        return 0;
    }

    char *json = readTextFile(path);
    if (!json) {
        LogWarn(("asset replacement: cannot read intro music %s; "
                 "using compiled streams", path));
        return 0;
    }

    for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
        char symbol[64];
        snprintf(symbol, sizeof(symbol), "asound_intro_voice%d", voice);
        g_music.intro[voice] =
            parseStream(json, symbol, &g_music.intro_size[voice]);
        snprintf(symbol, sizeof(symbol), "asound_release_voice%d", voice);
        g_music.release[voice] =
            parseStream(json, symbol, &g_music.release_size[voice]);
        if (!g_music.intro[voice] || !g_music.release[voice]) {
            SDL_free(json);
            freeStreams();
            LogWarn(("asset replacement: incomplete intro music %s; "
                     "using compiled streams", path));
            return 0;
        }
    }
    SDL_free(json);
    g_music.loaded = 1;
    LogInfo(("asset replacement: loaded intro music %s", path));
    return 1;
}

int asound_start_replacement_music(AsoundDriver *driver, int release_phase) {
    if (!driver) return 0;
    if (!g_music.tried) asound_reload_replacement_music();
    if (!g_music.loaded) return 0;

    AsoundU8 **streams = release_phase ? g_music.release : g_music.intro;
    for (int voice = 0; voice < ASOUND_STREAM_COUNT; ++voice) {
        asound_stream_init(&driver->streams[voice], streams[voice]);
    }
    return 1;
}

int asound_replacement_music_stream(int release_phase, int voice,
                                    const AsoundU8 **data, size_t *size) {
    if (data) *data = NULL;
    if (size) *size = 0;
    if (!g_music.loaded || voice < 0 || voice >= ASOUND_STREAM_COUNT) return 0;
    if (data) {
        *data = release_phase ? g_music.release[voice] : g_music.intro[voice];
    }
    if (size) {
        *size = release_phase
            ? g_music.release_size[voice] : g_music.intro_size[voice];
    }
    return 1;
}
