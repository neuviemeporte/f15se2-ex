#ifndef F15_SE2_LOG_H
#define F15_SE2_LOG_H

/* Tag every log line with the owning application ("start", "egame", ...).
 * Call once from each program's main(); harmless to omit (lines are untagged). */
void log_set_app(const char *name);

void log_message(const char *fmt, ...);
void log_verbose(const char *fmt, ...);
void log_debug(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);
void log_critical(const char *fmt, ...);

#define Log(x) log_message x
#define LogVerbose(x) log_verbose x
#define LogDebug(x) log_debug x
#define LogInfo(x) log_info x
#define LogWarn(x) log_warn x
#define LogError(x) log_error x
#define LogCritical(x) log_critical x

#endif /* F15_SE2_LOG_H */
