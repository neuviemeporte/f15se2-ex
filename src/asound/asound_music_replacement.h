/*
 * asound_music_replacement.h - Editable ASOUND intro/release stream override.
 */
#ifndef ASOUND_MUSIC_REPLACEMENT_H
#define ASOUND_MUSIC_REPLACEMENT_H

#include "asound_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reload sounds/intro_music.asound.json; all twelve streams are required.
 * Call only while no replacement stream is playing.
 */
int asound_reload_replacement_music(void);

/*
 * Initialize all driver voices from replacement streams.
 * release_phase selects intro (zero) or release (nonzero). Returns zero when
 * the caller should use the original compiled streams.
 */
int asound_start_replacement_music(AsoundDriver *driver, int release_phase);

/* Return one loaded stream and its exact byte length for diagnostics/tools. */
int asound_replacement_music_stream(int release_phase, int voice,
                                    const AsoundU8 **data, size_t *size);

#ifdef __cplusplus
}
#endif

#endif /* ASOUND_MUSIC_REPLACEMENT_H */
