#ifndef SLOT_H
#define SLOT_H

#include <dos.h>
#include "inttype.h"

/* ---- misc / input (first slot 0x5a, 6 used) ----
 * Swappable input overlay (keyboard + joystick). */

/* Return 0 when an event is queued, 0xFFFF otherwise. */
int16 FAR CDECL misc_checkKeyBuf(void);

/* Pop the next key event, return scancode<<8 | ascii. */
int16 FAR CDECL misc_getKey(void);
int16 FAR CDECL misc_slot5c(void);

/* Return the joystick axis value for the requested axis. */
int16 FAR CDECL misc_readJoystick(int16 axis);

/* Flush the event queue / reset modifier state. */
void FAR CDECL misc_clearKeyFlags(void);

/* slots 0x5f-0x63: unknown/unused input slots. */
int16 FAR CDECL misc_slot5f(void);
int16 FAR CDECL misc_slot60(void);
int16 FAR CDECL misc_slot61(void);
int16 FAR CDECL misc_slot62(void);
int16 FAR CDECL misc_slot63(void);
/* Init the driver. sampleDataSeg = segment holding the digitized
 * voice/sample blob (stored for later sample playback); variantSel selects how
 * many sample variants are valid (1/2/3, derived from the digitized-sound caps). */
int16 FAR CDECL audio_setup(int16 sampleDataSeg, int16 variantSel);

/* Stop all playing channels and reset mixer state. */
int16 FAR CDECL audio_shutdown(void);

/* play the sample/effect mapped to soundId. */
int16 FAR CDECL audio_playSound(int16 soundId);

/* Start the title music track. */
int16 FAR CDECL audio_playIntro(void);

/* Start/unpause the looping engine sound. */
int16 FAR CDECL audio_engineDroneOn(void);

/* Stop/pause the looping engine sound. */
int16 FAR CDECL audio_engineDroneOff(void);

/* Update the engine loop's playback rate/pitch from knots+thrust. */
int16 FAR CDECL audio_setEnginePitch(int16 knots, int16 thrust);

/* Advance the sequencer one tick. */
int16 FAR CDECL audio_timerTick(void);

/* Apply per-tick noise/modulation to active channels. */
int16 FAR CDECL audio_noiseTick(void);

/* Play the sample for sampleIdx. */
int16 FAR CDECL audio_playSample(int16 sampleIdx);

#endif /* SLOT_H */
