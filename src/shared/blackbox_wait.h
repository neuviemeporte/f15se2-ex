#ifndef F15_SE2_BLACKBOX_WAIT_H
#define F15_SE2_BLACKBOX_WAIT_H

#include "inttype.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*BlackboxPollInputFn)(void);

/* Runs a bounded wait on blackbox virtual time. Returns zero outside blackbox
 * mode so the caller can retain its normal wall-clock implementation. */
int blackbox_virtualWait(uint32 durationTicks, int stopOnInput,
                         BlackboxPollInputFn pollInput);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_WAIT_H */
