#ifndef F15_SE2_BLACKBOX_CLI_H
#define F15_SE2_BLACKBOX_CLI_H

#include "inttype.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BlackboxCliOptions {
    const char *recordPath;
    const char *replayPath;
    uint32 seed;
    uint32 pauseTick;
    uint32 fastForwardTick;
    uint32 dumpTick;
    int debug;
    int ignoreBuild;
    int captureRender;
} BlackboxCliOptions;

void blackbox_cliInit(BlackboxCliOptions *options);
void blackbox_cliPrintUsage(void);
/* Returns 1 when recognized, 0 when not a blackbox option, and -1 on error. */
int blackbox_cliParseOption(BlackboxCliOptions *options, int argc,
                            char **argv, int *index);
int blackbox_cliStart(const BlackboxCliOptions *options);

#ifdef __cplusplus
}
#endif

#endif /* F15_SE2_BLACKBOX_CLI_H */
