#include "log.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdlib.h>

/* Application name prepended to each line; empty until log_set_app() is called. */
static const char *app_tag = "";

void log_set_app(const char *name) {
    app_tag = name ? name : "";
}

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
