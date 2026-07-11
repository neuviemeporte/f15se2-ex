/*
 * asound_sdl.c - SDL3 audio backend for the ported ASOUND AdLib driver.
 *
 * Replaces the DOS glue (asound_/asound_model/asdrv51.*): instead of busy-waited
 * writes to OPL ports 388h/389h, the decoded driver/asopl shadow state is
 * flushed into a Nuked-OPL3 (YM3812/OPL2) emulator and rendered to PCM on an SDL
 * audio thread. Digitized voice cues (F15DGTL.BIN) are mixed in as raw PCM.
 *
 * The game-facing audio_* slot ABI (slot.h) maps by name onto the model's
 * sound_driver_* entry points. The audio thread runs the sequencer at 60 Hz (the
 * game tick rate); a single mutex guards the driver/asopl state it shares with
 * the game thread.
 */

#include <SDL3/SDL.h>

#include "../shared/asset_compare.h"
#include "asound_model.h"
#include "asopl.h"
/* opl3.h has no extern "C" guard of its own; this TU compiles as C++ but opl3.c
 * is built as C, so the declarations must use C linkage to match. */
extern "C" {
#include "opl3.h"
}

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "inttype.h"
#include <dos.h>
#include "slot.h"
#include "common.h" /* openFile */
#include "input.h"  /* input_keyWaiting / input_setMode */
#include "log.h"

#define ASND_OUT_RATE 44100 /* SDL output sample rate (Hz) */
#define ASND_TICK_HZ 60     /* sequencer tick = game tick rate */
#define ASND_SAMPLE_HZ 7850 /* Recovered F15DGTL.BIN cue playback rate, shared with exported WAVs. */
#define ASND_CHUNK 512      /* max frames generated per inner loop pass */

static AsoplState g_opl; /* OPL register shadow (event -> regs) */
static opl3_chip g_chip; /* Nuked-OPL3 synthesis state */
static SDL_AudioStream *g_stream;
static SDL_Mutex *g_lock;
static AsoundU8 g_hwShadow[ASOPL_REGISTER_COUNT]; /* last value written to chip */
static bool g_ready;                              /* device opened */

/* Digitized-sample blob + active playback cursor (audio-thread-only state). */
static AsoundU8 *g_blob;
static int g_blobSize;
static bool g_smpActive;
static const AsoundU8 *g_smpData;
static int g_smpSize;
static double g_smpPos; /* fractional read position (bytes) */
static double g_smpStep = (double)ASND_SAMPLE_HZ / (double)ASND_OUT_RATE;

typedef struct ReplacementCue {
    AsoundU16 start;
    AsoundU16 endInclusive;
    const char *id;
    AsoundU8 *data;
    int size;
    int sampleRate;
} ReplacementCue;

static ReplacementCue g_replacementCues[] = {
    {0x0000u, 0x31f3u, "voice_cue_000_sample0", NULL, 0, 0},
    {0x31f4u, 0x4796u, "voice_cue_001_sample4", NULL, 0, 0},
    {0x4797u, 0x5c92u, "voice_cue_002_sample2_variant0", NULL, 0, 0},
    {0x5c93u, 0x6a1au, "voice_cue_003_sample2_variant1", NULL, 0, 0},
    {0x6a1bu, 0x7d9du, "voice_cue_004_sample2_variant2", NULL, 0, 0},
};

typedef struct ReplacementMusic {
    AsoundU8 *intro[ASOUND_STREAM_COUNT];
    AsoundU8 *release[ASOUND_STREAM_COUNT];
    int introSize[ASOUND_STREAM_COUNT];
    int releaseSize[ASOUND_STREAM_COUNT];
    int loaded;
    int tried;
} ReplacementMusic;

static ReplacementMusic g_replacementMusic;

/* Fractional countdown (in output frames) to the next sequencer tick. */
static double g_tickAccum;

static uint16_t rd16le(const AsoundU8 *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32le(const AsoundU8 *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static AsoundU8 *readEntireIo(SDL_IOStream *io, Sint64 *outSize) {
    Sint64 size;
    AsoundU8 *data;
    size_t got;

    if (outSize) *outSize = 0;
    if (!io) return NULL;
    size = SDL_GetIOSize(io);
    if (size <= 0) return NULL;

    data = (AsoundU8 *)SDL_malloc((size_t)size);
    if (!data) return NULL;
    got = SDL_ReadIO(io, data, (size_t)size);
    if (got != (size_t)size) {
        SDL_free(data);
        return NULL;
    }
    if (outSize) *outSize = size;
    return data;
}

static int decodePcm8Wav(const AsoundU8 *wav, Sint64 wavSize, AsoundU8 **outSamples, int *outSampleRate) {
    Sint64 pos;
    int fmtOk = 0;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint16_t audioFormat = 0;

    if (outSamples) *outSamples = NULL;
    if (outSampleRate) *outSampleRate = 0;
    if (!wav || wavSize < 44 || memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return 0;
    }

    pos = 12;
    while (pos + 8 <= wavSize) {
        const AsoundU8 *chunk = wav + pos;
        uint32_t chunkSize = rd32le(chunk + 4);
        Sint64 dataPos = pos + 8;
        Sint64 nextPos = dataPos + chunkSize + (chunkSize & 1);
        if (dataPos + chunkSize > wavSize) return 0;

        if (memcmp(chunk, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat = rd16le(wav + dataPos);
            channels = rd16le(wav + dataPos + 2);
            sampleRate = rd32le(wav + dataPos + 4);
            bitsPerSample = rd16le(wav + dataPos + 14);
            fmtOk = (audioFormat == 1 && channels == 1 && bitsPerSample == 8 && sampleRate > 0);
        } else if (memcmp(chunk, "data", 4) == 0) {
            AsoundU8 *samples;
            if (!fmtOk || chunkSize == 0) return 0;
            samples = (AsoundU8 *)SDL_malloc(chunkSize);
            if (!samples) return 0;
            memcpy(samples, wav + dataPos, chunkSize);
            if (outSamples) *outSamples = samples;
            if (outSampleRate) *outSampleRate = (int)sampleRate;
            return (int)chunkSize;
        }
        pos = nextPos;
    }
    return 0;
}

static AsoundU8 *loadLegacyDigitizedBlob(int *outSize) {
    SDL_IOStream *io = openFile("F15DGTL.BIN", 0);
    Sint64 size = 0;
    AsoundU8 *data;
    if (outSize) *outSize = 0;
    if (!io) return NULL;
    data = readEntireIo(io, &size);
    SDL_CloseIO(io);
    if (!data) return NULL;
    if (outSize) *outSize = (int)size;
    return data;
}

static ReplacementCue *findCueForRange(AsoundU16 start, AsoundU16 endInclusive) {
    int i;
    for (i = 0; i < (int)(sizeof(g_replacementCues) / sizeof(g_replacementCues[0])); i++) {
        if (g_replacementCues[i].start == start && g_replacementCues[i].endInclusive == endInclusive) {
            return &g_replacementCues[i];
        }
    }
    return NULL;
}

static int loadReplacementCueWavs(const AsoundU8 *legacyBlob, int legacyBlobSize) {
    int i;
    int loaded = 0;
    for (i = 0; i < (int)(sizeof(g_replacementCues) / sizeof(g_replacementCues[0])); i++) {
        ReplacementCue *cue = &g_replacementCues[i];
        char replacementPath[512];
        SDL_IOStream *wavIo;
        Sint64 wavSize = 0;
        AsoundU8 *wavData;
        AsoundU8 *samples = NULL;
        int sampleSize = 0;
        int sampleRate = 0;

        SDL_free(cue->data);
        cue->data = NULL;
        cue->size = 0;
        cue->sampleRate = 0;

        if (!findReplacementAssetPath(cue->id, ".wav", replacementPath, sizeof(replacementPath))) {
            continue;
        }

        wavIo = SDL_IOFromFile(replacementPath, "rb");
        wavData = readEntireIo(wavIo, &wavSize);
        if (wavIo) SDL_CloseIO(wavIo);
        if (wavData) {
            sampleSize = decodePcm8Wav(wavData, wavSize, &samples, &sampleRate);
            SDL_free(wavData);
        }
        if (!samples || sampleSize <= 0) {
            SDL_free(samples);
            LogWarn(("asset replacement: failed to decode non-empty cue WAV %s; legacy sample range remains fallback", replacementPath));
            continue;
        }

        assetCompareSoundCueRange(
            cue->id,
            cue->start,
            cue->endInclusive,
            ASND_SAMPLE_HZ,
            legacyBlob,
            legacyBlobSize > 0 ? (size_t)legacyBlobSize : 0u,
            samples,
            (size_t)sampleSize,
            sampleRate,
            replacementPath
        );
        cue->data = samples;
        cue->size = sampleSize;
        cue->sampleRate = sampleRate;
        loaded++;
        LogInfo((
            "asset replacement: loaded sound cue %s from %s (%d bytes, %d Hz)",
            cue->id,
            replacementPath,
            sampleSize,
            sampleRate
        ));
    }
    return loaded;
}

static const char *jsonFindAfter(const char *text, const char *needle) {
    return text && needle ? strstr(text, needle) : NULL;
}

static AsoundU8 *parseJsonStreamBytes(const char *json, const char *symbol, int *outCount) {
    char needle[96];
    const char *p;
    const char *array;
    AsoundU8 *bytes = NULL;
    int count = 0;
    int cap = 0;

    if (outCount) *outCount = 0;
    snprintf(needle, sizeof(needle), "\"source_symbol\": \"%s\"", symbol);
    p = jsonFindAfter(json, needle);
    if (!p) return NULL;
    array = jsonFindAfter(p, "\"stream_bytes\"");
    if (!array) return NULL;
    array = strchr(array, '[');
    if (!array) return NULL;
    array++;

    while (*array && *array != ']') {
        char *end = NULL;
        long value;
        while (*array == ' ' || *array == '\n' || *array == '\r' || *array == '\t' || *array == ',') array++;
        if (*array == ']') break;
        value = strtol(array, &end, 10);
        if (end == array || value < 0 || value > 255) {
            SDL_free(bytes);
            return NULL;
        }
        if (count >= cap) {
            int newCap = cap ? cap * 2 : 64;
            AsoundU8 *newBytes = (AsoundU8 *)SDL_realloc(bytes, (size_t)newCap);
            if (!newBytes) {
                SDL_free(bytes);
                return NULL;
            }
            bytes = newBytes;
            cap = newCap;
        }
        bytes[count++] = (AsoundU8)value;
        array = end;
    }
    if (count <= 0) {
        SDL_free(bytes);
        return NULL;
    }
    if (outCount) *outCount = count;
    return bytes;
}

static void freeReplacementMusic(void) {
    int phase, voice;
    AsoundU8 **sets[2] = {g_replacementMusic.intro, g_replacementMusic.release};
    for (phase = 0; phase < 2; phase++) {
        for (voice = 0; voice < ASOUND_STREAM_COUNT; voice++) {
            SDL_free(sets[phase][voice]);
            sets[phase][voice] = NULL;
            if (phase == 0) g_replacementMusic.introSize[voice] = 0;
            else g_replacementMusic.releaseSize[voice] = 0;
        }
    }
    g_replacementMusic.loaded = 0;
}

static int loadReplacementIntroMusic(void) {
    char replacementPath[512];
    SDL_IOStream *io;
    Sint64 jsonSize = 0;
    char *json;
    int voice;

    if (g_replacementMusic.tried) return g_replacementMusic.loaded;
    g_replacementMusic.tried = 1;

    if (!findReplacementAssetPath("sounds/intro_music", ".asound.json", replacementPath, sizeof(replacementPath))) {
        return 0;
    }

    io = SDL_IOFromFile(replacementPath, "rb");
    json = (char *)readEntireIo(io, &jsonSize);
    if (io) SDL_CloseIO(io);
    if (!json || jsonSize <= 0) {
        SDL_free(json);
        LogWarn(("asset replacement: failed to read intro music %s; using built-in ASOUND streams", replacementPath));
        return 0;
    }

    /* readEntireIo returns exact file bytes. Add a terminator for this
     * purpose-built parser; the JSON remains the authoritative music data. */
    {
        char *terminated = (char *)SDL_realloc(json, (size_t)jsonSize + 1u);
        if (!terminated) {
            SDL_free(json);
            return 0;
        }
        json = terminated;
        json[jsonSize] = '\0';
    }

    freeReplacementMusic();
    for (voice = 0; voice < ASOUND_STREAM_COUNT; voice++) {
        char symbol[64];
        snprintf(symbol, sizeof(symbol), "asound_intro_voice%d", voice);
        g_replacementMusic.intro[voice] = parseJsonStreamBytes(json, symbol, &g_replacementMusic.introSize[voice]);
        snprintf(symbol, sizeof(symbol), "asound_release_voice%d", voice);
        g_replacementMusic.release[voice] = parseJsonStreamBytes(json, symbol, &g_replacementMusic.releaseSize[voice]);
        if (!g_replacementMusic.intro[voice] || !g_replacementMusic.release[voice]) {
            freeReplacementMusic();
            SDL_free(json);
            LogWarn(("asset replacement: malformed intro music %s; using built-in ASOUND streams", replacementPath));
            return 0;
        }
    }
    SDL_free(json);
    g_replacementMusic.loaded = 1;
    LogInfo(("asset replacement: loaded intro music from %s", replacementPath));
    return 1;
}

static int startReplacementMusicPhase(int releasePhase) {
    AsoundDriver *drv;
    int voice;
    if (!loadReplacementIntroMusic()) return 0;
    drv = sound_driver_state();
    for (voice = 0; voice < ASOUND_STREAM_COUNT; voice++) {
        asound_stream_init(
            &drv->streams[voice],
            releasePhase ? g_replacementMusic.release[voice] : g_replacementMusic.intro[voice]
        );
    }
    return 1;
}
static const double g_samplesPerTick = (double)ASND_OUT_RATE / (double)ASND_TICK_HZ;

/* ---- OPL shadow -> chip --------------------------------------------------- */

static void asnd_invalidateHwShadow(void) {
    for (int r = 0; r < ASOPL_REGISTER_COUNT; r++) g_hwShadow[r] = 0xff;
}

/* Push every changed shadow register into the emulator (mirrors asdrv_sync_backend). */
static void asnd_syncChip(void) {
    for (int r = 0; r < ASOPL_REGISTER_COUNT; r++) {
        AsoundU8 v = asopl_get_register(&g_opl, (AsoundU8)r);
        if (g_hwShadow[r] == v) continue;
        OPL3_WriteReg(&g_chip, (uint16_t)r, v);
        g_hwShadow[r] = v;
    }
}

/* ---- sequencer tick (audio thread, under g_lock) -------------------------- */

static void asnd_startSample(AsoundU16 start, AsoundU16 end) {
    ReplacementCue *cue = findCueForRange(start, end);
    int s, e;
    if (cue && cue->data && cue->size > 0) {
        g_smpData = cue->data;
        g_smpSize = cue->size;
        g_smpPos = 0;
        /* Replacement WAVs are the editable source of truth, so their RIFF
         * sample rate controls playback speed instead of the legacy blob rate. */
        g_smpStep = (double)(cue->sampleRate > 0 ? cue->sampleRate : ASND_SAMPLE_HZ) / (double)ASND_OUT_RATE;
        g_smpActive = true;
        return;
    }

    if (!g_blob || g_blobSize <= 0) return;
    s = start;
    e = (int)end + 1;
    if (e > g_blobSize) e = g_blobSize;
    if (s < 0 || s >= e) return;
    g_smpData = g_blob + s;
    g_smpSize = e - s;
    g_smpPos = 0;
    g_smpStep = (double)ASND_SAMPLE_HZ / (double)ASND_OUT_RATE;
    g_smpActive = true;
}

/* One 60 Hz service tick: advance streams, apply events to the OPL shadow, run
 * the per-tick pitch-slide/noise/drone updates, then flush to the chip. This is
 * asdrv_timer + asdrv_noise from the DOS glue, minus the hardware I/O. */
static void asnd_tick(void) {
    AsoundDriver *drv = sound_driver_state();
    AsoundEvent events[32];
    size_t count = asound_driver_tick_events(drv, events, 32);
    for (size_t i = 0; i < count; i++) {
        if (events[i].type == ASOUND_EVENT_SAMPLE_RANGE) {
            asnd_startSample(events[i].a, events[i].b);
        } else {
            asopl_apply_event(&g_opl, drv, &events[i]);
        }
    }
    asopl_service_tick(&g_opl, drv);
    asopl_noise_tick(&g_opl);
    asnd_syncChip();
}

/* ---- PCM rendering (audio thread, lock-free) ------------------------------ */

static void asnd_mixSample(Sint16 *buf, int frames) {
    if (!g_smpActive) return;
    for (int i = 0; i < frames; i++) {
        int idx = (int)g_smpPos;
        if (idx >= g_smpSize) {
            g_smpActive = false;
            break;
        }
        /* 8-bit unsigned -> signed, scaled below full-scale to leave OPL headroom */
        int s = ((int)g_smpData[idx] - 128) * 200;
        int l = buf[2 * i] + s;
        int r = buf[2 * i + 1] + s;
        if (l > 32767)
            l = 32767;
        else if (l < -32768)
            l = -32768;
        if (r > 32767)
            r = 32767;
        else if (r < -32768)
            r = -32768;
        buf[2 * i] = (Sint16)l;
        buf[2 * i + 1] = (Sint16)r;
        g_smpPos += g_smpStep;
    }
}

static void SDLCALL asnd_callback(void *user, SDL_AudioStream *stream,
                                  int additional, int total) {
    (void)user;
    (void)total;
    Sint16 buf[ASND_CHUNK * 2];
    int framesNeeded = additional / (int)sizeof(Sint16) / 2;

    while (framesNeeded > 0) {
        /* The OPL chip and driver state are shared with the game thread (which
         * calls OPL3_Reset/register writes via audio_setup/shutdown), so the
         * tick + generation must be serialized with it. Lock per chunk (<=512
         * frames) so game-thread audio_* calls only ever block briefly. */
        SDL_LockMutex(g_lock);
        while (g_tickAccum < 1.0) {
            asnd_tick();
            g_tickAccum += g_samplesPerTick;
        }
        int chunk = framesNeeded;
        if (chunk > (int)g_tickAccum) chunk = (int)g_tickAccum;
        if (chunk > ASND_CHUNK) chunk = ASND_CHUNK;

        OPL3_GenerateStream(&g_chip, buf, (uint32_t)chunk);
        asnd_mixSample(buf, chunk);
        g_tickAccum -= chunk;
        SDL_UnlockMutex(g_lock);

        SDL_PutAudioStreamData(stream, buf, chunk * 2 * (int)sizeof(Sint16));
        framesNeeded -= chunk;
    }
}

/* ---- device lifecycle ----------------------------------------------------- */

static void asnd_openDevice(void) {
    if (g_ready) return;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        LogError(("asound: SDL_INIT_AUDIO failed: %s", SDL_GetError()));
        return;
    }
    g_lock = SDL_CreateMutex();
    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = ASND_OUT_RATE;
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                         asnd_callback, NULL);
    if (!g_stream) {
        LogError(("asound: SDL_OpenAudioDeviceStream failed: %s", SDL_GetError()));
        SDL_DestroyMutex(g_lock);
        g_lock = NULL;
        return;
    }
    /* Make the synth valid before the callback can fire: a zeroed opl3_chip has
     * rateratio 0 and OPL3_GenerateStream would divide by zero. */
    asopl_init(&g_opl);
    OPL3_Reset(&g_chip, ASND_OUT_RATE);
    asnd_invalidateHwShadow();
    g_tickAccum = g_samplesPerTick;
    SDL_ResumeAudioStreamDevice(g_stream);
    g_ready = true;
}

/* Reset the synth to silence (used by setup and shutdown). Caller holds g_lock
 * if the device is already running. */
static void asnd_resetSynth(AsoundU16 setupValue) {
    sound_driver_setup(setupValue, /*driver_segment=*/1); /* nonzero: enables intro */
    asopl_init(&g_opl);
    asopl_set_drone_pitch(&g_opl, 0);
    asopl_set_drone_enable(&g_opl, 0);
    OPL3_Reset(&g_chip, ASND_OUT_RATE);
    asnd_invalidateHwShadow();
    asnd_syncChip();
    g_smpActive = false;
}

/* ---- game-facing slot ABI (slot.h) --------------------------------------- */

int FAR CDECL audio_setup(int16 sampleDataSeg, int16 variantSel) {
    (void)sampleDataSeg; /* segments are meaningless natively; blob is loaded by loadF15DgtlBin */
    asnd_openDevice();
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    asnd_resetSynth((AsoundU16)variantSel);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_shutdown(void) {
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    sound_driver_shutdown();
    asopl_reset(&g_opl);
    OPL3_Reset(&g_chip, ASND_OUT_RATE);
    asnd_invalidateHwShadow();
    asnd_syncChip();
    g_smpActive = false;
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_playSound(int soundId) {
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    int r = sound_driver_dispatch_sound((AsoundU16)soundId);
    SDL_UnlockMutex(g_lock);
    return r;
}

/* Voices 0-2 carry the intro melody; all three idle (stream_ptr cleared) once a
 * set of intro/release streams has played out. */
static bool asnd_introVoicesActive(void) {
    AsoundDriver *drv = sound_driver_state();
    bool active;
    SDL_LockMutex(g_lock);
    active = drv->streams[0].stream_ptr || drv->streams[1].stream_ptr ||
             drv->streams[2].stream_ptr;
    SDL_UnlockMutex(g_lock);
    return active;
}

/* Faithful port of adlib_play_intro_until_key: play the title jingle, then peek
 * the keyboard without consuming it (so the caller's getKey still sees the
 * press), play the short release tail, and silence the chip. Blocking, like the
 * original - the title screen is a blocking wait regardless. Deadlines are
 * defensive caps so a stuck stream can never hang the title. */
int FAR CDECL audio_playIntro(void) {
    asnd_openDevice();
    if (!g_ready) {
        LogWarn(("asound: intro music skipped because no audio device is ready"));
        return 0;
    }

    LogInfo(("asound: playing intro music"));
    SDL_LockMutex(g_lock);
    if (!startReplacementMusicPhase(0)) {
        sound_driver_play_intro();
    }
    SDL_UnlockMutex(g_lock);

    input_setMode(INPUT_MODE_MENU);
    Uint64 deadline = SDL_GetTicksNS() + 15 * SDL_NS_PER_SECOND;
    while (asnd_introVoicesActive() && !input_keyWaiting() &&
           SDL_GetTicksNS() < deadline) {
        SDL_DelayNS(2 * SDL_NS_PER_MS); /* input_keyWaiting() pumps the clock */
    }

    /* release tail (adlib_start_intro_release), then let it decay out */
    SDL_LockMutex(g_lock);
    if (!startReplacementMusicPhase(1)) {
        asound_driver_start_intro_rel(sound_driver_state());
    }
    SDL_UnlockMutex(g_lock);
    deadline = SDL_GetTicksNS() + 2 * SDL_NS_PER_SECOND;
    while (asnd_introVoicesActive() && SDL_GetTicksNS() < deadline) {
        SDL_DelayNS(2 * SDL_NS_PER_MS);
        input_keyWaiting();
    }

    /* silence the chip (adlib_reset_state) */
    SDL_LockMutex(g_lock);
    asopl_reset(&g_opl);
    OPL3_Reset(&g_chip, ASND_OUT_RATE);
    asnd_invalidateHwShadow();
    asnd_syncChip();
    g_smpActive = false;
    SDL_UnlockMutex(g_lock);
    LogInfo(("asound: intro music finished"));
    return 0;
}

int FAR CDECL audio_engineDroneOn(void) {
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    sound_driver_enable_drone();
    asopl_set_drone_enable(&g_opl, 1);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_engineDroneOff(void) {
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    sound_driver_disable_drone();
    asopl_set_drone_enable(&g_opl, 0);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_setEnginePitch(int knots, int thrust) {
    (void)thrust; /* the original ASOUND set_drone_pitch slot used only the first arg */
    if (!g_ready) return 0;
    AsoundU16 pitch = (knots < 0) ? 0 : (knots > 0x02bc ? 0x02bc : (AsoundU16)knots);
    SDL_LockMutex(g_lock);
    sound_driver_set_drone_pitch(pitch);
    asopl_set_drone_pitch(&g_opl, pitch);
    SDL_UnlockMutex(g_lock);
    return 0;
}

int FAR CDECL audio_playSample(int sampleIdx) {
    if (!g_ready) return 0;
    SDL_LockMutex(g_lock);
    int r = sound_driver_play_sample((AsoundU16)sampleIdx);
    SDL_UnlockMutex(g_lock);
    return r;
}

/* The sequencer is driven from the audio callback, so the game's per-tick audio
 * hooks (originally the PIT IRQ path) are no-ops here. */
int FAR CDECL audio_timerTick(void) { return 0; }
int FAR CDECL audio_noiseTick(void) { return 0; }

/* ---- digitized sample blob ----------------------------------------------- */

/* Load F15DGTL.BIN (the digitized voice/effect blob) and any modern cue WAVs.
 * The legacy blob size is still returned because game code stores it in
 * f15DgtlResult and passes it to audio_setup as the sample-variant selector.
 * If the blob is missing but cue WAVs exist, return the known ASOUND span so
 * those modern replacements can play without requiring JSON sidecars. */
int loadF15DgtlBin(void) {
    AsoundU8 *data;
    int size = 0;
    int replacementCueCount;

    data = loadLegacyDigitizedBlob(&size);
    replacementCueCount = loadReplacementCueWavs(data, size);
    if ((!data || size <= 0) && replacementCueCount <= 0) {
        LogError(("asound: cannot open F15DGTL.BIN or replacement cue WAVs"));
        return 0;
    }

    if (g_lock) SDL_LockMutex(g_lock);
    SDL_free(g_blob);
    g_blob = data;
    g_blobSize = size;
    if (g_lock) SDL_UnlockMutex(g_lock);

    if (size > 0) return size;
    return 0x7d9e;
}

#ifdef DEBUG
int asound_testCompareReplacementCues(void) {
    AsoundU8 *legacyBlob = NULL;
    int legacySize = 0;
    int loaded;
    int i;

    legacyBlob = loadLegacyDigitizedBlob(&legacySize);
    if (!legacyBlob || legacySize <= 0) {
        SDL_free(legacyBlob);
        return 0;
    }
    loaded = loadReplacementCueWavs(legacyBlob, legacySize);
    if (loaded != (int)(sizeof(g_replacementCues) / sizeof(g_replacementCues[0]))) {
        SDL_free(legacyBlob);
        return 0;
    }
    for (i = 0; i < (int)(sizeof(g_replacementCues) / sizeof(g_replacementCues[0])); i++) {
        ReplacementCue *cue = &g_replacementCues[i];
        int start = cue->start;
        int end = (int)cue->endInclusive + 1;
        int expectedSize = end - start;
        if (!cue->data || cue->sampleRate != ASND_SAMPLE_HZ ||
            cue->size != expectedSize || start < 0 || end > legacySize ||
            memcmp(cue->data, legacyBlob + start, (size_t)expectedSize) != 0) {
            SDL_free(legacyBlob);
            return 0;
        }
    }
    SDL_free(legacyBlob);
    return 1;
}

int asound_testCompareReplacementIntroMusic(void) {
    static const AsoundU8 *const legacyIntro[ASOUND_STREAM_COUNT] = {
        asound_intro_voice0, asound_intro_voice1, asound_intro_voice2,
        asound_intro_voice3, asound_intro_voice4, asound_intro_voice5
    };
    static const AsoundU8 *const legacyRelease[ASOUND_STREAM_COUNT] = {
        asound_release_voice0, asound_release_voice1, asound_release_voice2,
        asound_release_voice3, asound_release_voice4, asound_release_voice5
    };
    int voice;

    freeReplacementMusic();
    g_replacementMusic.tried = 0;
    if (!loadReplacementIntroMusic()) return 0;
    for (voice = 0; voice < ASOUND_STREAM_COUNT; voice++) {
        size_t introLen = asound_intro_voice_length(voice);
        size_t releaseLen = asound_release_voice_length(voice);
        if (introLen == 0 || releaseLen == 0) return 0;
        if (!g_replacementMusic.intro[voice] || !g_replacementMusic.release[voice]) return 0;
        if (g_replacementMusic.introSize[voice] != (int)introLen) return 0;
        if (g_replacementMusic.releaseSize[voice] != (int)releaseLen) return 0;
        if (memcmp(g_replacementMusic.intro[voice], legacyIntro[voice], introLen) != 0) return 0;
        if (memcmp(g_replacementMusic.release[voice], legacyRelease[voice], releaseLen) != 0) return 0;
    }
    return 1;
}
#endif
