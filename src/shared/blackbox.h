#ifndef F15_SE2_BLACKBOX_H
#define F15_SE2_BLACKBOX_H

#include "inttype.h"
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum BlackboxMode {
    BLACKBOX_OFF = 0,
    BLACKBOX_DEBUG,
    BLACKBOX_RECORD,
    BLACKBOX_REPLAY
} BlackboxMode;

typedef struct BlackboxDebugState {
    BlackboxMode mode;
    uint32 tick;
    uint32 inputPump;
    uint32 pauseTick;
    uint32 fastForwardTick;
    uint32 configuredSeed;
    uint32 rngState;
    uint32 keyPosition, keyCount;
    uint32 axesPosition, axesCount;
    uint32 seedPosition, seedCount;
    uint32 rngPosition, rngCount;
    uint32 framePosition, frameCount, frameIndex;
} BlackboxDebugState;

enum {
    BLACKBOX_DEFAULT_SEED = 1
};

int blackbox_startDebug(uint32 seed);
int blackbox_startRecord(const char *path, uint32 seed);
int blackbox_startReplay(const char *path);
void blackbox_shutdown(void);
void blackbox_setPauseTick(uint32 tick);
void blackbox_setFastForwardTick(uint32 tick);
void blackbox_setBuildVersion(const char *version);
void blackbox_setAllowBuildMismatch(int allow);

int blackbox_enabled(void);
int blackbox_recording(void);
int blackbox_replaying(void);
/* Debug/replay own a synthetic clock. Recording observes the native 60 Hz
 * clock so enabling a recorder cannot change gameplay speed. */
int blackbox_usesVirtualTime(void);
int blackbox_suppressPersistentWrites(void);
uint32 blackbox_tick(void);
void blackbox_getDebugState(BlackboxDebugState *state);
int blackbox_pauseReached(void);
/* True only while replay is advancing toward an explicitly requested tick. */
int blackbox_fastForwarding(void);

void blackbox_noteTick(void);
/* Called after the timer's optional gameplay hook has completed. */
void blackbox_afterTick(void);
/* Record/replay the number of native 60 Hz ticks produced by one timerPump()
 * invocation. This preserves real-time recording without making replay depend
 * on host speed or renderer timing. */
void blackbox_recordTimerPump(uint32 startTick, uint32 tickCount);
uint32 blackbox_replayTimerPump(void);
/* Count shared input-pump calls separately from timer ticks. Static menu phases
 * continue polling input even when their 60 Hz timer is not installed. */
void blackbox_noteInputPump(void);
uint64 blackbox_timerNowNs(void);

void blackbox_seedRandom(uint32 seed);
/* Seeds originating outside deterministic state (currently the DOS time-of-day
 * clock) are recorded once and restored verbatim on replay. */
uint32 blackbox_seedExternalRandom(uint32 externalSeed);
void blackbox_seedConfiguredRandom(void);
int blackbox_rand15(void);
int blackbox_randomActive(void);
void blackbox_logPhase(const char *phase);

void blackbox_recordKey(uint16 word);
int blackbox_replayNextKey(uint16 *word);
void blackbox_recordAxes(uint8 rawX, uint8 rawY, uint8 joyX, uint8 joyY);
void blackbox_applyReplayAxes(uint8 *rawX, uint8 *rawY, uint8 *joyX, uint8 *joyY);

void blackbox_recordFrame(SDL_Surface *page);
/* Window-system repaint requests are presentation side effects, not game
 * frames. Keep them out of the deterministic checksum stream. Calls may nest. */
void blackbox_beginPassivePresent(void);
void blackbox_endPassivePresent(void);
void blackbox_drawDebugOverlay(SDL_Surface *page);
void blackbox_restoreDebugOverlay(SDL_Surface *page);
int blackbox_shouldCaptureMutableFile(const char *name);
void blackbox_captureMutableFile(const char *name, const uint8 *data, uint32 size);
int blackbox_replayMutableFile(const char *name, const uint8 **data, uint32 *size);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_H */
