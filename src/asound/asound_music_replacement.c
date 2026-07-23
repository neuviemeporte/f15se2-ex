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
    const char *object = json;
    const size_t symbol_length = strlen(symbol);

    /*
     * Match source_symbol inside one flat stream object. Keeping the search
     * within its closing brace prevents malformed JSON from borrowing the
     * stream_bytes array from the following voice.
     */
    while ((object = strchr(object, '{')) != NULL) {
        const char *end = strchr(object, '}');
        const char *key = strstr(object, "\"source_symbol\"");
        if (!end) return NULL;
        if (key && key < end) {
            const char *value = strchr(key, ':');
            if (value && value < end) {
                do {
                    ++value;
                } while (value < end && isspace((unsigned char)*value));
                if (value < end && *value++ == '"'
                    && (size_t)(end - value) > symbol_length
                    && memcmp(value, symbol, symbol_length) == 0
                    && value[symbol_length] == '"') {
                    *object_end = end;
                    return object;
                }
            }
        }
        object = end + 1;
    }
    return NULL;
}

static AsoundU8 *parseStream(const char *json, const char *symbol) {
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
        g_music.intro[voice] = parseStream(json, symbol);
        snprintf(symbol, sizeof(symbol), "asound_release_voice%d", voice);
        g_music.release[voice] = parseStream(json, symbol);
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
