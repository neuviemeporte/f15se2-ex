/*
 * asound_replacements.h - Optional editable replacements for ASOUND assets.
 */
#ifndef ASOUND_REPLACEMENTS_H
#define ASOUND_REPLACEMENTS_H

#include "asound_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AsoundReplacementCue {
    const AsoundU8 *samples;
    int sample_count;
    int sample_rate;
} AsoundReplacementCue;

/* Reload all known cue WAVs. Missing or malformed files remain legacy fallbacks. */
int asound_load_replacement_cues(void);

/* Return a loaded cue matching the original inclusive sample range. */
int asound_find_replacement_cue(AsoundU16 start, AsoundU16 end_inclusive,
                                AsoundReplacementCue *cue);

#ifdef __cplusplus
}
#endif

#endif /* ASOUND_REPLACEMENTS_H */
