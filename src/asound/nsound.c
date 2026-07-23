/*
 * nsound.c - Silent audio backend (the AUDIO=OFF build).
 *
 * Drop-in replacement for asound_sdl.c that implements the game-facing audio_*
 * slot ABI (slot.h) and loadF15DgtlBin (egcode.h) as no-ops: no SDL audio
 * subsystem, no OPL3 emulator, no Sound Blaster. Selected by -DAUDIO=OFF and
 * named after the original NSOUND.EXE (MicroProse's own no-sound driver).
 *
 * Every entry point returns the slot-ABI success code (0) so callers behave
 * exactly as they do when the real backend has no device. Useful for porting /
 * bring-up on a new platform where the audio path is unproven, so a hang can be
 * isolated to (or ruled out of) the sound subsystem without a rebuild dance.
 */

#include <SDL3/SDL.h>

#include "inttype.h"
#include <dos.h>
#include "slot.h"
#include "log.h"

int FAR CDECL audio_setup(int16 sampleDataSeg, int16 variantSel) {
    (void)sampleDataSeg;
    (void)variantSel;
    LogInfo(("nsound: audio disabled (AUDIO=OFF build)"));
    return 0;
}

int FAR CDECL audio_shutdown(void) { return 0; }
int FAR CDECL audio_playSound(int soundId) {
    (void)soundId;
    return 0;
}
int FAR CDECL audio_playIntro(void) { return 0; }
int FAR CDECL audio_engineDroneOn(void) { return 0; }
int FAR CDECL audio_engineDroneOff(void) { return 0; }
int FAR CDECL audio_setEnginePitch(int knots, int thrust) {
    (void)knots;
    (void)thrust;
    return 0;
}
int FAR CDECL audio_timerTick(void) { return 0; }
int FAR CDECL audio_noiseTick(void) { return 0; }
int FAR CDECL audio_playSample(int sampleIdx) {
    (void)sampleIdx;
    return 0;
}

/* The digitized-blob loader's return value doubles as audio_setup's variant
 * selector and the voice-cue gate; 0 means "no cues", the correct silent state. */
int loadF15DgtlBin(void) { return 0; }
