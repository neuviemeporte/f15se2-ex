#ifndef F15_SE2_BLACKBOX_STATE_H
#define F15_SE2_BLACKBOX_STATE_H

#include "inttype.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void blackbox_state_reset(void);
void blackbox_state_setBuildVersion(const char *version);
void blackbox_state_setAllowBuildMismatch(int allow);
const char *blackbox_state_currentBuildVersion(void);
const char *blackbox_state_replayBuildVersion(void);
void blackbox_state_writeRecordHeader(FILE *file);
int blackbox_state_parseReplayLine(const char *line);
int blackbox_state_validateReplay(void);
int blackbox_state_shouldCaptureMutableFile(const char *name);
void blackbox_state_recordMutableFile(FILE *file, const char *name, const uint8 *data, uint32 size);
int blackbox_state_getReplayMutableFile(const char *name, const uint8 **data, uint32 *size);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_STATE_H */
