#include "log.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdlib.h>

/* Application name prepended to each line; empty until log_set_app() is called. */
static const char *app_tag = "";

void log_set_app(const char *name) {
    app_tag = name ? name : "";
}

/* Raise every log category to VERBOSE (--verbose), so the LogVerbose/LogDebug
 * trace macros — below SDL's default INFO threshold — become visible. */
void log_set_verbose(void) {
    SDL_SetLogPriorities(SDL_LOG_PRIORITY_VERBOSE);
}

#ifdef __DJGPP__
#include <stdio.h>
#define DOS_LOG_FILE "F15.LOG"
/* SDL's default log sink is stderr, which real DOS's COMMAND.COM can't redirect
 * to a file (no 2>/2>&1 support, unlike DOSBox's host shell or bash) and which
 * is invisible anyway once mode 13h is active. Log to a file instead so a
 * hang/hard-reset on real hardware still leaves a trail. Each line is written
 * with its own open+close so a partial log survives even a hard lockup. */
static void dosFileLogOutput(void *userdata, int category, SDL_LogPriority priority, const char *message) {
    (void)userdata;
    (void)category;
    (void)priority;
    FILE *f = fopen(DOS_LOG_FILE, "a");
    if (!f) return;
    fprintf(f, "%s\n", message);
    fclose(f);
}

__attribute__((constructor)) static void installDosFileLog(void) {
    /* Truncate once per run so the file doesn't grow unbounded across launches
     * and each run's log stands alone; per-line writes below then append. */
    FILE *f = fopen(DOS_LOG_FILE, "w");
    if (f) fclose(f);
    SDL_SetLogOutputFunction(dosFileLogOutput, NULL);
}
#endif

static void emit(SDL_LogPriority prio, const char *fmt, va_list ap) {
    char buf[1024];
    SDL_vsnprintf(buf, sizeof buf, fmt, ap);
    if (*app_tag)
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s: %s", app_tag, buf);
    else
        SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, prio, "%s", buf);
}

#define LOG_FN(name, prio)            \
    void name(const char *fmt, ...) { \
        va_list ap;                   \
        va_start(ap, fmt);            \
        emit(prio, fmt, ap);          \
        va_end(ap);                   \
    }

LOG_FN(log_message, SDL_LOG_PRIORITY_INFO)
LOG_FN(log_verbose, SDL_LOG_PRIORITY_VERBOSE)
LOG_FN(log_debug, SDL_LOG_PRIORITY_DEBUG)
LOG_FN(log_info, SDL_LOG_PRIORITY_INFO)
LOG_FN(log_warn, SDL_LOG_PRIORITY_WARN)
LOG_FN(log_error, SDL_LOG_PRIORITY_ERROR)

void log_critical(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    emit(SDL_LOG_PRIORITY_CRITICAL, fmt, ap);
    va_end(ap);
    exit(1);
}
