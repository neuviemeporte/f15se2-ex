#include "pointers.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static FILE *stream = NULL;

void log_close() {
    if (!stream) return;
    fclose(stream);
    stream = NULL;
}

void my_vtrace(const char* fmt, va_list ap) {
    static long lasttime = 0;
    const long thistime = time(NULL);
    long timedelta;
    if (stream == NULL) {
        lasttime = time(NULL);
        stream = fopen("f15.log", "a");
        if (stream == NULL) {
            printf("Unable to open debug stream");
            exit(1);
        }
        setbuf(stream, NULL);
        fprintf(stream, "Successfully opened debug log\n");
    }
    timedelta = thistime - lasttime;
    lasttime = thistime;
    fprintf(stream, "[%04lds] ", timedelta);
    vfprintf(stream, fmt, ap);
    fprintf(stream, "\n");
    fflush(stream);
}

void my_trace(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    my_vtrace(fmt, ap);
    va_end(ap);
}

static char tracebuf[128];

void my_fartrace(const char far *msg, ...) {
    const char FAR *ptr = msg;
    size_t size = 0, idx;
    va_list ap;
    while (*ptr++ != '\0') size++;
    ptr = msg;
    for (idx = 0; idx < size && idx < 127; ++idx) {
      tracebuf[idx] = *ptr++;
    }
    tracebuf[idx] = '\0';
    va_start(ap, msg);
    my_vtrace(tracebuf, ap);
    va_end(ap);
}

void ftoncpy(void *near_ptr, const void far *far_ptr, uint32 size) {
    const uint8 far *src = (uint8 far *)far_ptr;
    uint8 *dest = (uint8 *)near_ptr;
    uint32 i;

    for (i = 0; i < size; i++) {
        dest[i] = src[i];
    }
}

void changeext(char *filename, const char *ext) {
    char *dot = strchr(filename, '.');
    if (!dot) {
        dot = filename + strlen(filename);
        *dot = '.';
    }
    strncpy(dot + 1, ext, 3);
}
