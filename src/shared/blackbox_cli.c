#include "blackbox_cli.h"

#include "blackbox.h"
#include "blackbox_diag.h"
#include "version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int parseUint32Option(const char *option, const char *value,
                             uint32 *result) {
    char *end = NULL;
    unsigned long parsed;
    if (!value || value[0] == '-') {
        fprintf(stderr, "Invalid value for %s: %s\n", option,
                value ? value : "(null)");
        return 0;
    }
    errno = 0;
    parsed = strtoul(value, &end, 0);
    if (errno || end == value || *end != '\0' || parsed > 0xfffffffful) {
        fprintf(stderr, "Invalid value for %s: %s\n", option, value);
        return 0;
    }
    *result = (uint32)parsed;
    return 1;
}

static int parseDisplayedTimeOption(const char *option, const char *value,
                                    uint32 *result) {
    uint32 displayed;
    uint32 subtick;
    if (!parseUint32Option(option, value, &displayed)) return 0;
    subtick = displayed % 100u;
    if (subtick >= 60u) {
        fprintf(stderr, "Invalid value for %s: %s (last two digits must be 00..59)\n",
                option, value);
        return 0;
    }
    /* The UI shows seconds*100 + 1/60-second remainder. Convert that compact
     * decimal notation back to the raw deterministic tick used internally. */
    *result = (displayed / 100u) * 60u + subtick;
    return 1;
}

static const char *requiredArgument(const char *option, int argc, char **argv,
                                    int *index) {
    if (*index + 1 >= argc) {
        fprintf(stderr, "Option requires an argument: %s\n", option);
        return NULL;
    }
    return argv[++*index];
}

void blackbox_cliInit(BlackboxCliOptions *options) {
    memset(options, 0, sizeof(*options));
    options->seed = BLACKBOX_DEFAULT_SEED;
    options->pauseTick = 0xffffffffu;
    options->fastForwardTick = 0xffffffffu;
    options->dumpTick = 0xffffffffu;
}

void blackbox_cliPrintUsage(void) {
    printf("                  [--blackbox-debug] [--blackbox-seed n]\n"
           "                  [--blackbox-record path | --blackbox-replay path]\n"
           "--blackbox-debug       Deterministic debug mode with top-left tick time\n"
           "--blackbox-seed n      Deterministic RNG seed for debug/record mode, default 1\n"
           "--blackbox-record path Record tick-stamped input/RNG blackbox log\n"
           "--blackbox-replay path Replay a previously recorded blackbox log\n"
           "--blackbox-replay-ignore-build\n"
           "                      Replay even when the log was recorded by a different build\n"
           "--blackbox-capture-render\n"
           "                      Record every 3D scene/object/line submission (large log)\n"
           "--blackbox-fast-forward-tick n\n"
           "                      Replay at maximum speed until displayed time n\n"
           "--blackbox-dump-tick n Write text and JSON snapshots at displayed time n\n"
           "--blackbox-pause-tick n Freeze at displayed time n (seconds*100 + tick)\n");
}

int blackbox_cliParseOption(BlackboxCliOptions *options, int argc,
                            char **argv, int *index) {
    const char *option = argv[*index];
    const char *argument;
    if (strcmp(option, "--blackbox-debug") == 0) {
        options->debug = 1;
        return 1;
    }
    if (strcmp(option, "--blackbox-replay-ignore-build") == 0) {
        options->ignoreBuild = 1;
        return 1;
    }
    if (strcmp(option, "--blackbox-capture-render") == 0) {
        options->captureRender = 1;
        return 1;
    }
    if (strcmp(option, "--blackbox-record") == 0 ||
        strcmp(option, "--blackbox-replay") == 0) {
        argument = requiredArgument(option, argc, argv, index);
        if (!argument) return -1;
        if (strcmp(option, "--blackbox-record") == 0)
            options->recordPath = argument;
        else
            options->replayPath = argument;
        return 1;
    }
    if (strcmp(option, "--blackbox-seed") == 0 ||
        strcmp(option, "--blackbox-pause-tick") == 0 ||
        strcmp(option, "--blackbox-dump-tick") == 0 ||
        strcmp(option, "--blackbox-fast-forward-tick") == 0) {
        argument = requiredArgument(option, argc, argv, index);
        if (!argument) return -1;
        uint32 *destination = &options->pauseTick;
        if (strcmp(option, "--blackbox-seed") == 0)
            destination = &options->seed;
        else if (strcmp(option, "--blackbox-fast-forward-tick") == 0)
            destination = &options->fastForwardTick;
        else if (strcmp(option, "--blackbox-dump-tick") == 0)
            destination = &options->dumpTick;
        if (strcmp(option, "--blackbox-seed") == 0) {
            if (!parseUint32Option(option, argument, destination)) return -1;
        } else if (!parseDisplayedTimeOption(option, argument, destination)) {
            return -1;
        }
        if (strcmp(option, "--blackbox-dump-tick") == 0 && *destination == 0) {
            fprintf(stderr, "Invalid value for %s: %s (first capturable tick is 1)\n",
                    option, argument);
            return -1;
        }
        return 1;
    }
    return 0;
}

int blackbox_cliStart(const BlackboxCliOptions *options) {
    blackbox_setBuildVersion(F15_VERSION);
    blackbox_setAllowBuildMismatch(options->ignoreBuild);
    if (options->recordPath && options->replayPath) {
        fprintf(stderr, "Choose only one of --blackbox-record or --blackbox-replay\n");
        return 0;
    }
    if (options->fastForwardTick != 0xffffffffu && !options->replayPath) {
        fprintf(stderr, "--blackbox-fast-forward-tick requires --blackbox-replay\n");
        return 0;
    }
    if (options->captureRender && !options->recordPath) {
        fprintf(stderr, "--blackbox-capture-render requires --blackbox-record\n");
        return 0;
    }
    if (options->dumpTick != 0xffffffffu && !options->debug &&
        !options->recordPath && !options->replayPath) {
        fprintf(stderr, "--blackbox-dump-tick requires blackbox debug, record, or replay mode\n");
        return 0;
    }
    if (options->fastForwardTick != 0xffffffffu &&
        options->pauseTick != 0xffffffffu &&
        options->pauseTick < options->fastForwardTick) {
        fprintf(stderr, "--blackbox-pause-tick cannot precede --blackbox-fast-forward-tick\n");
        return 0;
    }
    if (options->dumpTick != 0xffffffffu &&
        options->pauseTick != 0xffffffffu &&
        options->pauseTick < options->dumpTick) {
        fprintf(stderr, "--blackbox-pause-tick cannot precede --blackbox-dump-tick\n");
        return 0;
    }
    if (options->replayPath) {
        if (!blackbox_startReplay(options->replayPath)) return 0;
    } else if (options->recordPath) {
        if (!blackbox_startRecord(options->recordPath, options->seed)) return 0;
    } else if (options->debug) {
        if (!blackbox_startDebug(options->seed)) return 0;
    }
    if (options->pauseTick != 0xffffffffu)
        blackbox_setPauseTick(options->pauseTick);
    if (options->fastForwardTick != 0xffffffffu)
        blackbox_setFastForwardTick(options->fastForwardTick);
    if (options->dumpTick != 0xffffffffu)
        blackbox_diagSetDumpTick(options->dumpTick);
    blackbox_diagSetRenderCapture(options->captureRender);
    return 1;
}
