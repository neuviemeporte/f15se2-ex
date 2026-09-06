/*
 * Deterministic blackbox recorder/replayer.
 *
 * The log is intentionally text: a developer or future agent can inspect a
 * problematic timeframe with normal tools, then replay the exact input/RNG
 * stream. Ticks are the game's deterministic 60 Hz timer ticks, not wall-clock
 * time. Keys additionally carry an input-pump ordinal because some menu phases
 * stop the timer while continuing to poll and clear the BIOS-style key buffer.
 */
#include "blackbox.h"
#include "blackbox_diag.h"
#include "blackbox_internal.h"
#include "blackbox_state.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core-only tests link blackbox.c without the flight globals that diagnostics
 * inspect. Weak lifecycle hooks keep that useful isolation; the game itself
 * pulls blackbox_diag.c through its input, simulation, and r3d call sites. */
#if defined(__GNUC__) || defined(__clang__)
#ifdef __cplusplus
extern "C" {
#endif
void blackbox_diagReset(void) __attribute__((weak));
int blackbox_diagParseReplayLine(const char *line) __attribute__((weak));
int blackbox_diagValidateReplay(void) __attribute__((weak));
void blackbox_diagShutdown(int pausedForInspection) __attribute__((weak));
void blackbox_diagOnTick(void) __attribute__((weak));
#ifdef __cplusplus
}
#endif
#endif

enum {
    BLACKBOX_FILE_VERSION = 7,
    BLACKBOX_TIMER_HZ = 60,
    BLACKBOX_RAND_MASK = 0x7fff,
    BLACKBOX_MAX_KEY_WORD = 0xffff,
    BLACKBOX_MAX_AXIS_VALUE = 0xff,
    /* timer.c resynchronizes after 250 ms. At 60 Hz, a valid call can emit at
     * most the due tick plus fifteen catch-up ticks. */
    BLACKBOX_MAX_TIMER_PUMP_TICKS = 16,
    BLACKBOX_DEBUG_BG = 0,
    BLACKBOX_DEBUG_FG = 15
};

typedef struct BlackboxKeyEvent {
    uint32 tick;
    uint32 inputPump;
    uint16 word;
} BlackboxKeyEvent;

typedef struct BlackboxAxesEvent {
    uint32 tick;
    uint8 rawX;
    uint8 rawY;
    uint8 joyX;
    uint8 joyY;
} BlackboxAxesEvent;

typedef struct BlackboxRandEvent {
    uint32 tick;
    int value;
} BlackboxRandEvent;

typedef struct BlackboxSeedEvent {
    uint32 tick;
    uint32 seed;
} BlackboxSeedEvent;

typedef struct BlackboxFrameEvent {
    uint32 tick;
    uint32 frame;
    uint32 hash;
} BlackboxFrameEvent;

typedef struct BlackboxTimerPumpEvent {
    uint32 startTick;
    uint32 tickCount;
} BlackboxTimerPumpEvent;

static BlackboxMode s_mode = BLACKBOX_OFF;
static FILE *s_file = NULL;
static uint32 s_tick = 0;
static uint32 s_inputPump = 0;
static uint32 s_pauseTick = 0xffffffffu;
static uint32 s_fastForwardTick = 0xffffffffu;
static uint32 s_seed = BLACKBOX_DEFAULT_SEED;
static uint32 s_rngState = BLACKBOX_DEFAULT_SEED;

static BlackboxKeyEvent *s_keys = NULL;
static size_t s_keyCount = 0;
static size_t s_keyCapacity = 0;
static size_t s_keyPos = 0;
static BlackboxAxesEvent *s_axes = NULL;
static size_t s_axesCount = 0;
static size_t s_axesCapacity = 0;
static size_t s_axesPos = 0;
static BlackboxRandEvent *s_randEvents = NULL;
static size_t s_randCount = 0;
static size_t s_randCapacity = 0;
static size_t s_randPos = 0;
static BlackboxSeedEvent *s_seedEvents = NULL;
static size_t s_seedCount = 0;
static size_t s_seedCapacity = 0;
static size_t s_seedPos = 0;
static BlackboxFrameEvent *s_frames = NULL;
static size_t s_frameCount = 0;
static size_t s_frameCapacity = 0;
static size_t s_framePos = 0;
static uint32 s_frameIndex = 0;
static BlackboxTimerPumpEvent *s_timerPumps = NULL;
static size_t s_timerPumpCount = 0;
static size_t s_timerPumpCapacity = 0;
static size_t s_timerPumpPos = 0;
static int s_reportedTimerPumpDivergence = 0;
static int s_reportedSeedDivergence = 0;
static int s_reportedRandDivergence = 0;
static int s_reportedFrameDivergence = 0;
static unsigned s_passivePresentDepth = 0;
static SDL_Surface *s_overlayPage = NULL;
static uint8 s_overlayBackup[7][52];
static int s_overlayWidth = 0;
static int s_overlayHeight = 0;
static BlackboxAxesEvent s_currentAxes = {0, 0x80, 0x80, 0x80, 0x80};
static int s_haveRecordedAxes = 0;
static BlackboxAxesEvent s_lastRecordedAxes = {0, 0x80, 0x80, 0x80, 0x80};

static int reserveEvents(void **events, size_t *capacity, size_t count,
                         size_t eventSize) {
    size_t newCapacity;
    void *next;
    if (count < *capacity) return 1;
    newCapacity = *capacity ? *capacity * 2u : 64u;
    if (newCapacity <= *capacity || newCapacity > SIZE_MAX / eventSize) return 0;
    next = realloc(*events, newCapacity * eventSize);
    if (!next) return 0;
    *events = next;
    *capacity = newCapacity;
    return 1;
}

static void blackbox_resetReplayData(void) {
    free(s_keys);
    free(s_axes);
    free(s_randEvents);
    free(s_seedEvents);
    free(s_frames);
    free(s_timerPumps);
    s_keys = NULL;
    s_axes = NULL;
    s_randEvents = NULL;
    s_seedEvents = NULL;
    s_frames = NULL;
    s_timerPumps = NULL;
    s_keyCount = 0;
    s_keyCapacity = 0;
    s_keyPos = 0;
    s_axesCount = 0;
    s_axesCapacity = 0;
    s_axesPos = 0;
    s_randCount = 0;
    s_randCapacity = 0;
    s_randPos = 0;
    s_seedCount = 0;
    s_seedCapacity = 0;
    s_seedPos = 0;
    s_frameCount = 0;
    s_frameCapacity = 0;
    s_framePos = 0;
    s_frameIndex = 0;
    s_timerPumpCount = 0;
    s_timerPumpCapacity = 0;
    s_timerPumpPos = 0;
    s_currentAxes.tick = 0;
    s_currentAxes.rawX = 0x80;
    s_currentAxes.rawY = 0x80;
    s_currentAxes.joyX = 0x80;
    s_currentAxes.joyY = 0x80;
}

static int blackbox_appendSeed(uint32 tick, uint32 seed) {
    if (!reserveEvents((void **)&s_seedEvents, &s_seedCapacity,
                       s_seedCount, sizeof(*s_seedEvents))) return 0;
    s_seedEvents[s_seedCount].tick = tick;
    s_seedEvents[s_seedCount].seed = seed;
    s_seedCount++;
    return 1;
}

static int blackbox_appendFrame(uint32 tick, uint32 frame, uint32 hash) {
    if (!reserveEvents((void **)&s_frames, &s_frameCapacity,
                       s_frameCount, sizeof(*s_frames))) return 0;
    s_frames[s_frameCount].tick = tick;
    s_frames[s_frameCount].frame = frame;
    s_frames[s_frameCount].hash = hash;
    s_frameCount++;
    return 1;
}

static int blackbox_appendRand(uint32 tick, int value) {
    if (!reserveEvents((void **)&s_randEvents, &s_randCapacity,
                       s_randCount, sizeof(*s_randEvents))) return 0;
    s_randEvents[s_randCount].tick = tick;
    s_randEvents[s_randCount].value = value;
    s_randCount++;
    return 1;
}

static int blackbox_appendKey(uint32 tick, uint32 inputPump, uint16 word) {
    if (!reserveEvents((void **)&s_keys, &s_keyCapacity,
                       s_keyCount, sizeof(*s_keys))) return 0;
    s_keys[s_keyCount].tick = tick;
    s_keys[s_keyCount].inputPump = inputPump;
    s_keys[s_keyCount].word = word;
    s_keyCount++;
    return 1;
}

static int blackbox_appendAxes(uint32 tick, uint8 rawX, uint8 rawY, uint8 joyX, uint8 joyY) {
    if (!reserveEvents((void **)&s_axes, &s_axesCapacity,
                       s_axesCount, sizeof(*s_axes))) return 0;
    s_axes[s_axesCount].tick = tick;
    s_axes[s_axesCount].rawX = rawX;
    s_axes[s_axesCount].rawY = rawY;
    s_axes[s_axesCount].joyX = joyX;
    s_axes[s_axesCount].joyY = joyY;
    s_axesCount++;
    return 1;
}

static int blackbox_appendTimerPump(uint32 startTick, uint32 tickCount) {
    if (!reserveEvents((void **)&s_timerPumps, &s_timerPumpCapacity,
                       s_timerPumpCount, sizeof(*s_timerPumps))) return 0;
    s_timerPumps[s_timerPumpCount].startTick = startTick;
    s_timerPumps[s_timerPumpCount].tickCount = tickCount;
    s_timerPumpCount++;
    return 1;
}

static int blackbox_validAxes(unsigned rawX, unsigned rawY, unsigned joyX, unsigned joyY) {
    return rawX <= BLACKBOX_MAX_AXIS_VALUE && rawY <= BLACKBOX_MAX_AXIS_VALUE &&
           joyX <= BLACKBOX_MAX_AXIS_VALUE && joyY <= BLACKBOX_MAX_AXIS_VALUE;
}

static void blackbox_resetState(BlackboxMode mode, uint32 seed) {
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    blackbox_resetReplayData();
    blackbox_state_reset();
    if (blackbox_diagReset) blackbox_diagReset();
    s_mode = mode;
    s_tick = 0;
    s_inputPump = 0;
    s_pauseTick = 0xffffffffu;
    s_fastForwardTick = 0xffffffffu;
    s_seed = seed ? seed : BLACKBOX_DEFAULT_SEED;
    s_rngState = s_seed;
    s_haveRecordedAxes = 0;
    s_reportedSeedDivergence = 0;
    s_reportedRandDivergence = 0;
    s_reportedFrameDivergence = 0;
    s_reportedTimerPumpDivergence = 0;
    s_passivePresentDepth = 0;
    s_overlayPage = NULL;
    s_overlayWidth = 0;
    s_overlayHeight = 0;
    s_lastRecordedAxes.tick = 0;
    s_lastRecordedAxes.rawX = 0x80;
    s_lastRecordedAxes.rawY = 0x80;
    s_lastRecordedAxes.joyX = 0x80;
    s_lastRecordedAxes.joyY = 0x80;
}

int blackbox_startDebug(uint32 seed) {
    blackbox_resetState(BLACKBOX_DEBUG, seed);
    log_info("blackbox: debug deterministic mode, seed=%u", (unsigned)s_seed);
    return 1;
}

int blackbox_startRecord(const char *path, uint32 seed) {
    blackbox_resetState(BLACKBOX_RECORD, seed);
    s_file = fopen(path, "w");
    if (!s_file) {
        log_error("blackbox: unable to open record log '%s'", path);
        blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
        return 0;
    }
    fprintf(s_file, "F15SE2_BLACKBOX %d\n", BLACKBOX_FILE_VERSION);
    fprintf(s_file, "seed %u\n", (unsigned)s_seed);
    blackbox_state_writeRecordHeader(s_file);
    fflush(s_file);
    log_info("blackbox: recording '%s', seed=%u", path, (unsigned)s_seed);
    return 1;
}

int blackbox_startReplay(const char *path) {
    char line[8192];
    unsigned version = 0;
    FILE *f;

    blackbox_resetState(BLACKBOX_REPLAY, BLACKBOX_DEFAULT_SEED);
    f = fopen(path, "r");
    if (!f) {
        log_error("blackbox: unable to open replay log '%s'", path);
        blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
        return 0;
    }

    if (!fgets(line, sizeof(line), f) || sscanf(line, "F15SE2_BLACKBOX %u", &version) != 1 ||
        version != BLACKBOX_FILE_VERSION) {
        log_error("blackbox: unsupported replay log '%s'", path);
        fclose(f);
        blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
        return 0;
    }

    while (fgets(line, sizeof(line), f)) {
        unsigned tick, inputPump, word, rawX, rawY, joyX, joyY, seed, frame, hash, tickCount;
        int randValue;
        if (sscanf(line, "seed %u", &seed) == 1) {
            s_seed = seed ? seed : BLACKBOX_DEFAULT_SEED;
            s_rngState = s_seed;
        } else if (sscanf(line, "timer_pump %u %u", &tick, &tickCount) == 2) {
            if (tickCount > BLACKBOX_MAX_TIMER_PUMP_TICKS) {
                log_error("blackbox: invalid timer-pump count in replay log: %s", line);
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
            if (!blackbox_appendTimerPump((uint32)tick, (uint32)tickCount)) {
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
        } else if (sscanf(line, "rng_seed %u %u", &tick, &seed) == 2) {
            if (!blackbox_appendSeed((uint32)tick, seed ? (uint32)seed : BLACKBOX_DEFAULT_SEED)) {
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
        } else if (sscanf(line, "rng %u %d", &tick, &randValue) == 2) {
            if (!blackbox_appendRand((uint32)tick, randValue)) {
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
        } else if (sscanf(line, "frame %u %u %x", &tick, &frame, &hash) == 3) {
            if (!blackbox_appendFrame((uint32)tick, (uint32)frame, (uint32)hash)) {
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
        } else if (sscanf(line, "key %u %u %x", &tick, &inputPump, &word) == 3) {
            if (word > BLACKBOX_MAX_KEY_WORD) {
                log_error("blackbox: invalid key word in replay log: %s", line);
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
            if (!blackbox_appendKey((uint32)tick, (uint32)inputPump, (uint16)word)) {
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
        } else if (sscanf(line, "axes %u %u %u %u %u", &tick, &rawX, &rawY, &joyX, &joyY) == 5) {
            if (!blackbox_validAxes(rawX, rawY, joyX, joyY)) {
                log_error("blackbox: invalid axes values in replay log: %s", line);
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
            if (!blackbox_appendAxes((uint32)tick, (uint8)rawX, (uint8)rawY, (uint8)joyX, (uint8)joyY)) {
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
        } else if (sscanf(line, "phase %u", &tick) == 1) {
            /* Phase markers are for human/agent navigation; replay logic does not consume them. */
        } else {
            int parsed = blackbox_diagParseReplayLine ? blackbox_diagParseReplayLine(line) : 0;
            if (parsed == 0) parsed = blackbox_state_parseReplayLine(line);
            if (parsed > 0) continue;
            if (parsed < 0) {
                log_error("blackbox: invalid replay metadata line: %s", line);
                fclose(f);
                blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
                return 0;
            }
            log_error("blackbox: unknown replay log line: %s", line);
            fclose(f);
            blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
            return 0;
        }
    }
    fclose(f);
    if (!blackbox_state_validateReplay() ||
        (blackbox_diagValidateReplay && !blackbox_diagValidateReplay())) {
        blackbox_resetState(BLACKBOX_OFF, BLACKBOX_DEFAULT_SEED);
        return 0;
    }
    log_info("blackbox: replaying '%s', seed=%u, keys=%u, axes=%u, timer_pumps=%u, rng_seeds=%u, rng=%u, frames=%u",
             path, (unsigned)s_seed, (unsigned)s_keyCount, (unsigned)s_axesCount,
             (unsigned)s_timerPumpCount, (unsigned)s_seedCount,
             (unsigned)s_randCount, (unsigned)s_frameCount);
    return 1;
}

void blackbox_shutdown(void) {
    int pausedForInspection = blackbox_pauseReached();
    if (blackbox_diagShutdown) blackbox_diagShutdown(pausedForInspection);
    if (blackbox_replaying() && !pausedForInspection && s_keyPos != s_keyCount) {
        log_error("blackbox: replay consumed %u of %u recorded key events",
                  (unsigned)s_keyPos, (unsigned)s_keyCount);
    }
    if (blackbox_replaying() && !pausedForInspection && s_seedPos != s_seedCount) {
        log_error("blackbox: replay consumed %u of %u recorded RNG seed events",
                  (unsigned)s_seedPos, (unsigned)s_seedCount);
    }
    if (blackbox_replaying() && !pausedForInspection && s_randPos != s_randCount) {
        log_error("blackbox: replay consumed %u of %u recorded RNG values",
                  (unsigned)s_randPos, (unsigned)s_randCount);
    }
    if (blackbox_replaying() && !pausedForInspection && s_framePos != s_frameCount) {
        log_error("blackbox: replay presented %u of %u recorded frames",
                  (unsigned)s_framePos, (unsigned)s_frameCount);
    }
    if (blackbox_replaying() && !pausedForInspection && s_timerPumpPos != s_timerPumpCount) {
        log_error("blackbox: replay consumed %u of %u recorded timer-pump events",
                  (unsigned)s_timerPumpPos, (unsigned)s_timerPumpCount);
    }
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
    }
    blackbox_resetReplayData();
    blackbox_state_reset();
    s_mode = BLACKBOX_OFF;
}

int blackbox_enabled(void) { return s_mode != BLACKBOX_OFF; }
int blackbox_recording(void) { return s_mode == BLACKBOX_RECORD; }
int blackbox_replaying(void) { return s_mode == BLACKBOX_REPLAY; }
int blackbox_usesVirtualTime(void) {
    return s_mode == BLACKBOX_DEBUG || s_mode == BLACKBOX_REPLAY;
}
int blackbox_suppressPersistentWrites(void) {
    /* Blackbox record/replay sessions are investigation artifacts, not normal
     * gameplay sessions. Keep player/profile files stable even if the run
     * reaches death, debrief, or any other code path that normally saves
     * progress. The recorder itself writes its log through fopen(), not through
     * createFile(), so this does not block the .bb file. */
    return blackbox_recording() || blackbox_replaying();
}
FILE *blackbox_internalRecordFile(void) {
    return blackbox_recording() ? s_file : NULL;
}
void blackbox_setBuildVersion(const char *version) { blackbox_state_setBuildVersion(version); }
void blackbox_setAllowBuildMismatch(int allow) { blackbox_state_setAllowBuildMismatch(allow); }
uint32 blackbox_tick(void) { return s_tick; }
void blackbox_getDebugState(BlackboxDebugState *state) {
    if (!state) return;
    state->mode = s_mode;
    state->tick = s_tick;
    state->inputPump = s_inputPump;
    state->pauseTick = s_pauseTick;
    state->fastForwardTick = s_fastForwardTick;
    state->configuredSeed = s_seed;
    state->rngState = s_rngState;
    state->keyPosition = (uint32)s_keyPos;
    state->keyCount = (uint32)s_keyCount;
    state->axesPosition = (uint32)s_axesPos;
    state->axesCount = (uint32)s_axesCount;
    state->seedPosition = (uint32)s_seedPos;
    state->seedCount = (uint32)s_seedCount;
    state->rngPosition = (uint32)s_randPos;
    state->rngCount = (uint32)s_randCount;
    state->framePosition = (uint32)s_framePos;
    state->frameCount = (uint32)s_frameCount;
    state->frameIndex = s_frameIndex;
}
int blackbox_pauseReached(void) { return blackbox_enabled() && s_tick >= s_pauseTick; }
int blackbox_fastForwarding(void) {
    /* UINT32_MAX means the option was not supplied; it must not accidentally
     * turn every ordinary replay into an almost-permanent fast-forward. */
    return blackbox_replaying() && s_fastForwardTick != 0xffffffffu &&
           s_tick < s_fastForwardTick;
}

void blackbox_setPauseTick(uint32 tick) {
    s_pauseTick = tick;
    log_info("blackbox: pause tick=%u", (unsigned)s_pauseTick);
}

void blackbox_setFastForwardTick(uint32 tick) {
    s_fastForwardTick = tick;
    log_info("blackbox: fast-forward tick=%u", (unsigned)s_fastForwardTick);
}

void blackbox_noteTick(void) {
    if (blackbox_enabled() && !blackbox_pauseReached()) s_tick++;
}

void blackbox_afterTick(void) {
    /* Keep flight diagnostics optional for isolated core/timer tests. The game
     * links the implementation through its simulation and rendering hooks. */
    if (blackbox_diagOnTick) blackbox_diagOnTick();
}

void blackbox_recordTimerPump(uint32 startTick, uint32 tickCount) {
    if (!blackbox_recording() || !s_file) return;
    /* Zero-count entries are essential: they preserve which polling/render
     * iteration observed the next real 60 Hz tick. */
    fprintf(s_file, "timer_pump %u %u\n", (unsigned)startTick,
            (unsigned)tickCount);
}

uint32 blackbox_replayTimerPump(void) {
    BlackboxTimerPumpEvent event;
    if (!blackbox_replaying()) return 0;
    if (s_timerPumpPos >= s_timerPumpCount) {
        if (!s_reportedTimerPumpDivergence) {
            s_reportedTimerPumpDivergence = 1;
            log_error("blackbox: timer-pump divergence at tick %u: replay advanced past recorded schedule",
                      (unsigned)s_tick);
        }
        return 0;
    }
    event = s_timerPumps[s_timerPumpPos++];
    if (event.startTick != s_tick && !s_reportedTimerPumpDivergence) {
        s_reportedTimerPumpDivergence = 1;
        log_error("blackbox: timer-pump divergence at tick %u: expected start tick %u",
                  (unsigned)s_tick, (unsigned)event.startTick);
    }
    return event.tickCount;
}

void blackbox_noteInputPump(void) {
    /* Unlike the 60 Hz timer, this sequence advances in static menus and award
     * screens. It therefore records which polling call made a key observable,
     * including calls separated only by a key-buffer clear. */
    if (blackbox_enabled()) s_inputPump++;
}

uint64 blackbox_timerNowNs(void) {
    return (uint64)s_tick * (uint64)(1000000000u / BLACKBOX_TIMER_HZ);
}

void blackbox_seedRandom(uint32 seed) {
    s_rngState = seed ? seed : BLACKBOX_DEFAULT_SEED;
    if (blackbox_recording() && s_file) {
        fprintf(s_file, "rng_seed %u %u\n", (unsigned)s_tick, (unsigned)s_rngState);
        fflush(s_file);
    } else if (blackbox_replaying() && s_seedPos < s_seedCount) {
        BlackboxSeedEvent expected = s_seedEvents[s_seedPos++];
        /* Replay consumes the recorded seed stream as the source of truth. Tick
         * mismatches are still useful diagnostics, but allowing a locally chosen
         * seed through would make every later RNG value diverge and mask the
         * first real behavioral difference. */
        if ((expected.tick != s_tick || expected.seed != s_rngState) &&
            !s_reportedSeedDivergence) {
            s_reportedSeedDivergence = 1;
            log_error("blackbox: RNG seed replay mismatch at tick %u: recorded tick %u seed %u, local seed %u",
                      (unsigned)s_tick, (unsigned)expected.tick, (unsigned)expected.seed, (unsigned)s_rngState);
        }
        s_rngState = expected.seed ? expected.seed : BLACKBOX_DEFAULT_SEED;
    } else if (blackbox_replaying() && !s_reportedSeedDivergence) {
        s_reportedSeedDivergence = 1;
        log_error("blackbox: RNG seed divergence at tick %u: replay seeded past recorded stream with %u",
                  (unsigned)s_tick, (unsigned)s_rngState);
    }
}

uint32 blackbox_seedExternalRandom(uint32 externalSeed) {
    if (blackbox_recording()) {
        blackbox_seedRandom(externalSeed);
    } else if (blackbox_replaying()) {
        if (s_seedPos < s_seedCount) {
            BlackboxSeedEvent expected = s_seedEvents[s_seedPos++];
            /* A wall-clock seed is expected to differ between runs. Its event
             * position and tick are deterministic; its numeric value is not. */
            if (expected.tick != s_tick && !s_reportedSeedDivergence) {
                s_reportedSeedDivergence = 1;
                log_error("blackbox: external RNG seed replay mismatch at tick %u: recorded tick %u seed %u",
                          (unsigned)s_tick, (unsigned)expected.tick,
                          (unsigned)expected.seed);
            }
            s_rngState = expected.seed ? expected.seed : BLACKBOX_DEFAULT_SEED;
        } else if (!s_reportedSeedDivergence) {
            s_reportedSeedDivergence = 1;
            log_error("blackbox: external RNG seed divergence at tick %u: replay consumed past recorded stream",
                      (unsigned)s_tick);
        }
    } else if (blackbox_enabled()) {
        /* Debug mode has no recording to restore, so retain its configured seed
         * instead of introducing wall-clock state. */
        s_rngState = s_seed;
    }
    return s_rngState;
}

void blackbox_seedConfiguredRandom(void) {
    blackbox_seedRandom(s_seed);
}

int blackbox_rand15(void) {
    int value;
    /* ANSI/DOS-style LCG shape: deterministic across platforms, unlike libc rand(). */
    s_rngState = s_rngState * 1103515245u + 12345u;
    value = (int)((s_rngState >> 16) & BLACKBOX_RAND_MASK);
    if (blackbox_recording() && s_file) {
        fprintf(s_file, "rng %u %d\n", (unsigned)s_tick, value);
        fflush(s_file);
    } else if (blackbox_replaying() && s_randPos < s_randCount) {
        BlackboxRandEvent expected = s_randEvents[s_randPos++];
        if ((expected.tick != s_tick || expected.value != value) &&
            !s_reportedRandDivergence) {
            s_reportedRandDivergence = 1;
            log_error("blackbox: RNG replay mismatch at tick %u: recorded tick %u value %d, local value %d",
                      (unsigned)s_tick, (unsigned)expected.tick, expected.value, value);
        }
        value = expected.value;
    } else if (blackbox_replaying() && !s_reportedRandDivergence) {
        s_reportedRandDivergence = 1;
        log_error("blackbox: RNG divergence at tick %u: replay consumed past recorded stream, got %d",
                  (unsigned)s_tick, value);
    }
    return value;
}

int blackbox_randomActive(void) {
    return blackbox_enabled();
}

void blackbox_logPhase(const char *phase) {
    if (!blackbox_recording() || !s_file) return;
    fprintf(s_file, "phase %u %s\n", (unsigned)s_tick, phase ? phase : "unknown");
    fflush(s_file);
}

void blackbox_recordKey(uint16 word) {
    if (!blackbox_recording() || !s_file) return;
    fprintf(s_file, "key %u %u %04x\n", (unsigned)s_tick,
            (unsigned)s_inputPump, (unsigned)word);
    fflush(s_file);
}

int blackbox_replayNextKey(uint16 *word) {
    if (!blackbox_replaying() || s_keyPos >= s_keyCount) return 0;
    if (s_keys[s_keyPos].inputPump > s_inputPump) return 0;
    *word = s_keys[s_keyPos].word;
    s_keyPos++;
    return 1;
}

void blackbox_recordAxes(uint8 rawX, uint8 rawY, uint8 joyX, uint8 joyY) {
    if (!blackbox_recording() || !s_file) return;
    if (s_haveRecordedAxes &&
        s_lastRecordedAxes.rawX == rawX && s_lastRecordedAxes.rawY == rawY &&
        s_lastRecordedAxes.joyX == joyX && s_lastRecordedAxes.joyY == joyY) {
        return;
    }
    s_haveRecordedAxes = 1;
    s_lastRecordedAxes.tick = s_tick;
    s_lastRecordedAxes.rawX = rawX;
    s_lastRecordedAxes.rawY = rawY;
    s_lastRecordedAxes.joyX = joyX;
    s_lastRecordedAxes.joyY = joyY;
    fprintf(s_file, "axes %u %u %u %u %u\n",
            (unsigned)s_tick, (unsigned)rawX, (unsigned)rawY, (unsigned)joyX, (unsigned)joyY);
    fflush(s_file);
}

void blackbox_applyReplayAxes(uint8 *rawX, uint8 *rawY, uint8 *joyX, uint8 *joyY) {
    if (!blackbox_replaying()) return;
    while (s_axesPos < s_axesCount && s_axes[s_axesPos].tick <= s_tick) {
        s_currentAxes = s_axes[s_axesPos];
        s_axesPos++;
    }
    *rawX = s_currentAxes.rawX;
    *rawY = s_currentAxes.rawY;
    *joyX = s_currentAxes.joyX;
    *joyY = s_currentAxes.joyY;
}

static uint32 blackbox_hashSurface(SDL_Surface *page) {
    uint32 hash = 2166136261u;
    if (!page || page->format != SDL_PIXELFORMAT_INDEX8) return 0;
    if (SDL_MUSTLOCK(page) && !SDL_LockSurface(page)) return 0;
    for (int y = 0; y < page->h; y++) {
        const uint8 *row = (const uint8 *)page->pixels + (size_t)y * page->pitch;
        for (int x = 0; x < page->w; x++) {
            hash ^= row[x];
            hash *= 16777619u;
        }
    }
    if (SDL_MUSTLOCK(page)) SDL_UnlockSurface(page);
    return hash;
}

void blackbox_recordFrame(SDL_Surface *page) {
    uint32 hash;
    /* Frame hashes diagnose divergence; they must never drive the clock or
     * otherwise affect execution. In particular, host window expose events are
     * intentionally excluded because their timing is outside the recording. */
    if (!blackbox_enabled() || !page || s_passivePresentDepth != 0) return;
    hash = blackbox_hashSurface(page);
    if (blackbox_recording() && s_file) {
        fprintf(s_file, "frame %u %u %08x\n", (unsigned)s_tick, (unsigned)s_frameIndex, (unsigned)hash);
        fflush(s_file);
    } else if (blackbox_replaying() && s_framePos < s_frameCount) {
        BlackboxFrameEvent expected = s_frames[s_framePos++];
        if ((expected.frame != s_frameIndex || expected.tick != s_tick ||
             expected.hash != hash) && !s_reportedFrameDivergence) {
            s_reportedFrameDivergence = 1;
            log_error("blackbox: frame divergence at tick %u frame %u: expected tick %u frame %u hash %08x, got %08x",
                      (unsigned)s_tick, (unsigned)s_frameIndex, (unsigned)expected.tick,
                      (unsigned)expected.frame, (unsigned)expected.hash, (unsigned)hash);
        }
    } else if (blackbox_replaying() && !s_reportedFrameDivergence) {
        s_reportedFrameDivergence = 1;
        log_error("blackbox: frame divergence at tick %u frame %u: replay presented past recorded stream, hash %08x",
                  (unsigned)s_tick, (unsigned)s_frameIndex, (unsigned)hash);
    }
    s_frameIndex++;
}

void blackbox_beginPassivePresent(void) {
    s_passivePresentDepth++;
}

void blackbox_endPassivePresent(void) {
    if (s_passivePresentDepth != 0) s_passivePresentDepth--;
}

static const unsigned char kDigits[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}
};

static void putPixel(SDL_Surface *page, int x, int y, uint8 color) {
    if (x < 0 || y < 0 || x >= page->w || y >= page->h) return;
    ((uint8 *)page->pixels)[(size_t)y * page->pitch + x] = color;
}

static void drawDigit(SDL_Surface *page, int x, int y, int digit, uint8 color) {
    int row, col;
    for (row = 0; row < 5; row++) {
        unsigned char bits = kDigits[digit][row];
        for (col = 0; col < 3; col++) {
            if (bits & (1u << (2 - col))) putPixel(page, x + col, y + row, color);
        }
    }
}

void blackbox_drawDebugOverlay(SDL_Surface *page) {
    char text[16];
    int i, x;
    uint32 tick = s_tick;
    uint64 displayedTime;

    if (!blackbox_enabled() || !page || page->format != SDL_PIXELFORMAT_INDEX8 ||
        s_overlayPage) return;
    if (SDL_MUSTLOCK(page) && !SDL_LockSurface(page)) return;

    s_overlayPage = page;
    s_overlayWidth = page->w < 52 ? page->w : 52;
    s_overlayHeight = page->h < 7 ? page->h : 7;
    for (int y = 0; y < s_overlayHeight; y++) {
        memcpy(s_overlayBackup[y],
               (uint8 *)page->pixels + (size_t)y * page->pitch,
               (size_t)s_overlayWidth);
    }

    for (int y = 0; y < 7 && y < page->h; y++) {
        for (int px = 0; px < 52 && px < page->w; px++) putPixel(page, px, y, BLACKBOX_DEBUG_BG);
    }

    displayedTime = (uint64)(tick / BLACKBOX_TIMER_HZ) * 100u +
                    (uint64)(tick % BLACKBOX_TIMER_HZ);
    snprintf(text, sizeof(text), "%llu", (unsigned long long)displayedTime);
    x = 1;
    for (i = 0; text[i]; i++) {
        if (text[i] >= '0' && text[i] <= '9') {
            drawDigit(page, x, 1, text[i] - '0', BLACKBOX_DEBUG_FG);
            x += 4;
        }
    }

    if (SDL_MUSTLOCK(page)) SDL_UnlockSurface(page);
}

void blackbox_restoreDebugOverlay(SDL_Surface *page) {
    if (!page || page != s_overlayPage) return;
    if (SDL_MUSTLOCK(page) && !SDL_LockSurface(page)) return;
    for (int y = 0; y < s_overlayHeight; y++) {
        memcpy((uint8 *)page->pixels + (size_t)y * page->pitch,
               s_overlayBackup[y], (size_t)s_overlayWidth);
    }
    if (SDL_MUSTLOCK(page)) SDL_UnlockSurface(page);
    s_overlayPage = NULL;
    s_overlayWidth = 0;
    s_overlayHeight = 0;
}

void blackbox_captureMutableFile(const char *name, const uint8 *data, uint32 size) {
    if (!blackbox_recording() || !s_file) return;
    blackbox_state_recordMutableFile(s_file, name, data, size);
}

int blackbox_shouldCaptureMutableFile(const char *name) {
    if (!blackbox_recording()) return 0;
    return blackbox_state_shouldCaptureMutableFile(name);
}

int blackbox_replayMutableFile(const char *name, const uint8 **data, uint32 *size) {
    if (!blackbox_replaying()) return 0;
    return blackbox_state_getReplayMutableFile(name, data, size);
}
