/*
 * asound_replacements.c - Load individual unsigned 8-bit mono PCM cue WAVs.
 */

#include "asound_replacements.h"

#include "../shared/asset_path.h"
#include "../log.h"

#include <SDL3/SDL.h>

#include <limits.h>
#include <stdint.h>
#include <string.h>

typedef struct CueSlot {
    AsoundU16 start;
    AsoundU16 end_inclusive;
    const char *filename;
    AsoundU8 *samples;
    int sample_count;
    int sample_rate;
} CueSlot;

static CueSlot g_cues[] = {
    {0x0000u, 0x31f3u, "sounds/voice_cue_000_sample0.wav", NULL, 0, 0},
    {0x31f4u, 0x4796u, "sounds/voice_cue_001_sample4.wav", NULL, 0, 0},
    {0x4797u, 0x5c92u, "sounds/voice_cue_002_sample2_variant0.wav", NULL, 0, 0},
    {0x5c93u, 0x6a1au, "sounds/voice_cue_003_sample2_variant1.wav", NULL, 0, 0},
    {0x6a1bu, 0x7d9du, "sounds/voice_cue_004_sample2_variant2.wav", NULL, 0, 0},
};

/* Read an unsigned 16-bit little-endian value from an unaligned byte buffer. */
static uint16_t read16le(const AsoundU8 *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

/* Read an unsigned 32-bit little-endian value from an unaligned byte buffer. */
static uint32_t read32le(const AsoundU8 *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

/* Read an entire replacement file into an owned heap buffer. */
static AsoundU8 *readFile(const char *path, size_t *size) {
    SDL_IOStream *stream = SDL_IOFromFile(path, "rb");
    if (size) *size = 0;
    if (!stream) return NULL;

    const Sint64 length = SDL_GetIOSize(stream);
    if (length <= 0 || (uint64_t)length > SIZE_MAX) {
        SDL_CloseIO(stream);
        return NULL;
    }
    AsoundU8 *data = (AsoundU8 *)SDL_malloc((size_t)length);
    const size_t read = data ? SDL_ReadIO(stream, data, (size_t)length) : 0;
    SDL_CloseIO(stream);
    if (read != (size_t)length) {
        SDL_free(data);
        return NULL;
    }
    if (size) *size = read;
    return data;
}

/* Decode one unsigned 8-bit mono PCM WAV while rejecting unsupported layouts. */
static int decodePcm8MonoWav(const AsoundU8 *wav, size_t wav_size,
                             AsoundU8 **samples, int *sample_rate) {
    bool format_valid = false;
    uint32_t rate = 0;
    if (samples) *samples = NULL;
    if (sample_rate) *sample_rate = 0;
    if (!wav || wav_size < 12 || memcmp(wav, "RIFF", 4)
        || memcmp(wav + 8, "WAVE", 4)) {
        return 0;
    }

    for (size_t offset = 12; offset + 8 <= wav_size;) {
        const AsoundU8 *chunk = wav + offset;
        const uint32_t chunk_size = read32le(chunk + 4);
        const size_t data_offset = offset + 8;
        if (chunk_size > wav_size - data_offset) return 0;

        if (!memcmp(chunk, "fmt ", 4) && chunk_size >= 16) {
            const uint16_t format = read16le(wav + data_offset);
            const uint16_t channels = read16le(wav + data_offset + 2);
            rate = read32le(wav + data_offset + 4);
            const uint16_t bits = read16le(wav + data_offset + 14);
            format_valid = format == 1 && channels == 1 && bits == 8
                && rate > 0 && rate <= INT_MAX;
        } else if (!memcmp(chunk, "data", 4)) {
            if (!format_valid || !chunk_size || chunk_size > INT32_MAX) return 0;
            AsoundU8 *decoded = (AsoundU8 *)SDL_malloc(chunk_size);
            if (!decoded) return 0;
            memcpy(decoded, wav + data_offset, chunk_size);
            if (samples) *samples = decoded;
            if (sample_rate) *sample_rate = (int)rate;
            return (int)chunk_size;
        }

        const size_t padded_size = (size_t)chunk_size + (chunk_size & 1u);
        if (padded_size > wav_size - data_offset) return 0;
        offset = data_offset + padded_size;
    }
    return 0;
}

/* Load independently editable WAV cues and replace only slots that are present. */
int asound_load_replacement_cues(void) {
    int loaded = 0;
    for (CueSlot &slot : g_cues) {
        SDL_free(slot.samples);
        slot.samples = NULL;
        slot.sample_count = 0;
        slot.sample_rate = 0;

        char path[1024]{};
        if (!findAssetReplacement(slot.filename, path, sizeof(path))) continue;

        size_t wav_size = 0;
        AsoundU8 *wav = readFile(path, &wav_size);
        if (wav) {
            slot.sample_count =
                decodePcm8MonoWav(wav, wav_size, &slot.samples, &slot.sample_rate);
            SDL_free(wav);
        }
        if (!slot.samples || slot.sample_count <= 0) {
            SDL_free(slot.samples);
            slot.samples = NULL;
            slot.sample_count = 0;
            slot.sample_rate = 0;
            LogWarn(("asset replacement: invalid PCM8 mono cue WAV %s; "
                     "using legacy cue", path));
            continue;
        }
        ++loaded;
        LogInfo(("asset replacement: loaded cue WAV %s (%d samples, %d Hz)",
                 path, slot.sample_count, slot.sample_rate));
    }
    return loaded;
}

/* Return one past the highest legacy byte offset covered by a loaded cue. */
int asound_replacement_logical_span(void) {
    int logicalSpan = 0;
    for (const CueSlot &slot : g_cues) {
        if (!slot.samples || slot.sample_count <= 0) continue;
        const int slotSpan = (int)slot.end_inclusive + 1;
        if (slotSpan > logicalSpan) logicalSpan = slotSpan;
    }
    return logicalSpan;
}

/* Return the replacement cue for a legacy sample pointer, or NULL when not overridden. */
int asound_find_replacement_cue(AsoundU16 start, AsoundU16 end_inclusive,
                                AsoundReplacementCue *cue) {
    if (cue) {
        cue->samples = NULL;
        cue->sample_count = 0;
        cue->sample_rate = 0;
    }
    for (const CueSlot &slot : g_cues) {
        if (slot.start == start && slot.end_inclusive == end_inclusive
            && slot.samples && slot.sample_count > 0) {
            if (cue) {
                cue->samples = slot.samples;
                cue->sample_count = slot.sample_count;
                cue->sample_rate = slot.sample_rate;
            }
            return 1;
        }
    }
    return 0;
}

/* Return the number of currently loaded replacement sound cues. */
int asound_replacement_cue_count(void) {
    return (int)(sizeof(g_cues) / sizeof(g_cues[0]));
}

/* Return replacement cue metadata by stable enumeration index. */
int asound_replacement_cue_at(int index, AsoundU16 *start,
                              AsoundU16 *end_inclusive,
                              AsoundReplacementCue *cue) {
    if (index < 0 || index >= asound_replacement_cue_count()) return 0;
    CueSlot *slot = &g_cues[index];
    if (!slot->samples || slot->sample_count <= 0) return 0;
    if (start) *start = slot->start;
    if (end_inclusive) *end_inclusive = slot->end_inclusive;
    if (cue) {
        cue->samples = slot->samples;
        cue->sample_count = slot->sample_count;
        cue->sample_rate = slot->sample_rate;
    }
    return 1;
}
